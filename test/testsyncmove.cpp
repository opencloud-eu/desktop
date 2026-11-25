/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include "testutils/syncenginetestutils.h"
#include "testutils/testutils.h"

#include <QtTest>
#include <syncengine.h>
#include <theme.h>

using namespace OCC;
using namespace Qt::StringLiterals;

bool itemSuccessful(const ItemCompletedSpy &spy, const QString &path, const SyncInstructions instr)
{
    auto item = spy.findItem(path);
    return item->_status == SyncFileItem::Success && item->instruction() == instr;
}

bool itemConflict(const ItemCompletedSpy &spy, const QString &path)
{
    auto item = spy.findItem(path);
    return item->_status == SyncFileItem::Conflict && item->instruction() == CSYNC_INSTRUCTION_CONFLICT;
}

bool itemSuccessfulMove(const ItemCompletedSpy &spy, const QString &path)
{
    return itemSuccessful(spy, path, CSYNC_INSTRUCTION_RENAME);
}

QStringList findConflicts(const FileInfo &dir)
{
    QStringList conflicts;
    for (const auto &item : dir.children) {
        if (item.name.contains(QLatin1String("(conflicted copy"))) {
            conflicts.append(item.path());
        }
    }
    return conflicts;
}

bool expectAndWipeConflict(FileModifier &local, FileInfo state, const QString &path)
{
    PathComponents pathComponents(path);
    auto base = state.find(pathComponents.parentDirComponents());
    if (!base)
        return false;
    for (const auto &item : std::as_const(base->children)) {
        if (item.name.startsWith(pathComponents.fileName()) && item.name.contains(QLatin1String("(conflicted copy"))) {
            local.remove(item.path());
            return true;
        }
    }
    return false;
}

