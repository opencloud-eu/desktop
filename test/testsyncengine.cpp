/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <syncengine.h>

#include "libsync/filesystem.h"
#include "testutils/syncenginetestutils.h"
#include "testutils/testutils.h"

#include <QtTest>

using namespace Qt::Literals::StringLiterals;
using namespace std::chrono_literals;
using namespace OCC::FileSystem::SizeLiterals;
using namespace OCC;

bool itemDidComplete(const ItemCompletedSpy &spy, const QString &path)
{
    if (auto item = spy.findItem(path)) {
        return item->instruction() & ~(CSYNC_INSTRUCTION_NONE | CSYNC_INSTRUCTION_UPDATE_METADATA);
    }
    return false;
}

bool itemInstruction(const ItemCompletedSpy &spy, const QString &path, const SyncInstructions instr)
{
    auto item = spy.findItem(path);
    return item->instruction() == instr;
}

bool itemDidCompleteSuccessfully(const ItemCompletedSpy &spy, const QString &path)
{
    if (auto item = spy.findItem(path)) {
        return item->_status == SyncFileItem::Success;
    }
    return false;
}

class TestSyncEngine : public QObject
{
    Q_OBJECT
public:
    enum class LocalAction { None, ReplaceKeepMtime, ReplaceNewMtime, ReplaceWithContent, OverwriteInPlace };
    Q_ENUM(LocalAction)

private Q_SLOTS:
    void initTestCase_data()
    {
        QTest::addColumn<Vfs::Mode>("vfsMode");
        QTest::addColumn<bool>("filesAreDehydrated");

        QTest::newRow("Vfs::Mode::Off") << Vfs::Mode::Off << false;

        if (VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::OpenVFS)) {
            QTest::newRow("Vfs::Mode::OpenVFS dehydrated") << Vfs::Mode::OpenVFS << true;
            QTest::newRow("Vfs::Mode::OpenVFS hydrated") << Vfs::Mode::OpenVFS << false;
        }

