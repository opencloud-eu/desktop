/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <qglobal.h>
#include <QTemporaryDir>
#include <QtTest>

#include "common/utility.h"
#include "folderman.h"
#include "account.h"
#include "accountstate.h"
#include "configfile.h"

#include "testutils/testutils.h"

#ifndef Q_OS_WIN
#include <unistd.h>
#else
#include "common/utility_win.h"
#endif


using namespace Qt::Literals::StringLiterals;
using namespace OCC;


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
        QVERIFY(dir2.mkpath(QStringLiteral("sub/OpenCloud1/folder/f")));
        QVERIFY(dir2.mkpath(QStringLiteral("OpenCloud2")));
        QVERIFY(dir2.mkpath(QStringLiteral("sub/free")));
        QVERIFY(dir2.mkpath(QStringLiteral("free2/sub")));
        {
            QFile f(dir.path() + QStringLiteral("/sub/file.txt"));
            f.open(QFile::WriteOnly);
            f.write("hello");
        }
        QString dirPath = dir2.canonicalPath();

        auto newAccountState = TestUtils::createDummyAccount();
        FolderMan *folderman = TestUtils::folderMan();
        QCOMPARE(folderman, FolderMan::instance());

        const auto type = FolderMan::NewFolderType::SpacesFolder;
        const QUuid uuid = {};

        if (Utility::isWindows()) { // drive-letter tests
            if (QFileInfo(QStringLiteral("c:/")).isWritable()) {
                // we expect success
                QCOMPARE(folderman->checkPathValidityForNewFolder(QStringLiteral("c:"), type, uuid), QString());
                QCOMPARE(folderman->checkPathValidityForNewFolder(QStringLiteral("c:/"), type, uuid), QString());
                QCOMPARE(folderman->checkPathValidityForNewFolder(QStringLiteral("c:/foo"), type, uuid), QString());
            }
        }

        QVERIFY(folderman->addFolder(
            newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + QStringLiteral("/sub/OpenCloud1"))));
        QVERIFY(folderman->addFolder(
            newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + QStringLiteral("/OpenCloud2"))));


        // those should be allowed
        // QString FolderMan::checkPathValidityForNewFolder(const QString& path, const QUrl &serverUrl, bool forNewDirectory)

        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/free"), type, uuid), QString());
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/free2/"), type, uuid), QString());
        // Not an existing directory -> Ok
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/bliblablu"), type, uuid), QString());
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/free/bliblablu"), type, uuid), QString());
        // QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/bliblablu/some/more")), QString());

        // A file -> Error
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/file.txt"), type, uuid), QString());

        // The following both fail because they refer to the same account
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/OpenCloud1"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/OpenCloud2/"), type, uuid), QString());

        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath, type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/OpenCloud1/folder"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/OpenCloud1/folder/f"), type, uuid), QString());

#ifndef Q_OS_WIN // no links on windows, no permissions
        // make a bunch of links
        QVERIFY(QFile::link(dirPath + QStringLiteral("/sub/free"), dirPath + QStringLiteral("/link1")));
        QVERIFY(QFile::link(dirPath + QStringLiteral("/sub"), dirPath + QStringLiteral("/link2")));
        QVERIFY(QFile::link(dirPath + QStringLiteral("/sub/OpenCloud1"), dirPath + QStringLiteral("/link3")));
        QVERIFY(QFile::link(dirPath + QStringLiteral("/sub/OpenCloud1/folder"), dirPath + QStringLiteral("/link4")));

        // Ok
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link1"), type, uuid), QString());
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link2/free"), type, uuid), QString());

        // Not Ok
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link2"), type, uuid), QString());

        // link 3 points to an existing sync folder. To make it fail, the account must be the same
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link3"), type, uuid), QString());

        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link4"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link3/folder"), type, uuid), QString());

        // test some non existing sub path (error)
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/OpenCloud1/some/sub/path"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/OpenCloud2/blublu"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/sub/OpenCloud1/folder/g/h"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link3/folder/neu_folder"), type, uuid), QString());

        // Subfolder of links
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link1/subfolder"), type, uuid), QString());
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/link2/free/subfolder"), type, uuid), QString());

        if (getuid() != 0) {
            // Should not have the rights
            QCOMPARE_NE(folderman->checkPathValidityForNewFolder(QStringLiteral("/"), type, uuid), QString());
            QCOMPARE_NE(folderman->checkPathValidityForNewFolder(QStringLiteral("/usr/bin/somefolder"), type, uuid), QString());
        }
