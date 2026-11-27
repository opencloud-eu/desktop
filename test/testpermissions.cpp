/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QtTest>
#include "testutils/syncenginetestutils.h"
#include <syncengine.h>
#include "common/ownsql.h"

using namespace OCC;
using namespace Qt::StringLiterals;

static void applyPermissionsFromName(FileInfo &info) {
    static QRegularExpression rx("_PERM_([^_]*)_[^/]*$"_L1);
    auto m = rx.match(info.name);
    if (m.hasMatch()) {
        info.permissions = RemotePermissions::fromServerString(m.captured(1));
    }

    for (FileInfo &sub : info.children)
        applyPermissionsFromName(sub);
}

// Check if the expected rows in the DB are non-empty. Note that in some cases they might be, then we cannot use this function
// https://github.com/owncloud/client/issues/2038
static void assertCsyncJournalOk(SyncJournalDb &journal)
{
    // The DB is openend in locked mode: close to allow us to access.
    journal.close();

    SqlDatabase db;
    QVERIFY(db.openReadOnly(journal.databaseFilePath()));
    SqlQuery q("SELECT count(*) from metadata where length(fileId) == 0", db);
    QVERIFY(q.exec());
    QVERIFY(q.next().hasData);
    QCOMPARE(q.intValue(0), 0);
#if defined(Q_OS_WIN) // Make sure the file does not appear in the FileInfo
    FileSystem::setFileHidden(journal.databaseFilePath() + "-shm"_L1, true);
#endif
    journal.allowReopen();
}

SyncFileItemPtr findDiscoveryItem(const SyncFileItemSet &spy, const QString &path)
{
    for (const auto &item : spy) {
        if (item->destination() == path)
            return item;
    }
    return SyncFileItemPtr(new SyncFileItem);
}

bool itemInstruction(const ItemCompletedSpy &spy, const QString &path, const SyncInstructions instr)
{
    auto item = spy.findItem(path);
    Q_ASSERT(!item.isNull());
    return item->instruction() == instr;
}

bool discoveryInstruction(const SyncFileItemSet &spy, const QString &path, const SyncInstructions instr)
{
    auto item = findDiscoveryItem(spy, path);
    return item->instruction() == instr;
}