        if (VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::WindowsCfApi)) {
            QTest::newRow("Vfs::Mode::WindowsCfApi dehydrated") << Vfs::Mode::WindowsCfApi << true;
            QTest::newRow("Vfs::Mode::WindowsCfApi hydrated") << Vfs::Mode::WindowsCfApi << false;
        } else if (Utility::isWindows()) {
            qWarning("Skipping Vfs::Mode::WindowsCfApi");
        }
    }

    void testFileDownload() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.remoteModifier().insert("A/a0"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "A/a0"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void testFileUpload() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.localModifier().insert("A/a0"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "A/a0"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void testDirDownload() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.remoteModifier().mkdir("Y"_L1);
        fakeFolder.remoteModifier().mkdir("Z"_L1);
        fakeFolder.remoteModifier().insert("Z/d0"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "Y"_L1));
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "Z"_L1));
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "Z/d0"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void testDirUpload() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.localModifier().mkdir("Y"_L1);
        fakeFolder.localModifier().mkdir("Z"_L1);
        fakeFolder.localModifier().insert("Z/d0"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "Y"_L1));
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "Z"_L1));
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "Z/d0"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void testLocalDelete() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.remoteModifier().remove("A/a1"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "A/a1"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void testRemoteDelete() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.localModifier().remove("A/a1"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "A/a1"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void testEmlLocalChecksum() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo {}, vfsMode, filesAreDehydrated);
        fakeFolder.account()->setCapabilities({fakeFolder.account()->url(), TestUtils::testCapabilities(CheckSums::Algorithm::SHA1)});
        fakeFolder.localModifier().insert("a1.eml"_L1, 64_B, 'A');
        fakeFolder.localModifier().insert("a2.eml"_L1, 64_B, 'A');
        fakeFolder.localModifier().insert("a3.eml"_L1, 64_B, 'A');
        fakeFolder.localModifier().insert("b3.txt"_L1, 64_B, 'A');
        // Upload and calculate the checksums
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        auto getDbChecksum = [&](const QString &path) { return fakeFolder.syncJournal().getFileRecord(path).checksumHeader(); };

        // printf 'A%.0s' {1..64} | sha1sum -
        QByteArray referenceChecksum("SHA1:30b86e44e6001403827a62c58b08893e77cf121f");
        QCOMPARE(getDbChecksum("a1.eml"_L1), referenceChecksum);
        QCOMPARE(getDbChecksum("a2.eml"_L1), referenceChecksum);
        QCOMPARE(getDbChecksum("a3.eml"_L1), referenceChecksum);
        QCOMPARE(getDbChecksum("b3.txt"_L1), referenceChecksum);

        // Make sure that the lastModified time caused by the setContent calls below is actually different:
        QThread::sleep(1);

        ItemCompletedSpy completeSpy(fakeFolder);
        // Touch the file without changing the content, shouldn't upload
        fakeFolder.localModifier().setContents("a1.eml"_L1, 64_B, 'A');
        // Change the content/size
        fakeFolder.localModifier().setContents("a2.eml"_L1, 64_B, 'B');
        fakeFolder.localModifier().appendByte("a3.eml"_L1, 'X');
        fakeFolder.localModifier().appendByte("b3.txt"_L1, 'X');
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(getDbChecksum("a1.eml"_L1), referenceChecksum);
        QCOMPARE(getDbChecksum("a2.eml"_L1), QByteArray("SHA1:84951fc23a4dafd10020ac349da1f5530fa65949"));
        QCOMPARE(getDbChecksum("a3.eml"_L1), QByteArray("SHA1:c119308d57884896cd86a7050e449aaba24b1fee"));
        QCOMPARE(getDbChecksum("b3.eml"_L1), getDbChecksum("a3.txt"_L1));

        QVERIFY(!itemDidComplete(completeSpy, "a1.eml"_L1));
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "a2.eml"_L1));
        QVERIFY(itemDidCompleteSuccessfully(completeSpy, "a3.eml"_L1));

        // The local and remote state now differ: the local mtime for `a1.eml` is bigger (newer) than on the server, because
        // the upload was skipped (same checksum). So first verify that this is the case:
        QVERIFY(fakeFolder.currentLocalState().find("a1.eml"_L1)->lastModified() > fakeFolder.currentRemoteState().find("a1.eml"_L1)->lastModified());
        // And then check if everything else actually is the same:
        QVERIFY(fakeFolder.currentLocalState().equals(fakeFolder.currentRemoteState(), FileInfo::IgnoreLastModified));
    }

    void testSelectiveSyncBug() {
        // issue owncloud/enterprise#1965: files from selective-sync ignored
        // folders are uploaded anyway is some circumstances.
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        if (vfsMode != Vfs::Mode::Off) {
            QSKIP("selective sync is not supported with virtual files");
        }

        // clang-format off
        FakeFolder fakeFolder(
            FileInfo {QString(), {
                FileInfo {"parentFolder"_L1, {
                    FileInfo {"subFolderA"_L1, {
                        {"fileA.txt"_L1, 400},
                        {"fileB.txt"_L1, 400, 'o'},
                        FileInfo {"subsubFolder"_L1, {
                            {"fileC.txt"_L1, 400},
                            {"fileD.txt"_L1, 400, 'o'}
                        }},
                        FileInfo {"anotherFolder"_L1, {
                            FileInfo {"emptyFolder"_L1, {}},
                            FileInfo {"subsubFolder"_L1, {
                                {"fileE.txt"_L1, 400},
                                {"fileF.txt"_L1, 400, 'o'}
                            }}
                        }}
                    }},
                    FileInfo{"subFolderB"_L1, {}}
                }}
            }},
            vfsMode, filesAreDehydrated);
        // clang-format on

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        auto expectedServerState = fakeFolder.currentRemoteState();

        // Remove subFolderA with selectiveSync:
        fakeFolder.syncEngine().journal()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, {"parentFolder/subFolderA/"_L1});
        fakeFolder.syncEngine().journal()->schedulePathForRemoteDiscovery(QByteArrayLiteral("parentFolder/subFolderA/"));
        auto getEtag = [&](const QAnyStringView &file) { return fakeFolder.syncJournal().getFileRecord(file.toString()).etag(); };
        QCOMPARE(getEtag("parentFolder"), u"_invalid_"_s);
        QCOMPARE(getEtag("parentFolder/subFolderA"), u"_invalid_"_s);
        QCOMPARE_NE(getEtag("parentFolder/subFolderA/subsubFolder"), u"_invalid_"_s);

        // But touch local file before the next sync, such that the local folder
        // can't be removed
        fakeFolder.localModifier().setContents("parentFolder/subFolderA/fileB.txt"_L1, FileModifier::DefaultFileSize, 'n');
        fakeFolder.localModifier().setContents("parentFolder/subFolderA/subsubFolder/fileD.txt"_L1, FileModifier::DefaultFileSize, 'n');
        fakeFolder.localModifier().setContents("parentFolder/subFolderA/anotherFolder/subsubFolder/fileF.txt"_L1, FileModifier::DefaultFileSize, 'n');

        // Several follow-up syncs don't change the state at all,
        // in particular the remote state doesn't change and fileB.txt
        // isn't uploaded.

        for (int i = 0; i < 3; ++i) {
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());

            {
                // Nothing changed on the server
                QCOMPARE(fakeFolder.currentRemoteState(), expectedServerState);
                // The local state should still have subFolderA
                auto local = fakeFolder.currentLocalState();
                QVERIFY(local.find("parentFolder/subFolderA"_L1));
                QVERIFY(!local.find("parentFolder/subFolderA/fileA.txt"_L1));
                QVERIFY(local.find("parentFolder/subFolderA/fileB.txt"_L1));
                QVERIFY(!local.find("parentFolder/subFolderA/subsubFolder/fileC.txt"_L1));
                QVERIFY(local.find("parentFolder/subFolderA/subsubFolder/fileD.txt"_L1));
                QVERIFY(!local.find("parentFolder/subFolderA/anotherFolder/subsubFolder/fileE.txt"_L1));
                QVERIFY(local.find("parentFolder/subFolderA/anotherFolder/subsubFolder/fileF.txt"_L1));
                QVERIFY(!local.find("parentFolder/subFolderA/anotherFolder/emptyFolder"_L1));
                QVERIFY(local.find("parentFolder/subFolderB"_L1));
            }
        }
    }

    void abortAfterFailedMkdir() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo {}, vfsMode, filesAreDehydrated);
        QSignalSpy finishedSpy(&fakeFolder.syncEngine(), &SyncEngine::finished);
        fakeFolder.serverErrorPaths().append("NewFolder"_L1);
        fakeFolder.localModifier().mkdir("NewFolder"_L1);
        // This should be aborted and would otherwise fail in FileInfo::create.
        fakeFolder.localModifier().insert("NewFolder/NewFile"_L1, FileModifier::DefaultFileSize, 'n');

        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(finishedSpy.size(), 1);
        QCOMPARE(finishedSpy.first().first().toBool(), false);
    }

    /**
     * Verify that an incompletely propagated directory doesn't have the server's
     * etag stored in the database yet.
     */
    void testDirEtagAfterIncompleteSync()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo {}, vfsMode, filesAreDehydrated);
        QSignalSpy finishedSpy(&fakeFolder.syncEngine(), &SyncEngine::finished);
        fakeFolder.serverErrorPaths().append("NewFolder/foo"_L1);
        fakeFolder.remoteModifier().mkdir("NewFolder"_L1);
        fakeFolder.remoteModifier().insert("NewFolder/foo"_L1);
        bool syncSuccess = fakeFolder.applyLocalModificationsAndSync();
        if (filesAreDehydrated) {
            QVERIFY(syncSuccess);
        } else {
            QVERIFY(!syncSuccess);
        }

        SyncJournalFileRecord rec = fakeFolder.syncJournal().getFileRecord(u"NewFolder"_s);
        QVERIFY(rec.isValid());
        if (filesAreDehydrated) {
            // No error, failure occurs only with a GET, so etag should be valid (i.e. NOT invalid):
            QCOMPARE_NE(rec.etag(), u"_invalid_"_s);
        } else {
            // Download failed, check for invalid etag:
            QCOMPARE(rec.etag(), u"_invalid_"_s);
        }
        QVERIFY(!rec.fileId().isEmpty());
    }

    void testDirDownloadWithError() {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        if (filesAreDehydrated) {
            QSKIP("Nothing gets downloaded, so no error, so skip this test");
        }

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.remoteModifier().mkdir("Y"_L1);
        fakeFolder.remoteModifier().mkdir("Y/Z"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d0"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d1"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d2"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d3"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d4"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d5"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d6"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d7"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d8"_L1);
        fakeFolder.remoteModifier().insert("Y/Z/d9"_L1);
        fakeFolder.serverErrorPaths().append("Y/Z/d2"_L1, 503);
        fakeFolder.serverErrorPaths().append("Y/Z/d3"_L1, 503);
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
        QCoreApplication::processEvents(); // should not crash

        QSet<QString> seen;
        for(const QList<QVariant> &args : completeSpy) {
            auto item = args[0].value<SyncFileItemPtr>();
            qDebug() << item->localName() << item->isDirectory() << item->_status;
            QVERIFY(!seen.contains(item->localName())); // signal only sent once per item
            seen.insert(item->localName());
            if (item->localName() == QLatin1String("Y/Z/d2")) {
                QVERIFY(item->_status == SyncFileItem::NormalError);
            } else if (item->localName() == QLatin1String("Y/Z/d3")) {
                QVERIFY(item->_status != SyncFileItem::Success);
            } else if (!item->isDirectory()) {
                QVERIFY(item->_status == SyncFileItem::Success);
            }
        }
    }

    void testFakeConflict_data()
    {
        QTest::addColumn<bool>("sameMtime");
        QTest::addColumn<QByteArray>("checksums");
        QTest::addColumn<int>("expectedGEThydrated");
        QTest::addColumn<int>("expectedGETdehydrated");

        // if the files are dehydrated the count is always 1 bigger as we have to download them

        QTest::newRow("Same mtime, weak server checksum differ -> downloaded") //
            << true //
            << QByteArray("Adler32:bad") //
            << 1 // hydrated
            << 2; // dehydrated;

        QTest::newRow("Same mtime, matching weak checksum -> skipped")
            << true << QByteArray("Adler32:2a2010d")
            << 0 // hydrated
            << 1; // dehydrated;

        QTest::newRow("Same mtime, strong server checksum differ -> downloaded") //
            << true //
            << QByteArray("SHA1:bad") << 1 // hydrated
            << 2; // dehydrated;

        QTest::newRow("Same mtime, matching strong checksum -> skipped")
            << true << QByteArray("SHA1:56900fb1d337cf7237ff766276b9c1e8ce507427")
            << 0 // hydrated
            << 1; // dehydrated;

        QTest::newRow("mtime changed, weak checksum match -> download anyway") //
            << false //
            << QByteArray("Adler32:2a2010d") //
            << 1 // hydrated
            << 2; // dehydrated;

        QTest::newRow("mtime changed, strong checksum match -> skip")
            << false << QByteArray("SHA1:56900fb1d337cf7237ff766276b9c1e8ce507427")
            << 0 // hydrated
            << 1; // dehydrated;
    }

    void testFakeConflict()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);
        QFETCH(bool, sameMtime);
        QFETCH(QByteArray, checksums);
        QFETCH(int, expectedGEThydrated);
        QFETCH(int, expectedGETdehydrated);

        int expectedGET = filesAreDehydrated ? expectedGETdehydrated : expectedGEThydrated;

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        OperationCounter counter(fakeFolder);

        // For directly editing the remote checksum
        FileInfo &remoteInfo = fakeFolder.remoteModifier();

        // Base mtime with no ms content (filesystem is seconds only)
        auto mtime = QDateTime::currentDateTimeUtc().addDays(-4);
        mtime.setMSecsSinceEpoch(mtime.toMSecsSinceEpoch() / 1000 * 1000);

        const auto a1size = fakeFolder.currentRemoteState().find("A/a1"_L1)->contentSize;

        // In the dehydrated case, executing this `setContents` will cause a hydration of the file, so there will always be 1 GET request.
        fakeFolder.localModifier().setContents("A/a1"_L1, a1size, 'C');
        // nothing happend yet
        QCOMPARE(counter.nGET, 0);

        fakeFolder.localModifier().setModTime("A/a1"_L1, mtime);
        fakeFolder.remoteModifier().setContents("A/a1"_L1, a1size, 'C');
        if (!sameMtime) {
            mtime = mtime.addDays(1);
        }
        fakeFolder.remoteModifier().setModTime("A/a1"_L1, mtime);
        remoteInfo.find("A/a1"_L1)->checksums = checksums;
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(counter.nGET, expectedGET);

        // check that mtime in journal and filesystem agree
        QString a1path = fakeFolder.localPath() + "A/a1"_L1;
        SyncJournalFileRecord a1record = fakeFolder.syncJournal().getFileRecord(u"A/a1"_s);
        QCOMPARE(a1record.modtime(), FileSystem::getModTime(a1path));

        // Extra sync reads from db, no difference
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(counter.nGET, expectedGET);
    }

    /**
     * Checks whether SyncFileItems have the expected properties before start
     * of propagation.
     */
    void testSyncFileItemProperties()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        if (filesAreDehydrated) {
            QSKIP("This test expects files to exist in the sync folder before a sync.");
        }

        auto initialMtime = QDateTime::currentDateTimeUtc().addDays(-7);
        auto changedMtime = QDateTime::currentDateTimeUtc().addDays(-4);
        auto changedMtime2 = QDateTime::currentDateTimeUtc().addDays(-3);

        // Base mtime with no ms content (filesystem is seconds only)
        initialMtime.setMSecsSinceEpoch(initialMtime.toMSecsSinceEpoch() / 1000 * 1000);
        changedMtime.setMSecsSinceEpoch(changedMtime.toMSecsSinceEpoch() / 1000 * 1000);
        changedMtime2.setMSecsSinceEpoch(changedMtime2.toMSecsSinceEpoch() / 1000 * 1000);

        // Ensure the initial mtimes are as expected
        auto initialFileInfo = FileInfo::A12_B12_C12_S12();
        initialFileInfo.setModTime("A/a1"_L1, initialMtime);
        initialFileInfo.setModTime("B/b1"_L1, initialMtime);
        initialFileInfo.setModTime("C/c1"_L1, initialMtime);

        FakeFolder fakeFolder(initialFileInfo, vfsMode, filesAreDehydrated);

        // upload a
        fakeFolder.localModifier().appendByte("A/a1"_L1);
        fakeFolder.localModifier().setModTime("A/a1"_L1, changedMtime);
        // download b
        fakeFolder.remoteModifier().appendByte("B/b1"_L1);
        fakeFolder.remoteModifier().setModTime("B/b1"_L1, changedMtime);
        // conflict c
        fakeFolder.localModifier().appendByte("C/c1"_L1);
        fakeFolder.localModifier().appendByte("C/c1"_L1);
        fakeFolder.localModifier().setModTime("C/c1"_L1, changedMtime);
        fakeFolder.remoteModifier().appendByte("C/c1"_L1);
        fakeFolder.remoteModifier().setModTime("C/c1"_L1, changedMtime2);

        connect(&fakeFolder.syncEngine(), &SyncEngine::aboutToPropagate, this, [&](const SyncFileItemSet &items) {
            SyncFileItemPtr a1, b1, c1;
            for (auto &item : items) {
                if (item->localName() == QLatin1String("A/a1"))
                    a1 = item;
                if (item->localName() == QLatin1String("B/b1"))
                    b1 = item;
                if (item->localName() == QLatin1String("C/c1"))
                    c1 = item;
            }

            // a1: should have local size and modtime
            QVERIFY(a1);
            QCOMPARE(a1->instruction(), CSYNC_INSTRUCTION_SYNC);
            QCOMPARE(a1->_direction, SyncFileItem::Up);
            QCOMPARE(a1->_size, qint64(5));

            QCOMPARE(Utility::qDateTimeFromTime_t(a1->_modtime), changedMtime);
            QCOMPARE(a1->_previousSize, qint64(4));
            QCOMPARE(Utility::qDateTimeFromTime_t(a1->_previousModtime), initialMtime);

            // b2: should have remote size and modtime
            QVERIFY(b1);
            QCOMPARE(b1->instruction(), CSYNC_INSTRUCTION_SYNC);
            QCOMPARE(b1->_direction, SyncFileItem::Down);
            QCOMPARE(b1->_size, qint64(17));
            QCOMPARE(Utility::qDateTimeFromTime_t(b1->_modtime), changedMtime);
            QCOMPARE(b1->_previousSize, qint64(16));
            QCOMPARE(Utility::qDateTimeFromTime_t(b1->_previousModtime), initialMtime);

            // c1: conflicts are downloads, so remote size and modtime
            QVERIFY(c1);
            QCOMPARE(c1->instruction(), CSYNC_INSTRUCTION_CONFLICT);
            QCOMPARE(c1->_direction, SyncFileItem::None);
            QCOMPARE(c1->_size, qint64(25));
            QCOMPARE(Utility::qDateTimeFromTime_t(c1->_modtime), changedMtime2);
            QCOMPARE(c1->_previousSize, qint64(26));
            QCOMPARE(Utility::qDateTimeFromTime_t(c1->_previousModtime), changedMtime);
        });

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
    }

    /**
     * Checks whether subsequent large uploads are skipped after a 507 error
     */
    void testInsufficientRemoteStorage()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        // Disable parallel uploads
        SyncOptions syncOptions = fakeFolder.syncEngine().syncOptions();
        syncOptions._parallelNetworkJobs = [] { return 0; };
        fakeFolder.syncEngine().setSyncOptions(syncOptions);

        // Produce an error based on upload size
        int remoteQuota = 1000;
        int n507 = 0, nPUT = 0;
        QObject parent;
        fakeFolder.setServerOverride([&](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *) -> QNetworkReply * {
            if (op == QNetworkAccessManager::PutOperation) {
                nPUT++;
                if (request.rawHeader("OC-Total-Length").toInt() > remoteQuota) {
                    n507++;
                    return new FakeErrorReply(op, request, &parent, 507);
                }
            }
            return nullptr;
        });

        fakeFolder.localModifier().insert("A/big"_L1, 800_B);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(nPUT, 1);
        QCOMPARE(n507, 0);

        nPUT = 0;
        fakeFolder.localModifier().insert("A/big1"_L1, 500_B); // ok
        fakeFolder.localModifier().insert("A/big2"_L1, 1200_B); // 507 (quota guess now 1199)
        fakeFolder.localModifier().insert("A/big3"_L1, 1200_B); // skipped
        fakeFolder.localModifier().insert("A/big4"_L1, 1500_B); // skipped
        fakeFolder.localModifier().insert("A/big5"_L1, 1100_B); // 507 (quota guess now 1099)
        fakeFolder.localModifier().insert("A/big6"_L1, 900_B); // ok (quota guess now 199)
        fakeFolder.localModifier().insert("A/big7"_L1, 200_B); // skipped
        fakeFolder.localModifier().insert("A/big8"_L1, 199_B); // ok (quota guess now 0)

        fakeFolder.localModifier().insert("B/big8"_L1, 1150_B); // 507
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(nPUT, 6);
        QCOMPARE(n507, 3);
    }

    // Checks whether downloads with bad checksums are accepted
    void testChecksumValidation()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        QObject parent;

        QByteArray checksumValue;

        fakeFolder.setServerOverride([&](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *) -> QNetworkReply * {
            if (op == QNetworkAccessManager::GetOperation) {
                auto reply = new FakeGetReply(fakeFolder.remoteModifier(), op, request, &parent);
                if (!checksumValue.isNull())
                    reply->setRawHeader("OC-Checksum", checksumValue);
                return reply;
            }
            return nullptr;
        });

        // Basic case
        fakeFolder.remoteModifier().create("A/a3"_L1, 16, 'A');
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // Bad OC-Checksum
        checksumValue = "SHA1:bad";
        fakeFolder.remoteModifier().create("A/a4"_L1, 16, 'A');
        bool syncSucceeded = fakeFolder.applyLocalModificationsAndSync();
        if (filesAreDehydrated) {
            // In the dehydrated case, files are never downloaded, so checksums are not computed. Only placeholders are created.
            QVERIFY(syncSucceeded);
        } else {
            // Any case where files are actually downloaded (no vfs, local files are hydrated, etc), the checksum calculation should fail.
            QVERIFY(!syncSucceeded);
        }

        // Good OC-Checksum
        checksumValue = "SHA1:19b1928d58a2030d08023f3d7054516dbc186f20"; // printf 'A%.0s' {1..16} | sha1sum -
        fakeFolder.syncJournal().wipeErrorBlacklist();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        checksumValue = QByteArray();

        // OC-Checksum contains multiple checksums, one is valid
        fakeFolder.syncJournal().wipeErrorBlacklist();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // OC-Checksum contains Unsupported checksums
        checksumValue = "Unsupported:XXXX SHA1:invalid Invalid:XxX";
        fakeFolder.remoteModifier().create("A/a8"_L1, 16, 'A');
        syncSucceeded = fakeFolder.applyLocalModificationsAndSync();
        if (filesAreDehydrated) {
            // In the dehydrated case, files are never downloaded, so checksums are not computed. Only placeholders are created.
            QVERIFY(syncSucceeded);
        } else {
            // Since the supported SHA1 checksum is invalid, no download
            QVERIFY(!syncSucceeded);
        }
        checksumValue =  "Unsupported:XXXX SHA1:19b1928d58a2030d08023f3d7054516dbc186f20 Invalid:XxX";
        fakeFolder.syncJournal().wipeErrorBlacklist();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync()); // The supported SHA1 checksum is valid now, so the file are downloaded
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    // Tests the behavior of invalid filename detection
    void testInvalidFilenameRegex()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        if (!Utility::isWindows()) { // We can't have local file with these character
            fakeFolder.localModifier().insert("A/\\:?*\"<>|.txt"_L1);
            QVERIFY(fakeFolder.applyLocalModificationsAndSync());
            QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        }

        // We can override that by setting the capability
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // Check that new servers also accept the capability
        auto invalidFilenameRegexCapabilities = [](const QString &regex) {
            auto cap = TestUtils::testCapabilities();
            auto dav = cap["dav"_L1].toMap();
            dav.insert({{"invalidFilenameRegex"_L1, regex}});
            cap["dav"_L1] = dav;
            return cap;
        };
        fakeFolder.syncEngine().account()->setCapabilities({fakeFolder.account()->url(), invalidFilenameRegexCapabilities("my[fgh]ile"_L1)});
        fakeFolder.localModifier().insert("C/myfile.txt"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(!fakeFolder.currentRemoteState().find("C/myfile.txt"_L1));
    }

    void testDiscoveryHiddenFile()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // We can't depend on currentLocalState for hidden files since
        // it should rightfully skip things like download temporaries
        auto localFileExists = [&](const QString &name) {
            return QFileInfo::exists(fakeFolder.localPath() + name);
        };

        fakeFolder.syncEngine().setIgnoreHiddenFiles(true);
        fakeFolder.remoteModifier().insert("A/.hidden"_L1);
        fakeFolder.localModifier().insert("B/.hidden"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(!localFileExists("A/.hidden"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("B/.hidden"_L1));

        fakeFolder.syncEngine().setIgnoreHiddenFiles(false);
        fakeFolder.syncJournal().forceRemoteDiscoveryNextSync();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(localFileExists("A/.hidden"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("B/.hidden"_L1));
    }

    // The sync root is listed on every run, but a folder whose etag did not change is not, so a
    // remote hidden file inside it only shows up once the etags are invalidated. A full local
    // discovery, which is what "Force sync now" asks for, is not enough (#1127).
    void testRemoteHiddenFileInFolderNeedsRemoteDiscovery()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        auto localFileExists = [&](const QString &name) {
            return QFileInfo::exists(fakeFolder.localPath() + name);
        };

        fakeFolder.syncEngine().setIgnoreHiddenFiles(true);
        fakeFolder.remoteModifier().insert(".hidden"_L1);
        fakeFolder.remoteModifier().insert("A/.hidden"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(!localFileExists(".hidden"_L1));
        QVERIFY(!localFileExists("A/.hidden"_L1));

        fakeFolder.syncEngine().setIgnoreHiddenFiles(false);
        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::FilesystemOnly);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(localFileExists(".hidden"_L1));
        QVERIFY(!localFileExists("A/.hidden"_L1));

        fakeFolder.syncJournal().forceRemoteDiscoveryNextSync();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(localFileExists("A/.hidden"_L1));
    }

    void testRenameExcludedFile()
    {
        if (!VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::OpenVFS)) {
            QSKIP("Pin states require the OpenVFS plugin");
        }

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), Vfs::Mode::OpenVFS, false);
        fakeFolder.syncEngine().setIgnoreHiddenFiles(true);

        fakeFolder.localModifier().rename("A/a1"_L1, "A/.a1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(!fakeFolder.currentRemoteState().find("A/a1"_L1));
        const auto excludedPin = fakeFolder.vfs()->pinState("A/.a1"_L1);
        QVERIFY(excludedPin);
        QCOMPARE(*excludedPin, PinState::Excluded);

        fakeFolder.localModifier().rename("A/.a1"_L1, "A/a1renamed"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(fakeFolder.currentRemoteState().find("A/a1renamed"_L1));
        const auto pin = fakeFolder.vfs()->pinState("A/a1renamed"_L1);
        QVERIFY(pin);
        QVERIFY(*pin != PinState::Excluded);
    }

#ifndef Q_OS_WIN
    void testPropagatePermissions()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        auto perm = QFileDevice::Permission(0x7704); // user/owner: rwx, group: r, other: -
        QFile::setPermissions(fakeFolder.localPath() + "A/a1"_L1, perm);
        QFile::setPermissions(fakeFolder.localPath() + "A/a2"_L1, perm);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync()); // get the metadata-only change out of the way
        fakeFolder.remoteModifier().appendByte("A/a1"_L1);
        fakeFolder.remoteModifier().appendByte("A/a2"_L1);
        fakeFolder.localModifier().appendByte("A/a2"_L1);
        fakeFolder.localModifier().appendByte("A/a2"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync()); // perms should be preserved
        QCOMPARE(QFileInfo(fakeFolder.localPath() + "A/a1"_L1).permissions(), perm);
        QCOMPARE(QFileInfo(fakeFolder.localPath() + "A/a2"_L1).permissions(), perm);

        const QString conflictName = QString::fromUtf8(fakeFolder.syncJournal().conflictRecord(fakeFolder.syncJournal().conflictRecordPaths().first()).path);
        QVERIFY(conflictName.contains("A/a2"_L1));
        QCOMPARE(QFileInfo(fakeFolder.localPath() + conflictName).permissions(), perm);
    }
