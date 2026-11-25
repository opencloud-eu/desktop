/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <qglobal.h>
#include <QTemporaryDir>
#include <QtTest>

#include "account.h"
#include "accountstate.h"
#include "common/syncjournaldb.h"
#include "common/syncjournalfilerecord.h"
#include "common/utility.h"
#include "configfile.h"
#include "filesystem.h"
#include "folder.h"
#include "folderman.h"
#include "syncfileitem.h"

#include "testutils/testutils.h"

#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include "common/utility_win.h"
#endif

using namespace OCC;
using namespace Qt::StringLiterals;


class TestFolderMan: public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testCheckPathValidityForNewFolder()
    {
#ifdef Q_OS_WIN
        QNtfsPermissionCheckGuard ntfs_perm;
#endif
        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());
        QVERIFY(dir2.mkpath("sub/OpenCloud1/folder/f"_L1));
        QVERIFY(dir2.mkpath("OpenCloud2"_L1));
        QVERIFY(dir2.mkpath("sub/free"_L1));
        QVERIFY(dir2.mkpath("free2/sub"_L1));
        {
            QFile f(dir.path() + "/sub/file.txt"_L1);
            f.open(QFile::WriteOnly);
            f.write("hello");
        }
        QString dirPath = dir2.canonicalPath();

        auto newAccountState = TestUtils::createDummyAccount();
        FolderMan *folderman = TestUtils::folderMan();
        QCOMPARE(folderman, FolderMan::instance());
        QVERIFY(
            folderman->addFolder(newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + "/sub/OpenCloud1"_L1)));
        QVERIFY(folderman->addFolder(newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + "/OpenCloud2"_L1)));

        const auto type = FolderMan::NewFolderType::SpacesFolder;
        const QUuid uuid = {};

        // those should be allowed
        // QString FolderMan::checkPathValidityForNewFolder(const QString& path, const QUrl &serverUrl, bool forNewDirectory)

        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + "/sub/free"_L1, type, uuid), QString());
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + "/free2/"_L1, type, uuid), QString());
        // Not an existing directory -> Ok
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + "/sub/bliblablu"_L1, type, uuid), QString());
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + "/sub/free/bliblablu"_L1, type, uuid), QString());
        // QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + "/sub/bliblablu/some/more"_L1), QString());

        // A file -> Error
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/sub/file.txt"_L1, type, uuid).isNull());

        // The following both fail because they refer to the same account
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/sub/OpenCloud1"_L1, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/OpenCloud2/"_L1, type, uuid).isNull());

        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/sub/OpenCloud1/folder"_L1, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/sub/OpenCloud1/folder/f"_L1, type, uuid).isNull());

#ifndef Q_OS_WIN // no links on windows, no permissions
        // make a bunch of links
        QVERIFY(QFile::link(dirPath + "/sub/free"_L1, dirPath + "/link1"_L1));
        QVERIFY(QFile::link(dirPath + "/sub"_L1, dirPath + "/link2"_L1));
        QVERIFY(QFile::link(dirPath + "/sub/OpenCloud1"_L1, dirPath + "/link3"_L1));
        QVERIFY(QFile::link(dirPath + "/sub/OpenCloud1/folder"_L1, dirPath + "/link4"_L1));

        // Ok
        QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/link1"_L1, type, uuid).isNull());
        QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/link2/free"_L1, type, uuid).isNull());

        // Not Ok
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/link2"_L1, type, uuid).isNull());

        // link 3 points to an existing sync folder. To make it fail, the account must be the same
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/link3"_L1, type, uuid).isNull());

        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/link4"_L1, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/link3/folder"_L1, type, uuid).isNull());

        // test some non existing sub path (error)
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/sub/OpenCloud1/some/sub/path"_L1, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/OpenCloud2/blublu"_L1, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/sub/OpenCloud1/folder/g/h"_L1, type, uuid).isNull());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + "/link3/folder/neu_folder"_L1, type, uuid), QString());

        // Subfolder of links
        QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/link1/subfolder"_L1, type, uuid).isNull());
        QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/link2/free/subfolder"_L1, type, uuid).isNull());

        if (getuid() != 0) {
            // Should not have the rights
            QVERIFY(!folderman->checkPathValidityForNewFolder("/"_L1, type, uuid).isNull());
            QVERIFY(!folderman->checkPathValidityForNewFolder("/usr/bin/somefolder"_L1, type, uuid).isNull());
        }