class TestPermissions : public QObject
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

    void t7pl()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        if (vfsMode == Vfs::Mode::WindowsCfApi) {
            QSKIP("Known issue: the winvfs plug-in fails to set file permissions");
        }

        FakeFolder fakeFolder(FileInfo(), vfsMode, filesAreDehydrated);
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // Some of this test depends on the order of discovery. With threading
        // that order becomes effectively random, but we want to make sure to test
        // all cases and thus disable threading.
        auto syncOpts = fakeFolder.syncEngine().syncOptions();
        syncOpts._parallelNetworkJobs = [] { return 1; };
        fakeFolder.syncEngine().setSyncOptions(syncOpts);

        const auto cannotBeModifiedSize = 133_B;
        const auto canBeModifiedSize = 144_B;

        //create some files
        auto insertIn = [&](const QString &dir) {
            fakeFolder.remoteModifier().insert(dir + "normalFile_PERM_WVND_.data"_L1, 100_B);
            fakeFolder.remoteModifier().insert(dir + "cannotBeRemoved_PERM_WVN_.data"_L1, 101_B);
            fakeFolder.remoteModifier().insert(dir + "canBeRemoved_PERM_D_.data"_L1, 102_B);
            fakeFolder.remoteModifier().insert(dir + "cannotBeModified_PERM_DVN_.data"_L1, cannotBeModifiedSize, 'A');
            fakeFolder.remoteModifier().insert(dir + "canBeModified_PERM_W_.data"_L1, canBeModifiedSize);
        };

        //put them in some directories
        fakeFolder.remoteModifier().mkdir("normalDirectory_PERM_CKDNV_"_L1);
        insertIn("normalDirectory_PERM_CKDNV_/"_L1);
        fakeFolder.remoteModifier().mkdir("readonlyDirectory_PERM_M_"_L1);
        insertIn("readonlyDirectory_PERM_M_/"_L1);
        fakeFolder.remoteModifier().mkdir("readonlyDirectory_PERM_M_/subdir_PERM_CK_"_L1);
        fakeFolder.remoteModifier().mkdir("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_"_L1);
        fakeFolder.remoteModifier().insert("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1, 100_B);
        applyPermissionsFromName(fakeFolder.remoteModifier());

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        assertCsyncJournalOk(fakeFolder.syncJournal());
        qInfo("Do some changes and see how they propagate");

        //1. remove the file than cannot be removed
        //  (they should be recovered)
        fakeFolder.localModifier().remove("normalDirectory_PERM_CKDNV_/cannotBeRemoved_PERM_WVN_.data"_L1);
        fakeFolder.localModifier().remove("readonlyDirectory_PERM_M_/cannotBeRemoved_PERM_WVN_.data"_L1);

        //2. remove the file that can be removed
        //  (they should properly be gone)
        auto removeReadOnly = [&] (const QString &file)  {
            QVERIFY(!QFileInfo(fakeFolder.localPath() + file).permission(QFile::WriteOwner));
            QFile(fakeFolder.localPath() + file).setPermissions(QFile::WriteOwner | QFile::ReadOwner);
            fakeFolder.localModifier().remove(file);
        };
        removeReadOnly("normalDirectory_PERM_CKDNV_/canBeRemoved_PERM_D_.data"_L1);
        removeReadOnly("readonlyDirectory_PERM_M_/canBeRemoved_PERM_D_.data"_L1);

        //3. Edit the files that cannot be modified
        //  (they should be recovered, and a conflict shall be created)
        auto editReadOnly = [&] (const QString &file)  {
            QVERIFY(!QFileInfo(fakeFolder.localPath() + file).permission(QFile::WriteOwner));
            QFile(fakeFolder.localPath() + file).setPermissions(QFile::WriteOwner | QFile::ReadOwner);
            fakeFolder.localModifier().appendByte(file);
        };
        editReadOnly("normalDirectory_PERM_CKDNV_/cannotBeModified_PERM_DVN_.data"_L1);
        editReadOnly("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1);

        //4. Edit other files
        //  (they should be uploaded)
        fakeFolder.localModifier().appendByte("normalDirectory_PERM_CKDNV_/canBeModified_PERM_W_.data"_L1);
        fakeFolder.localModifier().appendByte("readonlyDirectory_PERM_M_/canBeModified_PERM_W_.data"_L1);

        //5. Create a new file in a read write folder
        // (should be uploaded)
        fakeFolder.localModifier().insert("normalDirectory_PERM_CKDNV_/newFile_PERM_WDNV_.data"_L1, 106_B);
        applyPermissionsFromName(fakeFolder.remoteModifier());

        //do the sync
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        assertCsyncJournalOk(fakeFolder.syncJournal());
        auto currentLocalState = fakeFolder.currentLocalState();

        //1.
        // File should be recovered
        QVERIFY(currentLocalState.find("normalDirectory_PERM_CKDNV_/cannotBeRemoved_PERM_WVN_.data"_L1));
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/cannotBeRemoved_PERM_WVN_.data"_L1));

        //2.
        // File should be deleted
        QVERIFY(!currentLocalState.find("normalDirectory_PERM_CKDNV_/canBeRemoved_PERM_D_.data"_L1));
        QVERIFY(!currentLocalState.find("readonlyDirectory_PERM_M_/canBeRemoved_PERM_D_.data"_L1));

        //3.
        // File should be recovered
        QCOMPARE(currentLocalState.find("normalDirectory_PERM_CKDNV_/cannotBeModified_PERM_DVN_.data"_L1)->contentSize, cannotBeModifiedSize);
        QCOMPARE(currentLocalState.find("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1)->contentSize, cannotBeModifiedSize);
        // and conflict created
        auto c1 = findConflict(currentLocalState, "normalDirectory_PERM_CKDNV_/cannotBeModified_PERM_DVN_.data"_L1);
        QVERIFY(c1);
        QCOMPARE(c1->contentSize, cannotBeModifiedSize + 1);
        auto c2 = findConflict(currentLocalState, "readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1);
        QVERIFY(c2);
        QCOMPARE(c2->contentSize, cannotBeModifiedSize + 1);
        // remove the conflicts for the next state comparison
        fakeFolder.localModifier().remove(c1->path());
        fakeFolder.localModifier().remove(c2->path());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        //4. File should be updated, that's tested by assertLocalAndRemoteDir
        QCOMPARE(currentLocalState.find("normalDirectory_PERM_CKDNV_/canBeModified_PERM_W_.data"_L1)->contentSize, canBeModifiedSize + 1);
        QCOMPARE(currentLocalState.find("readonlyDirectory_PERM_M_/canBeModified_PERM_W_.data"_L1)->contentSize, canBeModifiedSize + 1);

        //5.
        // the file should be in the server and local
        QVERIFY(currentLocalState.find("normalDirectory_PERM_CKDNV_/newFile_PERM_WDNV_.data"_L1));

        // Both side should still be the same
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // Next test

        //6. Create a new file in a read only folder
        // (they should not be uploaded)
        fakeFolder.localModifier().insert("readonlyDirectory_PERM_M_/newFile_PERM_WDNV_.data"_L1, 105_B);

        applyPermissionsFromName(fakeFolder.remoteModifier());
        // error: can't upload to readonly
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        assertCsyncJournalOk(fakeFolder.syncJournal());
        currentLocalState = fakeFolder.currentLocalState();

        //6.
        // The file should not exist on the remote, but still be there
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/newFile_PERM_WDNV_.data"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("readonlyDirectory_PERM_M_/newFile_PERM_WDNV_.data"_L1));
        // remove it so next test succeed.
        fakeFolder.localModifier().remove("readonlyDirectory_PERM_M_/newFile_PERM_WDNV_.data"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        // Both side should still be the same
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());


        //######################################################################
        qInfo( "remove the read only directory" );
        // -> It must be recovered
        fakeFolder.localModifier().remove("readonlyDirectory_PERM_M_"_L1);
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        assertCsyncJournalOk(fakeFolder.syncJournal());
        currentLocalState = fakeFolder.currentLocalState();
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/cannotBeRemoved_PERM_WVN_.data"_L1));
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_"_L1));
        // the subdirectory had delete permissions, so the contents were deleted
        QVERIFY(!currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // restore
        fakeFolder.remoteModifier().mkdir("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_"_L1);
        fakeFolder.remoteModifier().insert("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1);
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());


        //######################################################################
        qInfo( "move a directory in a outside read only folder" );

        //Missing directory should be restored
        //new directory should be uploaded
        fakeFolder.localModifier().rename("readonlyDirectory_PERM_M_/subdir_PERM_CK_"_L1, "normalDirectory_PERM_CKDNV_/subdir_PERM_CKDNV_"_L1);
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        currentLocalState = fakeFolder.currentLocalState();

        // old name restored
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_"_L1));
        // contents moved (had move permissions)
        QVERIFY(!currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_"_L1));
        QVERIFY(!currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1));

        // new still exist  (and is uploaded)
        QVERIFY(currentLocalState.find("normalDirectory_PERM_CKDNV_/subdir_PERM_CKDNV_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // restore for further tests
        fakeFolder.remoteModifier().mkdir("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_"_L1);
        fakeFolder.remoteModifier().insert("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1);
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());


        //######################################################################
        qInfo( "rename a directory in a read only folder and move a directory to a read-only" );

        // do a sync to update the database
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        assertCsyncJournalOk(fakeFolder.syncJournal());

        QVERIFY(fakeFolder.currentLocalState().find("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1));

        //1. rename a directory in a read only folder
        //Missing directory should be restored
        //new directory should stay but not be uploaded
        fakeFolder.localModifier().rename("readonlyDirectory_PERM_M_/subdir_PERM_CK_"_L1, "readonlyDirectory_PERM_M_/newname_PERM_CK_"_L1);

        //2. move a directory from read to read only  (move the directory from previous step)
        fakeFolder.localModifier().rename("normalDirectory_PERM_CKDNV_/subdir_PERM_CKDNV_"_L1, "readonlyDirectory_PERM_M_/moved_PERM_CK_"_L1);

        // error: can't upload to readonly!
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
        currentLocalState = fakeFolder.currentLocalState();

        //1.
        // old name restored
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_"_L1));
        // including contents
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/subdir_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1));
        // new still exist
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/newname_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1));
        // but is not on server: so remove it localy for the future comarison
        fakeFolder.localModifier().remove("readonlyDirectory_PERM_M_/newname_PERM_CK_"_L1);

        //2.
        // old removed
        QVERIFY(!currentLocalState.find("normalDirectory_PERM_CKDNV_/subdir_PERM_CKDNV_"_L1));
        // but still on the server: the rename causing an error meant the deletes didn't execute
        QVERIFY(fakeFolder.currentRemoteState().find("normalDirectory_PERM_CKDNV_/subdir_PERM_CKDNV_"_L1));
        // new still there
        QVERIFY(currentLocalState.find("readonlyDirectory_PERM_M_/moved_PERM_CK_/subsubdir_PERM_CKDNV_/normalFile_PERM_WVND_.data"_L1));
        //but not on server
        fakeFolder.localModifier().remove("readonlyDirectory_PERM_M_/moved_PERM_CK_"_L1);
        fakeFolder.remoteModifier().remove("normalDirectory_PERM_CKDNV_/subdir_PERM_CKDNV_"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());


        //######################################################################
        qInfo( "multiple restores of a file create different conflict files" );

        fakeFolder.remoteModifier().insert("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1);
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        editReadOnly("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1);
        fakeFolder.localModifier().setContents("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1, FileModifier::DefaultFileSize, 's');
        //do the sync
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        assertCsyncJournalOk(fakeFolder.syncJournal());

        QThread::sleep(1); // make sure changes have different mtime
        editReadOnly("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1);
        fakeFolder.localModifier().setContents("readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1, FileModifier::DefaultFileSize, 'd');

        //do the sync
        applyPermissionsFromName(fakeFolder.remoteModifier());
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        assertCsyncJournalOk(fakeFolder.syncJournal());

        // there should be two conflict files
        currentLocalState = fakeFolder.currentLocalState();
        int count = 0;
        while (auto i = findConflict(currentLocalState, "readonlyDirectory_PERM_M_/cannotBeModified_PERM_DVN_.data"_L1)) {
            QVERIFY((i->contentChar == 's') || (i->contentChar == 'd'));
            fakeFolder.localModifier().remove(i->path());
            QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
            currentLocalState = fakeFolder.currentLocalState();
            count++;
        }
        QCOMPARE(count, 2);
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    static void setAllPerm(FileInfo *fi, RemotePermissions perm)
    {
        fi->permissions = perm;
        for (auto &subFi : fi->children)
            setAllPerm(&subFi, perm);
    }

    // What happens if the source can't be moved or the target can't be created?
    void testForbiddenMoves()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        if (vfsMode == Vfs::Mode::WindowsCfApi) {
            QSKIP("Known issue: the winvfs plug-in fails to set file permissions");
        }

        FakeFolder fakeFolder(FileInfo(), vfsMode, filesAreDehydrated);

        // Some of this test depends on the order of discovery. With threading
        // that order becomes effectively random, but we want to make sure to test
        // all cases and thus disable threading.
        auto syncOpts = fakeFolder.syncEngine().syncOptions();
        syncOpts._parallelNetworkJobs = [] { return 1; };
        fakeFolder.syncEngine().setSyncOptions(syncOpts);

        auto &lm = fakeFolder.localModifier();
        auto &rm = fakeFolder.remoteModifier();
        rm.mkdir("allowed"_L1);
        rm.mkdir("norename"_L1);
        rm.mkdir("nomove"_L1);
        rm.mkdir("nocreatefile"_L1);
        rm.mkdir("nocreatedir"_L1);
        rm.mkdir("zallowed"_L1); // order of discovery matters

        rm.mkdir("allowed/sub"_L1);
        rm.mkdir("allowed/sub2"_L1);
        rm.insert("allowed/file"_L1);
        rm.insert("allowed/sub/file"_L1);
        rm.insert("allowed/sub2/file"_L1);
        rm.mkdir("norename/sub"_L1);
        rm.insert("norename/file"_L1);
        rm.insert("norename/sub/file"_L1);
        rm.mkdir("nomove/sub"_L1);
        rm.insert("nomove/file"_L1);
        rm.insert("nomove/sub/file"_L1);
        rm.mkdir("zallowed/sub"_L1);
        rm.mkdir("zallowed/sub2"_L1);
        rm.insert("zallowed/file"_L1);
        rm.insert("zallowed/sub/file"_L1);
        rm.insert("zallowed/sub2/file"_L1);

        setAllPerm(rm.find("norename"_L1), RemotePermissions::fromServerString("WDVCK"_L1));
        setAllPerm(rm.find("nomove"_L1), RemotePermissions::fromServerString("WDNCK"_L1));
        setAllPerm(rm.find("nocreatefile"_L1), RemotePermissions::fromServerString("WDNVK"_L1));
        setAllPerm(rm.find("nocreatedir"_L1), RemotePermissions::fromServerString("WDNVC"_L1));

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        // Renaming errors
        lm.rename("norename/file"_L1, "norename/file_renamed"_L1);
        lm.rename("norename/sub"_L1, "norename/sub_renamed"_L1);
        // Moving errors
        lm.rename("nomove/file"_L1, "allowed/file_moved"_L1);
        lm.rename("nomove/sub"_L1, "allowed/sub_moved"_L1);
        // Createfile errors
        lm.rename("allowed/file"_L1, "nocreatefile/file"_L1);
        lm.rename("zallowed/file"_L1, "nocreatefile/zfile"_L1);
        lm.rename("allowed/sub"_L1, "nocreatefile/sub"_L1); // TODO: probably forbidden because it contains file children?
        // Createdir errors
        lm.rename("allowed/sub2"_L1, "nocreatedir/sub2"_L1);
        lm.rename("zallowed/sub2"_L1, "nocreatedir/zsub2"_L1);

        // also hook into discovery!!
        SyncFileItemSet discovery;
        connect(&fakeFolder.syncEngine(), &SyncEngine::aboutToPropagate, this, [&discovery](auto v) { discovery = v; });
        ItemCompletedSpy completeSpy(fakeFolder);
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        // if renaming doesn't work, just delete+create
        QVERIFY(itemInstruction(completeSpy, "norename/file"_L1, CSYNC_INSTRUCTION_REMOVE));
        QVERIFY(itemInstruction(completeSpy, "norename/sub"_L1, CSYNC_INSTRUCTION_REMOVE));
        QVERIFY(discoveryInstruction(discovery, "norename/sub"_L1, CSYNC_INSTRUCTION_REMOVE));
        QVERIFY(itemInstruction(completeSpy, "norename/file_renamed"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "norename/sub_renamed"_L1, CSYNC_INSTRUCTION_NEW));
        // the contents can _move_
        QVERIFY(itemInstruction(completeSpy, "norename/sub_renamed/file"_L1, CSYNC_INSTRUCTION_RENAME));

        // simiilarly forbidding moves becomes delete+create
        QVERIFY(itemInstruction(completeSpy, "nomove/file"_L1, CSYNC_INSTRUCTION_REMOVE));
        QVERIFY(itemInstruction(completeSpy, "nomove/sub"_L1, CSYNC_INSTRUCTION_REMOVE));
        QVERIFY(discoveryInstruction(discovery, "nomove/sub"_L1, CSYNC_INSTRUCTION_REMOVE));
        // nomove/sub/file is removed as part of the dir
        QVERIFY(itemInstruction(completeSpy, "allowed/file_moved"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "allowed/sub_moved"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "allowed/sub_moved/file"_L1, CSYNC_INSTRUCTION_NEW));

        // when moving to an invalid target, the targets should be an error
        QVERIFY(itemInstruction(completeSpy, "nocreatefile/file"_L1, CSYNC_INSTRUCTION_ERROR));
        QVERIFY(itemInstruction(completeSpy, "nocreatefile/zfile"_L1, CSYNC_INSTRUCTION_ERROR));
        QVERIFY(itemInstruction(completeSpy, "nocreatefile/sub"_L1, CSYNC_INSTRUCTION_RENAME)); // TODO: What does a real server say?
        QVERIFY(itemInstruction(completeSpy, "nocreatedir/sub2"_L1, CSYNC_INSTRUCTION_ERROR));
        QVERIFY(itemInstruction(completeSpy, "nocreatedir/zsub2"_L1, CSYNC_INSTRUCTION_ERROR));

        // and the sources of the invalid moves should be restored, not deleted
        // (depending on the order of discovery a follow-up sync is needed)
        QVERIFY(completeSpy.findItem("allowed/file"_L1).isNull());
        QVERIFY(completeSpy.findItem("allowed/sub2"_L1).isNull());
        QVERIFY(itemInstruction(completeSpy, "zallowed/file"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "zallowed/sub2"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "zallowed/sub2/file"_L1, CSYNC_INSTRUCTION_NEW));
        QCOMPARE(fakeFolder.syncEngine().isAnotherSyncNeeded(), true);

        // A follow-up sync will restore allowed/file and allowed/sub2 and maintain the nocreatedir/file errors
        completeSpy.clear();
        QCOMPARE(fakeFolder.syncJournal().wipeErrorBlacklist(), 4);
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        QVERIFY(itemInstruction(completeSpy, "nocreatefile/file"_L1, CSYNC_INSTRUCTION_ERROR));
        QVERIFY(itemInstruction(completeSpy, "nocreatefile/zfile"_L1, CSYNC_INSTRUCTION_ERROR));
        QVERIFY(itemInstruction(completeSpy, "nocreatedir/sub2"_L1, CSYNC_INSTRUCTION_ERROR));
        QVERIFY(itemInstruction(completeSpy, "nocreatedir/zsub2"_L1, CSYNC_INSTRUCTION_ERROR));

        QVERIFY(itemInstruction(completeSpy, "allowed/file"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "allowed/sub2"_L1, CSYNC_INSTRUCTION_NEW));
        QVERIFY(itemInstruction(completeSpy, "allowed/sub2/file"_L1, CSYNC_INSTRUCTION_NEW));

        auto cls = fakeFolder.currentLocalState();
        QVERIFY(cls.find("allowed/file"_L1));
        QVERIFY(cls.find("allowed/sub2"_L1));
        QVERIFY(cls.find("zallowed/file"_L1));
        QVERIFY(cls.find("zallowed/sub2"_L1));
        QVERIFY(cls.find("zallowed/sub2/file"_L1));
    }

    // Test for issue #7293
    void testAllowedMoveForbiddenDelete() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        if (vfsMode == Vfs::Mode::WindowsCfApi) {
            QSKIP("Known issue: the winvfs plug-in fails to set file permissions");
        }

        FakeFolder fakeFolder(FileInfo(), vfsMode, filesAreDehydrated);

        // Some of this test depends on the order of discovery. With threading
        // that order becomes effectively random, but we want to make sure to test
        // all cases and thus disable threading.
        auto syncOpts = fakeFolder.syncEngine().syncOptions();
        syncOpts._parallelNetworkJobs = [] { return 1; };
        fakeFolder.syncEngine().setSyncOptions(syncOpts);

        auto &lm = fakeFolder.localModifier();
        auto &rm = fakeFolder.remoteModifier();
        rm.mkdir("changeonly"_L1);
        rm.mkdir("changeonly/sub1"_L1);
        rm.insert("changeonly/sub1/file1"_L1);
        rm.insert("changeonly/sub1/filetorname1a"_L1);
        rm.insert("changeonly/sub1/filetorname1z"_L1);
        rm.mkdir("changeonly/sub2"_L1);
        rm.insert("changeonly/sub2/file2"_L1);
        rm.insert("changeonly/sub2/filetorname2a"_L1);
        rm.insert("changeonly/sub2/filetorname2z"_L1);

        setAllPerm(rm.find("changeonly"_L1), RemotePermissions::fromServerString("NSV"_L1));

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        lm.rename("changeonly/sub1/filetorname1a"_L1, "changeonly/sub1/aaa1_renamed"_L1);
        lm.rename("changeonly/sub1/filetorname1z"_L1, "changeonly/sub1/zzz1_renamed"_L1);

        lm.rename("changeonly/sub2/filetorname2a"_L1, "changeonly/sub2/aaa2_renamed"_L1);
        lm.rename("changeonly/sub2/filetorname2z"_L1, "changeonly/sub2/zzz2_renamed"_L1);

        lm.rename("changeonly/sub1"_L1, "changeonly/aaa"_L1);
        lm.rename("changeonly/sub2"_L1, "changeonly/zzz"_L1);


        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }
};

QTEST_GUILESS_MAIN(TestPermissions)
#include "testpermissions.moc"