#endif

    void testEmptyLocalButHasRemote()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo {}, vfsMode, filesAreDehydrated);
        fakeFolder.remoteModifier().mkdir("foo"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        QVERIFY(fakeFolder.currentLocalState().find("foo"_L1));
    }

    // Check that server mtime is set on directories on initial propagation
    void testDirectoryInitialMtime()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo {}, vfsMode, filesAreDehydrated);
        fakeFolder.remoteModifier().mkdir("foo"_L1);
        fakeFolder.remoteModifier().insert("foo/bar"_L1);
        auto datetime = QDateTime::currentDateTime();
        datetime.setSecsSinceEpoch(datetime.toSecsSinceEpoch()); // wipe ms
        fakeFolder.remoteModifier().find("foo"_L1)->setLastModified(datetime);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        QCOMPARE(QFileInfo(fakeFolder.localPath() + "foo"_L1).lastModified(), datetime);
    }


    void testProceedWithIndependentDelets()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // during an upload of B/newFile an error occurs that aborts all delete jobs
        fakeFolder.localModifier().insert("B/newFile"_L1);
        fakeFolder.setServerOverride([&](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *) -> QNetworkReply * {
            if (op == QNetworkAccessManager::PutOperation) {
                return new FakeErrorReply(op, request, this, 507);
            }
            return nullptr;
        });

        // this will go through
        fakeFolder.remoteModifier().insert("C/newRemoteFile"_L1);

        // those will fail https://github.com/owncloud/client/issues/9311
        fakeFolder.remoteModifier().remove("A"_L1);
        fakeFolder.remoteModifier().remove("S"_L1);

        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        // the download succeeded
        QVERIFY(fakeFolder.currentLocalState().find("C/newRemoteFile"_L1));

        // the deletes happened as they are independent of the error in B
        QVERIFY(!fakeFolder.currentLocalState().find("A"_L1));
        QVERIFY(!fakeFolder.currentLocalState().find("S"_L1));
    }

    void testEmptyFileChangedOnServer_data()
    {
        QTest::addColumn<LocalAction>("localAction");
        QTest::addColumn<bool>("syncAfterLocalAction");
        QTest::addColumn<QString>("fileName");

        for (const auto &fileName : {u"empty.xlsx"_s, u"A/empty.xlsx"_s}) {
            for (const auto action : {LocalAction::None, LocalAction::ReplaceKeepMtime, LocalAction::ReplaceNewMtime, LocalAction::ReplaceWithContent,
                     LocalAction::OverwriteInPlace}) {
                for (const bool syncAfterLocalAction : {true, false}) {
                    QTest::addRow("%s %s %s", qPrintable(fileName), qPrintable(Utility::enumToString(action)), syncAfterLocalAction ? "synced" : "not synced")
                        << action << syncAfterLocalAction << fileName;
                }
            }
        }
    }

    void testEmptyFileChangedOnServer()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);
        QFETCH(LocalAction, localAction);
        QFETCH(bool, syncAfterLocalAction);
        QFETCH(QString, fileName);

        FakeFolder fakeFolder(FileInfo{}, vfsMode, filesAreDehydrated);
        const bool virtualFiles = vfsMode != Vfs::Mode::Off && filesAreDehydrated;
        const QString localFile = fakeFolder.localPath() + fileName;
        const auto serverContent = QByteArray(100, FileInfo::DefaultContentChar);
        const auto readLocalFile = [&localFile] {
            QFile f(localFile);
            return f.open(QFile::ReadOnly) ? f.readAll() : QByteArray();
        };

        if (fileName.contains(u'/')) {
            fakeFolder.remoteModifier().mkdir(QFileInfo(fileName).path());
        }
        fakeFolder.remoteModifier().insert(fileName, 0_B);
        QVERIFY(fakeFolder.syncOnce());
        QCOMPARE(QFileInfo(localFile).size(), 0);
        QCOMPARE(fakeFolder.isDehydratedPlaceholder(localFile), virtualFiles);

        const auto mtime = QDateTime::fromSecsSinceEpoch(FileSystem::getModTime(localFile));
        auto &localModifier = fakeFolder.localModifier();
        switch (localAction) {
        case LocalAction::None:
            break;
        case LocalAction::ReplaceKeepMtime:
        case LocalAction::ReplaceNewMtime:
        case LocalAction::ReplaceWithContent:
            localModifier.remove(fileName);
            localModifier.insert(fileName, localAction == LocalAction::ReplaceWithContent ? 10_B : 0_B, 'L');
            localModifier.setModTime(fileName, localAction == LocalAction::ReplaceKeepMtime ? mtime : mtime.addSecs(60));
            break;
        case LocalAction::OverwriteInPlace:
            localModifier.setContents(fileName, 10_B, 'L');
            localModifier.setModTime(fileName, mtime.addSecs(60));
            break;
        }
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        if (syncAfterLocalAction) {
            QVERIFY(fakeFolder.syncOnce());
        }

        fakeFolder.remoteModifier().setContents(fileName, 100_B);
        const bool conflict = !syncAfterLocalAction && localAction != LocalAction::None && localAction != LocalAction::ReplaceKeepMtime;
        {
            ItemCompletedSpy completeSpy(fakeFolder);
            QVERIFY(fakeFolder.syncOnce());
            const auto item = completeSpy.findItem(fileName);
            QVERIFY(item);
            QVERIFY2(item->_status == (conflict ? SyncFileItem::Conflict : SyncFileItem::Success),
                qPrintable(u"%1 %2"_s.arg(Utility::enumToString(item->_status), item->_errorString)));
        }

        QCOMPARE(QFileInfo(localFile).size(), 100);
        if (!virtualFiles) {
            QVERIFY(!fakeFolder.isDehydratedPlaceholder(localFile));
        } else if (localAction == LocalAction::None) {
            QVERIFY(fakeFolder.isDehydratedPlaceholder(localFile));
        }
        if (!fakeFolder.isDehydratedPlaceholder(localFile)) {
            QCOMPARE(readLocalFile(), serverContent);
        }
        if (conflict) {
            auto localState = fakeFolder.currentLocalState();
            const auto *conflictFile = findConflict(localState, fileName);
            QVERIFY(conflictFile);
            QCOMPARE(conflictFile->contentSize, localAction == LocalAction::ReplaceNewMtime ? 0_B : 10_B);
        }
        QVERIFY(fakeFolder.syncOnce());

        if (vfsMode != Vfs::Mode::Off) {
            QVERIFY(fakeFolder.vfs()->setPinState(fileName, PinState::AlwaysLocal));
            QVERIFY(fakeFolder.syncOnce());
            QVERIFY(!fakeFolder.isDehydratedPlaceholder(localFile));
            QCOMPARE(readLocalFile(), serverContent);
        }
    }
};

QTEST_GUILESS_MAIN(TestSyncEngine)
#include "testsyncengine.moc"