#endif

        if (Utility::isWindows()) { // drive-letter tests
            if (!QFileInfo("v:/"_L1).exists()) {
                QVERIFY(!folderman->checkPathValidityForNewFolder("v:"_L1, type, uuid).isNull());
                QVERIFY(!folderman->checkPathValidityForNewFolder("v:/"_L1, type, uuid).isNull());
                QVERIFY(!folderman->checkPathValidityForNewFolder("v:/foo"_L1, type, uuid).isNull());
            }
            if (QFileInfo("c:/"_L1).isWritable()) {
                QVERIFY(folderman->checkPathValidityForNewFolder("c:"_L1, type, uuid).isNull());
                QVERIFY(folderman->checkPathValidityForNewFolder("c:/"_L1, type, uuid).isNull());
                QVERIFY(folderman->checkPathValidityForNewFolder("c:/foo"_L1, type, uuid).isNull());
            }
        }

        // Invalid paths
        QVERIFY(!folderman->checkPathValidityForNewFolder({}, type, uuid).isNull());


        // REMOVE OpenCloud2 from the filesystem, but keep a folder sync'ed to it.
        QDir(dirPath + "/OpenCloud2/"_L1).removeRecursively();
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/OpenCloud2/blublu"_L1, type, uuid).isNull());
        QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/OpenCloud2/sub/subsub/sub"_L1, type, uuid).isNull());

        { // check for rejection of a directory with `.sync_*.db`
            QVERIFY(dir2.mkpath("db-check1"_L1));
            QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/db-check1"_L1, type, uuid).isNull());
            QFile f(dirPath + "/db-check1/.sync_something.db"_L1);
            QVERIFY(f.open(QFile::Truncate | QFile::WriteOnly));
            f.close();
            QVERIFY(QFileInfo::exists(dirPath + "/db-check1/.sync_something.db"_L1));
            QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/db-check1"_L1, type, uuid).isNull());
        }

        { // check for rejection of a directory with `._sync_*.db`
            QVERIFY(dir2.mkpath("db-check2"_L1));
            QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/db-check2"_L1, type, uuid).isNull());
            QFile f(dirPath + "/db-check2/._sync_something.db"_L1);
            QVERIFY(f.open(QFile::Truncate | QFile::WriteOnly));
            f.close();
            QVERIFY(QFileInfo::exists(dirPath + "/db-check2/._sync_something.db"_L1));
            QVERIFY(!folderman->checkPathValidityForNewFolder(dirPath + "/db-check2"_L1, type, uuid).isNull());
        }
    }

    void testFindGoodPathForNewSyncFolder()
    {
        // SETUP

        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());
        QVERIFY(dir2.mkpath("sub/OpenCloud1/folder/f"_L1));
        QVERIFY(dir2.mkpath("OpenCloud"_L1));
        QVERIFY(dir2.mkpath("OpenCloud2"_L1));
        QVERIFY(dir2.mkpath("OpenCloud2/foo"_L1));
        QVERIFY(dir2.mkpath("sub/free"_L1));
        QVERIFY(dir2.mkpath("free2/sub"_L1));
        QString dirPath = dir2.canonicalPath();

        auto newAccountState = TestUtils::createDummyAccount();

        FolderMan *folderman = TestUtils::folderMan();
        QVERIFY(
            folderman->addFolder(newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + "/sub/OpenCloud/"_L1)));
        QVERIFY(
            folderman->addFolder(newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + "/OpenCloud (2)/"_L1)));

        // TEST
        const auto folderType = FolderMan::NewFolderType::SpacesFolder;
        const auto uuid = QUuid::createUuid();

        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "oc"_L1, folderType, uuid), dirPath + "/oc"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud"_L1, folderType, uuid), dirPath + "/OpenCloud (3)"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud2"_L1, folderType, uuid), dirPath + "/OpenCloud2 (2)"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud (2)"_L1, folderType, uuid), dirPath + "/OpenCloud (2) (2)"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud2/foo"_L1, folderType, uuid), dirPath + "/OpenCloud2_foo"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud2/bar"_L1, folderType, uuid), dirPath + "/OpenCloud2_bar"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "sub"_L1, folderType, uuid), dirPath + "/sub (2)"_L1);

        // REMOVE OpenCloud2 from the filesystem, but keep a folder sync'ed to it.
        // We should still not suggest this folder as a new folder.
        QDir(dirPath + "/OpenCloud (2)/"_L1).removeRecursively();
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud"_L1, folderType, uuid), dirPath + "/OpenCloud (3)"_L1);
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud2"_L1, folderType, uuid), QString(dirPath + "/OpenCloud2 (2)"_L1));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "OpenCloud (2)"_L1, folderType, uuid), QString(dirPath + "/OpenCloud (2) (2)"_L1));

        // make sure people can't do evil stuff
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "../../../Bo/b"_L1, folderType, uuid), QString(dirPath + "/___Bo_b"_L1));

        // normalise the name
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, "            Bo:*<>!b          "_L1, folderType, uuid), QString(dirPath + "/Bo____!b"_L1));
    }

    // Enabling "sync hidden files" has to invalidate the state that was built up while the hidden
    // items were ignored, otherwise the ones inside already synced folders stay missing (#714, #1127).
    void testSetIgnoreHiddenFiles()
    {
        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());
        QVERIFY(dir2.mkpath("OpenCloud"_L1));
        const QString folderPath = dir2.canonicalPath() + "/OpenCloud"_L1;

        auto accountState = TestUtils::createDummyAccount();
        FolderMan *folderman = TestUtils::folderMan();
        auto *folder = folderman->addFolder(accountState.get(), TestUtils::createDummyFolderDefinition(accountState->account(), folderPath));
        QVERIFY(folder);

        // hidden files are ignored by default
        QVERIFY(folderman->ignoreHiddenFiles());
        QVERIFY(folder->ignoreHiddenFiles());

        // a directory that was already discovered, its etag has to be invalidated so the hidden
        // items on the server are looked at again
        auto item = TestUtils::dummyItem("adir"_L1);
        item._type = ItemTypeDirectory;
        item._etag = "etag"_L1;
        item._remotePerm = RemotePermissions::fromDbValue("RW");
        QVERIFY(folder->journalDb()->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(item)));

        folderman->setIgnoreHiddenFiles(false);

        QVERIFY(!folder->ignoreHiddenFiles());
        // and the remote has to be discovered again
        QCOMPARE(folder->journalDb()->getFileRecord("adir"_L1).etag(), "_invalid_"_L1);

        // turning it off again
        folderman->setIgnoreHiddenFiles(true);
        QVERIFY(folder->ignoreHiddenFiles());
    }

    void testSpuriousChangeResetsQueuedState()
    {
        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());
        QVERIFY(dir2.mkpath("OpenCloud"_L1));
        const QString folderPath = dir2.canonicalPath() + "/OpenCloud"_L1;

        auto accountState = TestUtils::createDummyAccount();
        FolderMan *folderman = TestUtils::folderMan();
        auto *folder = folderman->addFolder(accountState.get(), TestUtils::createDummyFolderDefinition(accountState->account(), folderPath));
        QVERIFY(folder);
        QTRY_VERIFY(folder->isReady());

        const QString filePath = folder->path() + "file.txt"_L1;
        QVERIFY(TestUtils::writeRandomFile(filePath, 100));

        auto item = TestUtils::dummyItem("file.txt"_L1);
        item._size = FileSystem::getSize(FileSystem::toFilesystemPath(filePath));
        item._modtime = FileSystem::getModTime(filePath);
        item._inode = FileSystem::getInode(filePath).value();
        QVERIFY(folder->journalDb()->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(item)));

        folder->setSyncState(SyncResult::Queued);

        folder->slotWatchedPathsChanged({filePath}, Folder::ChangeReason::Other);
        QCOMPARE(folder->syncState(), SyncResult::Success);
    }

    void testSpacesSyncRootAndFolderCreation()
    {
        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());

        // Create a sync root for another account
        QVERIFY(dir2.mkpath("AnotherSpacesSyncRoot"_L1));
        const auto anotherUuid = QUuid::createUuid();
        Utility::markDirectoryAsSyncRoot(dir2.filePath("AnotherSpacesSyncRoot"_L1), anotherUuid);

        FolderMan *folderman = TestUtils::folderMan();
        QCOMPARE(folderman, FolderMan::instance());

        QString dirPath = dir2.canonicalPath();
        const auto ourUuid = QUuid::createUuid();

        // Spaces Sync Root in another Spaces Sync Root should fail
        QVERIFY(!folderman
                ->checkPathValidityForNewFolder(dirPath + "/AnotherSpacesSyncRoot/OurSpacesSyncRoot"_L1, FolderMan::NewFolderType::SpacesSyncRoot, ourUuid)
                .isNull());
        // Spaces Sync Root one level up should be fine
        QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/OurSpacesSyncRoot"_L1, FolderMan::NewFolderType::SpacesSyncRoot, ourUuid).isNull());

        // Create the sync root so we can test Spaces Folder creation below
        QVERIFY(dir2.mkpath("OurSpacesSyncRoot"_L1));
        Utility::markDirectoryAsSyncRoot(dir2.filePath("OurSpacesSyncRoot"_L1), ourUuid);

        // A folder for a Space in a sync root for another account should fail
        QVERIFY(
            !folderman->checkPathValidityForNewFolder(dirPath + "/AnotherSpacesSyncRoot/OurSpacesFolder"_L1, FolderMan::NewFolderType::SpacesFolder, ourUuid)
                .isNull());
        // But in our sync root that should just be fine
        QVERIFY(folderman->checkPathValidityForNewFolder(dirPath + "/OurSpacesSyncRoot/OurSpacesFolder"_L1, FolderMan::NewFolderType::SpacesFolder, ourUuid)
                .isNull());
    }
};

QTEST_GUILESS_MAIN(TestFolderMan)
#include "testfolderman.moc"