class TestSyncMove : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase_data()
    {
        QTest::addColumn<Vfs::Mode>("vfsMode");
        QTest::addColumn<bool>("filesAreDehydrated");

        QTest::newRow("Vfs::Mode::Off") << Vfs::Mode::Off << false;

        if (VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::WindowsCfApi)) {
            QTest::newRow("Vfs::Mode::WindowsCfApi dehydrated") << Vfs::Mode::WindowsCfApi << true;
            QTest::newRow("Vfs::Mode::WindowsCfApi hydrated") << Vfs::Mode::WindowsCfApi << false;
        } else if (Utility::isWindows()) {
            qWarning("Skipping Vfs::Mode::WindowsCfApi");
        }
    }

    void testRemoteChangeInMovedFolder()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        // issue #5192
        FakeFolder fakeFolder(FileInfo { QString(), { FileInfo { QStringLiteral("folder"), { FileInfo { QStringLiteral("folderA"), { { QStringLiteral("file.txt"), 400 } } }, QStringLiteral("folderB") } } } }, vfsMode, filesAreDehydrated);

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // Edit a file in a moved directory.
        fakeFolder.remoteModifier().setContents("folder/folderA/file.txt"_L1, FileInfo::DefaultFileSize, 'a');
        fakeFolder.remoteModifier().rename("folder/folderA"_L1, "folder/folderB/folderA"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        auto oldState = fakeFolder.currentLocalState();
        QVERIFY(oldState.find("folder/folderB/folderA/file.txt"_L1));
        QVERIFY(!oldState.find("folder/folderA/file.txt"_L1));

        // This sync should not remove the file
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(fakeFolder.currentLocalState(), oldState);
    }

    void testSelectiveSyncMovedFolder()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        // issue #5224
        FakeFolder fakeFolder(FileInfo { QString(), { FileInfo { QStringLiteral("parentFolder"), { FileInfo { QStringLiteral("subFolderA"), { { QStringLiteral("fileA.txt"), 400 } } }, FileInfo { QStringLiteral("subFolderB"), { { QStringLiteral("fileB.txt"), 400 } } } } } } }, vfsMode, filesAreDehydrated);

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        auto expectedServerState = fakeFolder.currentRemoteState();

        // Remove subFolderA with selectiveSync:
        fakeFolder.syncEngine().journal()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, {"parentFolder/subFolderA/"_L1});
        fakeFolder.syncEngine().journal()->schedulePathForRemoteDiscovery(QByteArrayLiteral("parentFolder/subFolderA/"));

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        {
            // Nothing changed on the server
            QCOMPARE(fakeFolder.currentRemoteState(), expectedServerState);
            // The local state should not have subFolderA
            auto remoteState = fakeFolder.currentRemoteState();
            remoteState.remove("parentFolder/subFolderA"_L1);
            QCOMPARE(fakeFolder.currentLocalState(), remoteState);
        }

        // Rename parentFolder on the server
        fakeFolder.remoteModifier().rename("parentFolder"_L1, "parentFolderRenamed"_L1);
        expectedServerState = fakeFolder.currentRemoteState();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        {
            QCOMPARE(fakeFolder.currentRemoteState(), expectedServerState);
            auto remoteState = fakeFolder.currentRemoteState();
            // The subFolderA should still be there on the server.
            QVERIFY(remoteState.find("parentFolderRenamed/subFolderA/fileA.txt"_L1));
            // But not on the client because of the selective sync
            remoteState.remove("parentFolderRenamed/subFolderA"_L1);
            QCOMPARE(fakeFolder.currentLocalState(), remoteState);
        }

        // Rename it again, locally this time.
        fakeFolder.localModifier().rename("parentFolderRenamed"_L1, "parentThirdName"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        {
            auto remoteState = fakeFolder.currentRemoteState();
            // The subFolderA should still be there on the server.
            QVERIFY(remoteState.find("parentThirdName/subFolderA/fileA.txt"_L1));
            // But not on the client because of the selective sync
            remoteState.remove("parentThirdName/subFolderA"_L1);
            QCOMPARE(fakeFolder.currentLocalState(), remoteState);

            expectedServerState = fakeFolder.currentRemoteState();
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync()); // This sync should do nothing
            QCOMPARE(completeSpy.count(), 0);

            QCOMPARE(fakeFolder.currentRemoteState(), expectedServerState);
            QCOMPARE(fakeFolder.currentLocalState(), remoteState);
        }
    }

    void testLocalMoveDetection()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        fakeFolder.account()->setCapabilities({fakeFolder.account()->url(), TestUtils::testCapabilities(CheckSums::Algorithm::ADLER32)});

        OperationCounter counter(fakeFolder);

        // For directly editing the remote checksum
        FileInfo &remoteInfo = fakeFolder.remoteModifier();
        // Simple move causing a remote rename
        fakeFolder.localModifier().rename("A/a1"_L1, "A/a1m"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), remoteInfo);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(remoteInfo));
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 1);
        QCOMPARE(counter.nDELETE, 0);
        counter.reset();

        // Move-and-change, mtime+size, causing a upload and delete
        QVERIFY(fakeFolder.currentLocalState().find("A/a2"_L1)->isDehydratedPlaceholder
            == filesAreDehydrated); // no-one touched it, so the hydration state should be the same as the initial state
        auto mt = fakeFolder.currentLocalState().find("A/a2"_L1)->lastModified();
        QVERIFY(mt.toSecsSinceEpoch() + 1 < QDateTime::currentDateTime().toSecsSinceEpoch());
        fakeFolder.localModifier().rename("A/a2"_L1, "A/a2m"_L1);
        fakeFolder.localModifier().setContents("A/a2m"_L1, fakeFolder.remoteModifier().contentSize + 1, 'x');
        fakeFolder.localModifier().setModTime("A/a2m"_L1, mt.addSecs(1));
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(!fakeFolder.currentLocalState()
                .find("A/a2m"_L1)
                ->isDehydratedPlaceholder); // We overwrote all data in the file, so whatever the state was before, it is no longer dehydrated
        QCOMPARE(fakeFolder.currentLocalState(), remoteInfo);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(remoteInfo));
        QCOMPARE(counter.nGET, filesAreDehydrated ? 1 : 0); // on winvfs, with a dehydrated file, the os will try to hydrate the file before we write to it. When the file is hydrated, it doesn't need to be fetched.
        QCOMPARE(counter.nMOVE, 0); // we cannot detect moves (and we didn't implement it yet in winvfs), so ...
        QCOMPARE(counter.nDELETE, 1); // ... the file just disappears, and ...
        QCOMPARE(counter.nPUT, 1); // ... another file (with just 1 byte difference) appears somewhere else. Coincidence.
        counter.reset();
        // Move-and-change, mtime+content only
        fakeFolder.localModifier().rename("B/b1"_L1, "B/b1m"_L1);
        fakeFolder.localModifier().setContents("B/b1m"_L1, FileModifier::DefaultFileSize, 'C');
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), remoteInfo);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(remoteInfo));
        QCOMPARE(counter.nPUT, 1);
        QCOMPARE(counter.nDELETE, 1);
        counter.reset();

        // Move-and-change, size+content only
        auto mtime = fakeFolder.remoteModifier().find("B/b2"_L1)->lastModified();
        fakeFolder.localModifier().rename("B/b2"_L1, "B/b2m"_L1);
        fakeFolder.localModifier().appendByte("B/b2m"_L1);
        fakeFolder.localModifier().setModTime("B/b2m"_L1, mtime);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), remoteInfo);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(remoteInfo));
        if (!filesAreDehydrated) {
            QCOMPARE(counter.nGET, 0); // b2m is detected as a *new* file, so we don't need to fetch the contents
        } else {
            QCOMPARE(counter.nGET, 1); // b2 is accessed, so we get a callback to download the file
        }
        QCOMPARE(counter.nMOVE, 0); // content differs, so not a move
        QCOMPARE(counter.nPUT, 1); // upload b2m
        QCOMPARE(counter.nDELETE, 1); // delete b2
        counter.reset();

        // WinVFS handles this just fine.
        if (vfsMode == Vfs::Mode::Off) {
            // Move-and-change, content only -- c1 has no checksum, so we fail to detect this!
            // NOTE: This is an expected failure.
            mtime = fakeFolder.remoteModifier().find("C/c1"_L1)->lastModified();
            auto size = fakeFolder.currentRemoteState().find("C/c1"_L1)->contentSize;
            fakeFolder.localModifier().rename("C/c1"_L1, "C/c1m"_L1);
            fakeFolder.localModifier().setContents("C/c1m"_L1, size, 'C');
            fakeFolder.localModifier().setModTime("C/c1m"_L1, mtime);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            QCOMPARE(counter.nPUT, 0);
            QCOMPARE(counter.nDELETE, 0);
            QVERIFY(fakeFolder.currentLocalState() != remoteInfo);
            counter.reset();

            // cleanup, and upload a file that will have a checksum in the db
            fakeFolder.localModifier().remove("C/c1m"_L1);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            QCOMPARE(counter.nDELETE, 1);
            counter.reset();
        } else {
            // no rename happened, nothing to clean up
        }

        fakeFolder.localModifier().insert("C/c3"_L1, 13_B, 'E'); // 13, because c1 (and c2) have a size of 24 bytes, so this file is clearly different
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), remoteInfo);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(remoteInfo));
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nMOVE, 0);
        QCOMPARE(counter.nPUT, 1);
        QCOMPARE(counter.nDELETE, 0);
        counter.reset();

        // Move-and-change, content only, this time while having a checksum
        mtime = fakeFolder.remoteModifier().find("C/c3"_L1)->lastModified();
        fakeFolder.localModifier().rename("C/c3"_L1, "C/c3m"_L1);
        fakeFolder.localModifier().setContents("C/c3m"_L1, FileModifier::DefaultFileSize, 'C');
        fakeFolder.localModifier().setModTime("C/c3m"_L1, mtime);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nMOVE, 0);
        QCOMPARE(counter.nPUT, 1);
        QCOMPARE(counter.nDELETE, 1);
        QCOMPARE(fakeFolder.currentLocalState(), remoteInfo);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(remoteInfo));
        counter.reset();
    }

    void testDuplicateFileId_data()
    {
        QTest::addColumn<QString>("prefix");

        // There have been bugs related to how the original
        // folder and the folder with the duplicate tree are
        // ordered. Test both cases here.
        QTest::newRow("first ordering") << "O"; // "O" > "A"
        QTest::newRow("second ordering") << "0"; // "0" < "A"
    }

    // If the same folder is shared in two different ways with the same
    // user, the target user will see duplicate file ids. We need to make
    // sure the move detection and sync still do the right thing in that
    // case.
    void testDuplicateFileId()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);
        QFETCH(QString, prefix);

        if (filesAreDehydrated) {
            QSKIP("This test expects to be able to modify local files on disk, which does not work with dehydrated files.");
        }

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        auto &remote = fakeFolder.remoteModifier();

        remote.mkdir("A/W"_L1);
        remote.insert("A/W/w1"_L1);
        remote.mkdir("A/Q"_L1);

        // Duplicate every entry in A under O/A
        remote.mkdir(prefix);
        remote.children[prefix].addChild(remote.children["A"_L1]);

        // This already checks that the rename detection doesn't get
        // horribly confused if we add new files that have the same
        // fileid as existing ones
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        OperationCounter counter(fakeFolder);

        // Try a remote file move
        remote.rename("A/a1"_L1, "A/W/a1m"_L1);
        remote.rename(prefix + "/A/a1"_L1, prefix + "/A/W/a1m"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nGET, 0);

        // And a remote directory move
        remote.rename("A/W"_L1, "A/Q/W"_L1);
        remote.rename(prefix + "/A/W"_L1, prefix + "/A/Q/W"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nGET, 0);

        // Partial file removal (in practice, A/a2 may be moved to O/a2, but we don't care)
        remote.rename(prefix + "/A/a2"_L1, prefix + "/a2"_L1);
        remote.remove("A/a2"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nGET, 0);

        // Local change plus remote move at the same time
        fakeFolder.localModifier().appendByte(prefix + "/a2"_L1);
        remote.rename(prefix + "/a2"_L1, prefix + "/a3"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nGET, 1);
        counter.reset();

        // remove localy, and remote move at the same time
        fakeFolder.localModifier().remove("A/Q/W/a1m"_L1);
        remote.rename("A/Q/W/a1m"_L1, "A/Q/W/a1p"_L1);
        remote.rename(prefix + "/A/Q/W/a1m"_L1, prefix + "/A/Q/W/a1p"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nGET, 1);
        counter.reset();
    }

    void testMovePropagation()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        auto &local = fakeFolder.localModifier();
        auto &remote = fakeFolder.remoteModifier();

        OperationCounter counter(fakeFolder);

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        counter.reset();

        // Move
        {
            local.rename("A/a1"_L1, "A/a1m"_L1);
            remote.rename("B/b1"_L1, "B/b1m"_L1);
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
            QCOMPARE(counter.nGET, 0);
            QCOMPARE(counter.nPUT, 0);
            QCOMPARE(counter.nMOVE, 1);
            QCOMPARE(counter.nDELETE, 0);
            QVERIFY(itemSuccessfulMove(completeSpy, "A/a1m"_L1));
            QVERIFY(itemSuccessfulMove(completeSpy, "B/b1m"_L1));
            QCOMPARE(completeSpy.findItem("A/a1m"_L1)->localName(), "A/a1"_L1);
            QCOMPARE(completeSpy.findItem("A/a1m"_L1)->_renameTarget, "A/a1m"_L1);
            QCOMPARE(completeSpy.findItem("B/b1m"_L1)->localName(), "B/b1"_L1);
            QCOMPARE(completeSpy.findItem("B/b1m"_L1)->_renameTarget, "B/b1m"_L1);
            counter.reset();
        }

        // Touch+Move on same side
        local.rename("A/a2"_L1, "A/a2m"_L1);
        local.setContents("A/a2m"_L1, FileModifier::DefaultFileSize, 'A');
        remote.rename("B/b2"_L1, "B/b2m"_L1);
        remote.setContents("B/b2m"_L1, FileModifier::DefaultFileSize, 'A');
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 1);
        QCOMPARE(counter.nPUT, 1);
        QCOMPARE(counter.nMOVE, 0);
        QCOMPARE(counter.nDELETE, 1);
        QCOMPARE(remote.find("A/a2m"_L1)->contentChar, 'A');
        QCOMPARE(remote.find("B/b2m"_L1)->contentChar, 'A');
        counter.reset();

        // Touch+Move on opposite sides
        local.rename("A/a1m"_L1, "A/a1m2"_L1);
        remote.setContents("A/a1m"_L1, FileModifier::DefaultFileSize, 'B');
        remote.rename("B/b1m"_L1, "B/b1m2"_L1);
        local.setContents("B/b1m"_L1, FileModifier::DefaultFileSize, 'B');

        if (filesAreDehydrated) {
            // When a placeholder is accessed, a hydration request is made. This will fail, because
            // the file (b1m) is no longer available on the server. We then return an error, so the
            // helper will fail to do the local modifications.
            QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
            QSKIP("Behaviour for dehydrated files is different at this point compared to no-VFS");
        }

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 2);
        QCOMPARE(counter.nPUT, 2);
        QCOMPARE(counter.nMOVE, 0);
        QCOMPARE(counter.nDELETE, 0);

        // All these files existing afterwards is debatable. Should we propagate
        // the rename in one direction and grab the new contents in the other?
        // Currently there's no propagation job that would do that, and this does
        // at least not lose data.
        QVERIFY(remote.find("A/a1m"_L1)->contentChar == 'B');
        QVERIFY(remote.find("B/b1m"_L1)->contentChar == 'B');
        QVERIFY(remote.find("A/a1m2"_L1)->contentChar == 'W');
        QVERIFY(remote.find("B/b1m2"_L1)->contentChar == 'W');
        QCOMPARE(remote.find("A/a1m"_L1)->contentChar, 'B');
        QCOMPARE(remote.find("B/b1m"_L1)->contentChar, 'B');
        QCOMPARE(remote.find("A/a1m2"_L1)->contentChar, 'W');
        QCOMPARE(remote.find("B/b1m2"_L1)->contentChar, 'W');
        counter.reset();

        // Touch+create on one side, move on the other
        {
            local.appendByte("A/a1m"_L1);
            local.insert("A/a1mt"_L1);
            remote.rename("A/a1m"_L1, "A/a1mt"_L1);
            remote.appendByte("B/b1m"_L1);
            remote.insert("B/b1mt"_L1);
            local.rename("B/b1m"_L1, "B/b1mt"_L1);
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            // First check the counters:
            QCOMPARE(counter.nGET, 3);
            QCOMPARE(counter.nPUT, 1);
            QCOMPARE(counter.nMOVE, 0);
            QCOMPARE(counter.nDELETE, 0);
            // Ok, now we can remove the conflicting files. This needs disk access, so it might trigger server interaction. (Hence checking the counters before we do this.)
            QVERIFY(expectAndWipeConflict(local, fakeFolder.currentLocalState(), "A/a1mt"_L1));
            QVERIFY(expectAndWipeConflict(local, fakeFolder.currentLocalState(), "B/b1mt"_L1));
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            // Now we can compare the clean-up states:
            QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
            QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
            QVERIFY(itemSuccessful(completeSpy, "A/a1m"_L1, CSYNC_INSTRUCTION_NEW));
            QVERIFY(itemSuccessful(completeSpy, "B/b1m"_L1, CSYNC_INSTRUCTION_NEW));
            QVERIFY(itemConflict(completeSpy, "A/a1mt"_L1));
            QVERIFY(itemConflict(completeSpy, "B/b1mt"_L1));
            counter.reset();
        }

        // Create new on one side, move to new on the other
        {
            local.insert("A/a1N"_L1, 13_B);
            remote.rename("A/a1mt"_L1, "A/a1N"_L1);
            remote.insert("B/b1N"_L1, 13_B);
            local.rename("B/b1mt"_L1, "B/b1N"_L1);
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            // First check the counters:
            QCOMPARE(counter.nGET, 2);
            QCOMPARE(counter.nPUT, 0);
            QCOMPARE(counter.nMOVE, 0);
            QCOMPARE(counter.nDELETE, 1);
            // Ok, now we can remove the conflicting files. This needs disk access, so it might trigger server interaction. (Hence checking the counters before we do this.)
            QVERIFY(expectAndWipeConflict(local, fakeFolder.currentLocalState(), "A/a1N"_L1));
            QVERIFY(expectAndWipeConflict(local, fakeFolder.currentLocalState(), "B/b1N"_L1));
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            // Now we can compare the clean-up states:
            QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
            QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
            QVERIFY(itemSuccessful(completeSpy, "A/a1mt"_L1, CSYNC_INSTRUCTION_REMOVE));
            QVERIFY(itemSuccessful(completeSpy, "B/b1mt"_L1, CSYNC_INSTRUCTION_REMOVE));
            QVERIFY(itemConflict(completeSpy, "A/a1N"_L1));
            QVERIFY(itemConflict(completeSpy, "B/b1N"_L1));
            counter.reset();
        }

        // Local move, remote move
        local.rename("C/c1"_L1, "C/c1mL"_L1);
        remote.rename("C/c1"_L1, "C/c1mR"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        // end up with both files
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 1);
        QCOMPARE(counter.nPUT, 1);
        QCOMPARE(counter.nMOVE, 0);
        QCOMPARE(counter.nDELETE, 0);

        // Rename/rename conflict on a folder
        counter.reset();
        remote.rename("C"_L1, "CMR"_L1);
        local.rename("C"_L1, "CML"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        // End up with both folders
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 3); // 3 files in C
        QCOMPARE(counter.nPUT, 3);
        QCOMPARE(counter.nMOVE, 0);
        QCOMPARE(counter.nDELETE, 0);
        counter.reset();

        // Folder move
        {
            local.rename("A"_L1, "AM"_L1);
            remote.rename("B"_L1, "BM"_L1);
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
            QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
            QCOMPARE(counter.nGET, 0);
            QCOMPARE(counter.nPUT, 0);
            QCOMPARE(counter.nMOVE, 1);
            QCOMPARE(counter.nDELETE, 0);
            QVERIFY(itemSuccessfulMove(completeSpy, "AM"_L1));
            QVERIFY(itemSuccessfulMove(completeSpy, "BM"_L1));
            QCOMPARE(completeSpy.findItem("AM"_L1)->localName(), "A"_L1);
            QCOMPARE(completeSpy.findItem("AM"_L1)->_renameTarget, "AM"_L1);
            QCOMPARE(completeSpy.findItem("BM"_L1)->localName(), "B"_L1);
            QCOMPARE(completeSpy.findItem("BM"_L1)->_renameTarget, "BM"_L1);
            counter.reset();
        }

        // Folder move with contents touched on the same side
        {
            local.setContents("AM/a2m"_L1, FileModifier::DefaultFileSize, 'C');
            // We must change the modtime for it is likely that it did not change between sync.
            // (Previous version of the client (<=2.5) would not need this because it was always doing
            // checksum comparison for all renames. But newer version no longer does it if the file is
            // renamed because the parent folder is renamed)
            local.setModTime("AM/a2m"_L1, QDateTime::currentDateTimeUtc().addDays(3));
            local.rename("AM"_L1, "A2"_L1);
            remote.setContents("BM/b2m"_L1, FileModifier::DefaultFileSize, 'C');
            remote.rename("BM"_L1, "B2"_L1);
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
            QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
            QCOMPARE(counter.nGET, 1);
            QCOMPARE(counter.nPUT, 1);
            QCOMPARE(counter.nMOVE, 1);
            QCOMPARE(counter.nDELETE, 0);
            QCOMPARE(remote.find("A2/a2m"_L1)->contentChar, 'C');
            QCOMPARE(remote.find("B2/b2m"_L1)->contentChar, 'C');
            QVERIFY(itemSuccessfulMove(completeSpy, "A2"_L1));
            QVERIFY(itemSuccessfulMove(completeSpy, "B2"_L1));
            counter.reset();
        }

        // Folder rename with contents touched on the other tree
        remote.setContents("A2/a2m"_L1, FileModifier::DefaultFileSize, 'D');
        // setContents alone may not produce updated mtime if the test is fast
        // and since we don't use checksums here, that matters.
        remote.appendByte("A2/a2m"_L1);
        local.rename("A2"_L1, "A3"_L1);
        local.setContents("B2/b2m"_L1, FileModifier::DefaultFileSize, 'D');
        local.appendByte("B2/b2m"_L1);
        remote.rename("B2"_L1, "B3"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 1);
        QCOMPARE(counter.nPUT, 1);
        QCOMPARE(counter.nMOVE, 1);
        QCOMPARE(counter.nDELETE, 0);
        QCOMPARE(remote.find("A3/a2m"_L1)->contentChar, 'D');
        QCOMPARE(remote.find("B3/b2m"_L1)->contentChar, 'D');
        counter.reset();

        // Folder rename with contents touched on both ends
        remote.setContents("A3/a2m"_L1, FileModifier::DefaultFileSize, 'R');
        remote.appendByte("A3/a2m"_L1);
        local.setContents("A3/a2m"_L1, FileModifier::DefaultFileSize, 'L');
        local.appendByte("A3/a2m"_L1);
        local.appendByte("A3/a2m"_L1);
        local.rename("A3"_L1, "A4"_L1);
        remote.setContents("B3/b2m"_L1, FileModifier::DefaultFileSize, 'R');
        remote.appendByte("B3/b2m"_L1);
        local.setContents("B3/b2m"_L1, FileModifier::DefaultFileSize, 'L');
        local.appendByte("B3/b2m"_L1);
        local.appendByte("B3/b2m"_L1);
        remote.rename("B3"_L1, "B4"_L1);
        QThread::sleep(1); // This test is timing-sensitive. No idea why, it's probably the modtime on the client side.
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        qDebug() << counter;
        QCOMPARE(counter.nGET, 2);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 1);
        QCOMPARE(counter.nDELETE, 0);
        auto currentLocal = fakeFolder.currentLocalState();
        auto conflicts = findConflicts(currentLocal.children["A4"_L1]);
        QCOMPARE(conflicts.size(), 1);
        for (const auto &c : std::as_const(conflicts)) {
            QCOMPARE(currentLocal.find(c)->contentChar, 'L');
            local.remove(c);
        }
        conflicts = findConflicts(currentLocal.children["B4"_L1]);
        QCOMPARE(conflicts.size(), 1);
        for (const auto &c : std::as_const(conflicts)) {
            QCOMPARE(currentLocal.find(c)->contentChar, 'L');
            local.remove(c);
        }
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(remote.find("A4/a2m"_L1)->contentChar, 'R');
        QCOMPARE(remote.find("B4/b2m"_L1)->contentChar, 'R');
        counter.reset();

        // Rename a folder and rename the contents at the same time
        local.rename("A4/a2m"_L1, "A4/a2m2"_L1);
        local.rename("A4"_L1, "A5"_L1);
        remote.rename("B4/b2m"_L1, "B4/b2m2"_L1);
        remote.rename("B4"_L1, "B5"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 2);
        QCOMPARE(counter.nDELETE, 0);
    }

    // These renames can be troublesome on windows
    void testRenameCaseOnly()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        auto &local = fakeFolder.localModifier();
        auto &remote = fakeFolder.remoteModifier();

        OperationCounter counter(fakeFolder);

        local.rename("A/a1"_L1, "A/A1"_L1);
        remote.rename("A/a2"_L1, "A/A2"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), remote);
        QCOMPARE(printDbData(fakeFolder.dbState()), printDbData(fakeFolder.currentRemoteState()));
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 1);
        QCOMPARE(counter.nDELETE, 0);
    }

    // Check interaction of moves with file type changes
    void testMoveAndTypeChange()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        auto &local = fakeFolder.localModifier();
        auto &remote = fakeFolder.remoteModifier();

        // Touch on one side, rename and mkdir on the other
        {
            local.appendByte("A/a1"_L1);
            remote.rename("A/a1"_L1, "A/a1mq"_L1);
            remote.mkdir("A/a1"_L1);
            remote.appendByte("B/b1"_L1);
            local.rename("B/b1"_L1, "B/b1mq"_L1);
            local.mkdir("B/b1"_L1);
            ItemCompletedSpy completeSpy(fakeFolder);
            if (filesAreDehydrated) {
                // the dehydrating the placeholder failed as the metadata is out of sync
                QSignalSpy spy(fakeFolder.vfs().get(), &Vfs::needSync);
                QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
                QCOMPARE(spy.count(), 1);
                QVERIFY(fakeFolder.syncOnce());
                QVERIFY(fakeFolder.applyLocalModificationsAndSync());
                QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
            } else {
                QVERIFY(fakeFolder.applyLocalModificationsAndSync());
                QCOMPARE(findConflicts(fakeFolder.currentLocalState().children["A"_L1]).size(), 1);
                QCOMPARE(findConflicts(fakeFolder.currentLocalState().children["B"_L1]).size(), 1);
            }
        }
    }

    // https://github.com/owncloud/client/issues/6629#issuecomment-402450691
    // When a file is moved and the server mtime was not in sync, the local mtime should be kept
    void testMoveAndMTimeChange()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        OperationCounter counter(fakeFolder);

        // Changing the mtime on the server (without invalidating the etag)
        fakeFolder.remoteModifier().find("A/a1"_L1)->setLastModified(QDateTime::currentDateTime().addSecs(-50000));
        fakeFolder.remoteModifier().find("A/a2"_L1)->setLastModified(QDateTime::currentDateTime().addSecs(-40000));

        // Move a few files
        fakeFolder.remoteModifier().rename("A/a1"_L1, "A/a1_server_renamed"_L1);
        fakeFolder.localModifier().rename("A/a2"_L1, "A/a2_local_renamed"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 1);
        QCOMPARE(counter.nDELETE, 0);

        // Another sync should do nothing
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 1);
        QCOMPARE(counter.nDELETE, 0);

        // Check that everything other than the mtime is still equal:
        QVERIFY(fakeFolder.currentLocalState().equals(fakeFolder.currentRemoteState(), FileInfo::IgnoreLastModified));
    }

    // Test for https://github.com/owncloud/client/issues/6694
    void testInvertFolderHierarchy()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        fakeFolder.remoteModifier().mkdir("A/Empty"_L1);
        fakeFolder.remoteModifier().mkdir("A/Empty/Foo"_L1);
        fakeFolder.remoteModifier().mkdir("C/AllEmpty"_L1);
        fakeFolder.remoteModifier().mkdir("C/AllEmpty/Bar"_L1);
        fakeFolder.remoteModifier().insert("A/Empty/f1"_L1);
        fakeFolder.remoteModifier().insert("A/Empty/Foo/f2"_L1);
        fakeFolder.remoteModifier().mkdir("C/AllEmpty/f3"_L1);
        fakeFolder.remoteModifier().mkdir("C/AllEmpty/Bar/f4"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        OperationCounter counter(fakeFolder);

        // "Empty" is after "A", alphabetically
        fakeFolder.localModifier().rename("A/Empty"_L1, "Empty"_L1);
        fakeFolder.localModifier().rename("A"_L1, "Empty/A"_L1);

        // "AllEmpty" is before "C", alphabetically
        fakeFolder.localModifier().rename("C/AllEmpty"_L1, "AllEmpty"_L1);
        fakeFolder.localModifier().rename("C"_L1, "AllEmpty/C"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nDELETE, 0);
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);

        // Now, the revert, but "crossed"
        fakeFolder.localModifier().rename("Empty/A"_L1, "A"_L1);
        fakeFolder.localModifier().rename("AllEmpty/C"_L1, "C"_L1);
        fakeFolder.localModifier().rename("Empty"_L1, "C/Empty"_L1);
        fakeFolder.localModifier().rename("AllEmpty"_L1, "A/AllEmpty"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nDELETE, 0);
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);

        // Reverse on remote
        fakeFolder.remoteModifier().rename("A/AllEmpty"_L1, "AllEmpty"_L1);
        fakeFolder.remoteModifier().rename("C/Empty"_L1, "Empty"_L1);
        fakeFolder.remoteModifier().rename("C"_L1, "AllEmpty/C"_L1);
        fakeFolder.remoteModifier().rename("A"_L1, "Empty/A"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nDELETE, 0);
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
    }

    void testDeepHierarchy_data()
    {
        QTest::addColumn<bool>("local");
        QTest::newRow("remote") << false;
        QTest::newRow("local") << true;
    }

    void testDeepHierarchy()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);
        QFETCH(bool, local);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        FileModifier &modifier = local ? fakeFolder.localModifier() : fakeFolder.remoteModifier();

        modifier.mkdir("FolA"_L1);
        modifier.mkdir("FolA/FolB"_L1);
        modifier.mkdir("FolA/FolB/FolC"_L1);
        modifier.mkdir("FolA/FolB/FolC/FolD"_L1);
        modifier.mkdir("FolA/FolB/FolC/FolD/FolE"_L1);
        modifier.insert("FolA/FileA.txt"_L1);
        modifier.insert("FolA/FolB/FileB.txt"_L1);
        modifier.insert("FolA/FolB/FolC/FileC.txt"_L1);
        modifier.insert("FolA/FolB/FolC/FolD/FileD.txt"_L1);
        modifier.insert("FolA/FolB/FolC/FolD/FolE/FileE.txt"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        OperationCounter counter(fakeFolder);

        modifier.insert("FolA/FileA2.txt"_L1);
        modifier.insert("FolA/FolB/FileB2.txt"_L1);
        modifier.insert("FolA/FolB/FolC/FileC2.txt"_L1);
        modifier.insert("FolA/FolB/FolC/FolD/FileD2.txt"_L1);
        modifier.insert("FolA/FolB/FolC/FolD/FolE/FileE2.txt"_L1);
        modifier.rename("FolA"_L1, "FolA_Renamed"_L1);
        modifier.rename("FolA_Renamed/FolB"_L1, "FolB_Renamed"_L1);
        modifier.rename("FolB_Renamed/FolC"_L1, "FolA"_L1);
        modifier.rename("FolA/FolD"_L1, "FolA/FolD_Renamed"_L1);
        modifier.mkdir("FolB_Renamed/New"_L1);
        modifier.rename("FolA/FolD_Renamed/FolE"_L1, "FolB_Renamed/New/FolE"_L1);
        auto expected = local ? fakeFolder.currentLocalState() : fakeFolder.currentRemoteState();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(counter.nDELETE, local ? 1 : 0); // FolC was is renamed to an existing name, so it is not considered as renamed
        // There was 5 inserts
        QCOMPARE(counter.nGET, local || filesAreDehydrated ? 0 : 5);
        QCOMPARE(counter.nPUT, local ? 5 : 0);
    }

    void renameOnBothSides()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        OperationCounter counter(fakeFolder);

        // Test that renaming a file within a directory that was renamed on the other side actually do a rename.

        // 1) move the folder alphabeticaly before
        fakeFolder.remoteModifier().rename("A/a1"_L1, "A/a1m"_L1);
        fakeFolder.localModifier().rename("A"_L1, "_A"_L1);
        fakeFolder.localModifier().rename("B/b1"_L1, "B/b1m"_L1);
        fakeFolder.remoteModifier().rename("B"_L1, "_B"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentRemoteState(), fakeFolder.currentRemoteState());
        QVERIFY(fakeFolder.currentRemoteState().find("_A/a1m"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("_B/b1m"_L1));
        QCOMPARE(counter.nDELETE, 0);
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 2);
        counter.reset();

        // 2) move alphabetically after
        fakeFolder.remoteModifier().rename("_A/a2"_L1, "_A/a2m"_L1);
        fakeFolder.localModifier().rename("_B/b2"_L1, "_B/b2m"_L1);
        fakeFolder.localModifier().rename("_A"_L1, "S/A"_L1);
        fakeFolder.remoteModifier().rename("_B"_L1, "S/B"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentRemoteState(), fakeFolder.currentRemoteState());
        QVERIFY(fakeFolder.currentRemoteState().find("S/A/a2m"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("S/B/b2m"_L1));
        QCOMPARE(counter.nDELETE, 0);
        QCOMPARE(counter.nGET, 0);
        QCOMPARE(counter.nPUT, 0);
        QCOMPARE(counter.nMOVE, 2);
    }

    void moveFileToDifferentFolderOnBothSides()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        OperationCounter counter(fakeFolder);

        QCOMPARE(fakeFolder.currentLocalState().find("B/b1"_L1)->isDehydratedPlaceholder, filesAreDehydrated);
        QCOMPARE(fakeFolder.currentLocalState().find("B/b2"_L1)->isDehydratedPlaceholder, filesAreDehydrated);

        // Test that moving a file within to different folder on both side does the right thing.

        fakeFolder.remoteModifier().rename("B/b1"_L1, "A/b1"_L1);
        fakeFolder.localModifier().rename("B/b1"_L1, "C/b1"_L1);

        fakeFolder.localModifier().rename("B/b2"_L1, "A/b2"_L1);
        fakeFolder.remoteModifier().rename("B/b2"_L1, "C/b2"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentRemoteState(), fakeFolder.currentRemoteState());
        // The easy checks: the server always has the data, so it can successfully move the files:
        QVERIFY(fakeFolder.currentRemoteState().find("A/b1"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("C/b2"_L1));
        // Either the client has hydrated files, in which case it will upload the data to the target locations;
        // or the files were dehydrated, so it has to remove the files. (No data-loss in the latter case: the
        // files were dehydrated, so there was no data anyway.)
        QVERIFY(fakeFolder.currentRemoteState().find("C/b1"_L1) || filesAreDehydrated);
        QVERIFY(fakeFolder.currentRemoteState().find("A/b2"_L1) || filesAreDehydrated);

        QCOMPARE(counter.nMOVE, 0); // Unfortunately, we can't really make a move in this case
        QCOMPARE(counter.nGET, filesAreDehydrated ? 0 : 2);
        QCOMPARE(counter.nPUT, filesAreDehydrated ? 0 : 2);
        QCOMPARE(counter.nDELETE, 0);
        counter.reset();
    }

    // Test that deletes don't run before renames
    void testRenameParallelism()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder({ FileInfo {} }, vfsMode, filesAreDehydrated);
        fakeFolder.remoteModifier().mkdir("A"_L1);
        fakeFolder.remoteModifier().insert("A/file"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        {
            auto localState = fakeFolder.currentLocalState();
            FileInfo *localFile = localState.find("A/file"_L1);
            QVERIFY(localFile != nullptr); // check if the file exists
            QCOMPARE(vfsMode == Vfs::Mode::Off || localFile->isDehydratedPlaceholder, vfsMode == Vfs::Mode::Off || filesAreDehydrated);

            auto remoteState = fakeFolder.currentRemoteState();
            FileInfo *remoteFile = remoteState.find("A/file"_L1);
            QVERIFY(remoteFile != nullptr);
            QCOMPARE(localFile->lastModified(), remoteFile->lastModified());

            QCOMPARE(localState, remoteState);
        }

        fakeFolder.localModifier().mkdir("B"_L1);
        fakeFolder.localModifier().rename("A/file"_L1, "B/file"_L1);
        fakeFolder.localModifier().remove("A"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        {
            auto localState = fakeFolder.currentLocalState();
            QVERIFY(localState.find("A/file"_L1) == nullptr); // check if the file is gone
            QVERIFY(localState.find("A"_L1) == nullptr); // check if the directory is gone
            FileInfo *localFile = localState.find("B/file"_L1);
            QVERIFY(localFile != nullptr); // check if the file exists
            QCOMPARE(vfsMode == Vfs::Mode::Off || localFile->isDehydratedPlaceholder, vfsMode == Vfs::Mode::Off || filesAreDehydrated);

            auto remoteState = fakeFolder.currentRemoteState();
            FileInfo *remoteFile = remoteState.find("B/file"_L1);
            QVERIFY(remoteFile != nullptr);
            QCOMPARE(localFile->lastModified(), remoteFile->lastModified());

            QCOMPARE(localState, remoteState);
        }
    }

    void testMovedWithError_data()
    {
        QTest::addColumn<Vfs::Mode>("vfsMode");

        QTest::newRow("Vfs::Mode::Off") << Vfs::Mode::Off;
#ifdef Q_OS_WIN32
        if (VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::WindowsCfApi)) {
            QTest::newRow("Vfs::Mode::WindowsCfApi") << Vfs::Mode::WindowsCfApi;
        } else {
            qWarning("Skipping Vfs::Mode::WindowsCfApi");
        }
#endif
    }

    void testMovedWithError()
    {
        QFETCH(Vfs::Mode, vfsMode);
        const QString src = "folder/folderA/file.txt"_L1;
        const QString dest = "folder/folderB/file.txt"_L1;
        FakeFolder fakeFolder{ FileInfo{ QString(), { FileInfo{ QStringLiteral("folder"), { FileInfo{ QStringLiteral("folderA"), { { QStringLiteral("file.txt"), 400 } } }, QStringLiteral("folderB") } } } } };
        auto syncOpts = fakeFolder.syncEngine().syncOptions();
        syncOpts._parallelNetworkJobs = [] { return 0; };
        fakeFolder.syncEngine().setSyncOptions(syncOpts);

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        if (vfsMode != Vfs::Mode::Off) {
            auto vfs = QSharedPointer<Vfs>(VfsPluginManager::instance().createVfsFromPlugin(vfsMode).release());
            QVERIFY(vfs);
            fakeFolder.switchToVfs(vfs);
            // make files virtual
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        }


        fakeFolder.serverErrorPaths().append(src, 403);
        fakeFolder.localModifier().rename(src, dest);
        QVERIFY(fakeFolder.currentRemoteState().find(src));
        QVERIFY(!fakeFolder.currentRemoteState().find(dest));

        // sync1 file gets detected as error, instruction is still NEW_FILE
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        // sync2 file is in error state, checkErrorBlacklisting sets instruction to IGNORED
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        if (vfsMode != Vfs::Mode::Off) {
            QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
        }

        QVERIFY(!fakeFolder.currentLocalState().find(src));
        QVERIFY(fakeFolder.currentLocalState().find(dest));
        QVERIFY(fakeFolder.currentRemoteState().find(src));
        QVERIFY(!fakeFolder.currentRemoteState().find(dest));
    }

};

QTEST_GUILESS_MAIN(TestSyncMove)
#include "testsyncmove.moc"