#endif

        // Invalid paths
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder({}, type, uuid), QString());


        // REMOVE OpenCloud2 from the filesystem, but keep a folder sync'ed to it.
        QDir(dirPath + QStringLiteral("/OpenCloud2/")).removeRecursively();
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/OpenCloud2/blublu"), type, uuid), QString());
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/OpenCloud2/sub/subsub/sub"), type, uuid), QString());

        { // check for rejection of a directory with `.sync_*.db`
            QVERIFY(dir2.mkpath(QStringLiteral("db-check1")));
            QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/db-check1"), type, uuid), QString());
            QFile f(dirPath + QStringLiteral("/db-check1/.sync_something.db"));
            QVERIFY(f.open(QFile::Truncate | QFile::WriteOnly));
            f.close();
            QVERIFY(QFileInfo::exists(dirPath + QStringLiteral("/db-check1/.sync_something.db")));
            QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/db-check1"), type, uuid), QString());
        }

        { // check for rejection of a directory with `._sync_*.db`
            QVERIFY(dir2.mkpath(QStringLiteral("db-check2")));
            QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/db-check2"), type, uuid), QString());
            QFile f(dirPath + QStringLiteral("/db-check2/._sync_something.db"));
            QVERIFY(f.open(QFile::Truncate | QFile::WriteOnly));
            f.close();
            QVERIFY(QFileInfo::exists(dirPath + QStringLiteral("/db-check2/._sync_something.db")));
            QCOMPARE_NE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/db-check2"), type, uuid), QString());
        }


        if (Utility::isWindows()) { // drive-letter tests
            const auto driveLetter = QFileInfo(dirPath).absoluteDir().absolutePath().at(0);
            const auto drive = u"%1:/"_s.arg(driveLetter);
            if (QFileInfo(drive).isWritable()) {
                // fails as we already sync dirPath + QStringLiteral("/sub/OpenCloud1")
                QCOMPARE_NE(folderman->checkPathValidityForNewFolder(u"%1:"_s.arg(driveLetter), type, uuid), QString());
                QCOMPARE_NE(folderman->checkPathValidityForNewFolder(u"%1:/"_s.arg(driveLetter), type, uuid), QString());
                // succeeds as the sub dir foo does not contain OpenCloud1
                QCOMPARE(folderman->checkPathValidityForNewFolder(u"%1:/foo"_s.arg(driveLetter), type, uuid), QString());
            }
        }
    }

    void testFindGoodPathForNewSyncFolder()
    {
        // SETUP

        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());
        QVERIFY(dir2.mkpath(QStringLiteral("sub/OpenCloud1/folder/f")));
        QVERIFY(dir2.mkpath(QStringLiteral("OpenCloud")));
        QVERIFY(dir2.mkpath(QStringLiteral("OpenCloud2")));
        QVERIFY(dir2.mkpath(QStringLiteral("OpenCloud2/foo")));
        QVERIFY(dir2.mkpath(QStringLiteral("sub/free")));
        QVERIFY(dir2.mkpath(QStringLiteral("free2/sub")));
        QString dirPath = dir2.canonicalPath();

        auto newAccountState = TestUtils::createDummyAccount();

        FolderMan *folderman = TestUtils::folderMan();
        QVERIFY(folderman->addFolder(
            newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + QStringLiteral("/sub/OpenCloud/"))));
        QVERIFY(folderman->addFolder(
            newAccountState.get(), TestUtils::createDummyFolderDefinition(newAccountState->account(), dirPath + QStringLiteral("/OpenCloud (2)/"))));

        // TEST
        const auto folderType = FolderMan::NewFolderType::SpacesFolder;
        const auto uuid = QUuid::createUuid();

        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("oc"), folderType, uuid), dirPath + QStringLiteral("/oc"));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud"), folderType, uuid), dirPath + QStringLiteral("/OpenCloud (3)"));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud2"), folderType, uuid), dirPath + QStringLiteral("/OpenCloud2 (2)"));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud (2)"), folderType, uuid),
            dirPath + QStringLiteral("/OpenCloud (2) (2)"));
        QCOMPARE(
            folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud2/foo"), folderType, uuid), dirPath + QStringLiteral("/OpenCloud2_foo"));
        QCOMPARE(
            folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud2/bar"), folderType, uuid), dirPath + QStringLiteral("/OpenCloud2_bar"));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("sub"), folderType, uuid), dirPath + QStringLiteral("/sub (2)"));

        // REMOVE OpenCloud2 from the filesystem, but keep a folder sync'ed to it.
        // We should still not suggest this folder as a new folder.
        QDir(dirPath + QStringLiteral("/OpenCloud (2)/")).removeRecursively();
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud"), folderType, uuid), dirPath + QStringLiteral("/OpenCloud (3)"));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud2"), folderType, uuid),
            QString(dirPath + QStringLiteral("/OpenCloud2 (2)")));
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("OpenCloud (2)"), folderType, uuid),
            QString(dirPath + QStringLiteral("/OpenCloud (2) (2)")));

        // make sure people can't do evil stuff
        QCOMPARE(
            folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("../../../Bo/b"), folderType, uuid), QString(dirPath + QStringLiteral("/___Bo_b")));

        // normalise the name
        QCOMPARE(folderman->findGoodPathForNewSyncFolder(dirPath, QStringLiteral("            Bo:*<>!b          "), folderType, uuid),
            QString(dirPath + QStringLiteral("/Bo____!b")));
    }

    void testSpacesSyncRootAndFolderCreation()
    {
        auto dir = TestUtils::createTempDir();
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());

        // Create a sync root for another account
        QVERIFY(dir2.mkpath(QStringLiteral("AnotherSpacesSyncRoot")));
        const auto anotherUuid = QUuid::createUuid();
        Utility::markDirectoryAsSyncRoot(dir2.filePath(QStringLiteral("AnotherSpacesSyncRoot")), anotherUuid);

        FolderMan *folderman = TestUtils::folderMan();
        QCOMPARE(folderman, FolderMan::instance());

        QString dirPath = dir2.canonicalPath();
        const auto ourUuid = QUuid::createUuid();

        // Spaces Sync Root in another Spaces Sync Root should fail
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(
                        dirPath + QStringLiteral("/AnotherSpacesSyncRoot/OurSpacesSyncRoot"), FolderMan::NewFolderType::SpacesSyncRoot, ourUuid),
            QString());
        // Spaces Sync Root one level up should be fine
        QCOMPARE(folderman->checkPathValidityForNewFolder(dirPath + QStringLiteral("/OurSpacesSyncRoot"), FolderMan::NewFolderType::SpacesSyncRoot, ourUuid),
            QString());

        // Create the sync root so we can test Spaces Folder creation below
        QVERIFY(dir2.mkpath(QStringLiteral("OurSpacesSyncRoot")));
        Utility::markDirectoryAsSyncRoot(dir2.filePath(QStringLiteral("OurSpacesSyncRoot")), ourUuid);

        // A folder for a Space in a sync root for another account should fail
        QCOMPARE_NE(folderman->checkPathValidityForNewFolder(
                        dirPath + QStringLiteral("/AnotherSpacesSyncRoot/OurSpacesFolder"), FolderMan::NewFolderType::SpacesFolder, ourUuid),
            QString());
        // But in our sync root that should just be fine
        QCOMPARE(folderman->checkPathValidityForNewFolder(
                     dirPath + QStringLiteral("/OurSpacesSyncRoot/OurSpacesFolder"), FolderMan::NewFolderType::SpacesFolder, ourUuid),
            QString());
    }
};

QTEST_GUILESS_MAIN(TestFolderMan)
#include "testfolderman.moc"
