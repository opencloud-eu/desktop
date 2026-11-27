/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include "testutils/syncenginetestutils.h"
#include "testutils/testutils.h"

#include <syncengine.h>
#include <localdiscoverytracker.h>

#include <QtTest>

using namespace OCC;
using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

class TestLocalDiscovery : public QObject
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

    /// Check correct behavior when local discovery is partially drawn from the db
    void testLocalDiscoveryStyle()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        LocalDiscoveryTracker tracker;
        connect(&fakeFolder.syncEngine(), &SyncEngine::itemCompleted, &tracker, &LocalDiscoveryTracker::slotItemCompleted);
        connect(&fakeFolder.syncEngine(), &SyncEngine::finished, &tracker, &LocalDiscoveryTracker::slotSyncFinished);

        // More subdirectories are useful for testing
        fakeFolder.localModifier().mkdir("A/X"_L1);
        fakeFolder.localModifier().mkdir("A/Y"_L1);
        fakeFolder.localModifier().insert("A/X/x1"_L1);
        fakeFolder.localModifier().insert("A/Y/y1"_L1);
        tracker.addTouchedPath("A/X"_L1);

        tracker.startSyncFullDiscovery();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QVERIFY(tracker.localDiscoveryPaths().empty());

        // Test begins
        fakeFolder.localModifier().insert("A/a3"_L1);
        fakeFolder.localModifier().insert("A/X/x2"_L1);
        fakeFolder.localModifier().insert("A/Y/y2"_L1);
        fakeFolder.localModifier().insert("B/b3"_L1);
        fakeFolder.remoteModifier().insert("C/c3"_L1);
        fakeFolder.remoteModifier().appendByte("C/c1"_L1);
        tracker.addTouchedPath("A/X"_L1);

        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, tracker.localDiscoveryPaths());
        tracker.startSyncPartialDiscovery();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QVERIFY(fakeFolder.currentRemoteState().find("A/a3"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("A/X/x2"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("A/Y/y2"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("B/b3"_L1));
        QVERIFY(fakeFolder.currentLocalState().find("C/c3"_L1));
        QCOMPARE(fakeFolder.syncEngine().lastLocalDiscoveryStyle(), LocalDiscoveryStyle::DatabaseAndFilesystem);
        QVERIFY(tracker.localDiscoveryPaths().empty());

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QCOMPARE(fakeFolder.syncEngine().lastLocalDiscoveryStyle(), LocalDiscoveryStyle::FilesystemOnly);
        QVERIFY(tracker.localDiscoveryPaths().empty());
    }

    void testLocalDiscoveryDecision()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        auto &engine = fakeFolder.syncEngine();

        QVERIFY(engine.shouldDiscoverLocally(QString()));
        QVERIFY(engine.shouldDiscoverLocally("A"_L1));
        QVERIFY(engine.shouldDiscoverLocally("A/X"_L1));

        fakeFolder.syncEngine().setLocalDiscoveryOptions(
            LocalDiscoveryStyle::DatabaseAndFilesystem, {"A/X"_L1, "A/X space"_L1, "A/X/beta"_L1, "foo bar space/touch"_L1, "foo/"_L1, "zzz"_L1, "zzzz"_L1});

        QVERIFY(engine.shouldDiscoverLocally(QString()));
        QVERIFY(engine.shouldDiscoverLocally("A"_L1));
        QVERIFY(engine.shouldDiscoverLocally("A/X"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("B"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("A B"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("B/X"_L1));
        QVERIFY(engine.shouldDiscoverLocally("foo bar space"_L1));
        QVERIFY(engine.shouldDiscoverLocally("foo"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("foo bar"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("foo bar/touch"_L1));
        // These are within "A/X"_L1 so they should be discovered
        QVERIFY(engine.shouldDiscoverLocally("A/X/alpha"_L1));
        QVERIFY(engine.shouldDiscoverLocally("A/X beta"_L1));
        QVERIFY(engine.shouldDiscoverLocally("A/X/Y"_L1));
        QVERIFY(engine.shouldDiscoverLocally("A/X space"_L1));
        QVERIFY(engine.shouldDiscoverLocally("A/X space/alpha"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("A/Xylo/foo"_L1));
        QVERIFY(engine.shouldDiscoverLocally("zzzz/hello"_L1));
        QVERIFY(!engine.shouldDiscoverLocally("zzza/hello"_L1));

        QEXPECT_FAIL("", "There is a possibility of false positives if the set contains a path "
            "which is a prefix, and that prefix is followed by a character less than '/'", Continue);
        QVERIFY(!engine.shouldDiscoverLocally("A/X o"_L1));

        fakeFolder.syncEngine().setLocalDiscoveryOptions(
            LocalDiscoveryStyle::DatabaseAndFilesystem,
            {});

        QVERIFY(!engine.shouldDiscoverLocally(QString()));
    }

    // Check whether item success and item failure adjusts the
    // tracker correctly.
    void testTrackerItemCompletion()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        LocalDiscoveryTracker tracker;
        connect(&fakeFolder.syncEngine(), &SyncEngine::itemCompleted, &tracker, &LocalDiscoveryTracker::slotItemCompleted);
        connect(&fakeFolder.syncEngine(), &SyncEngine::finished, &tracker, &LocalDiscoveryTracker::slotSyncFinished);
        auto trackerContains = [&](const char *path) {
            return tracker.localDiscoveryPaths().find(QString::fromLatin1(path)) != tracker.localDiscoveryPaths().end();
        };

        tracker.addTouchedPath("A/spurious"_L1);

        fakeFolder.localModifier().insert("A/a3"_L1);
        tracker.addTouchedPath("A/a3"_L1);

        fakeFolder.localModifier().insert("A/a4"_L1);
        fakeFolder.serverErrorPaths().append("A/a4"_L1);
        // We're not adding a4 as touched, it's in the same folder as a3 and will be seen.
        // And due to the error it should be added to the explicit list while a3 gets removed.

        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, tracker.localDiscoveryPaths());
        tracker.startSyncPartialDiscovery();
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        QVERIFY(fakeFolder.currentRemoteState().find("A/a3"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("A/a4"_L1));
        QVERIFY(!trackerContains("A/a3"));
        QVERIFY(trackerContains("A/a4"));
        QVERIFY(trackerContains("A/spurious")); // not removed since overall sync not successful

        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::FilesystemOnly);
        tracker.startSyncFullDiscovery();
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        QVERIFY(!fakeFolder.currentRemoteState().find("A/a4"_L1));
        QVERIFY(trackerContains("A/a4")); // had an error, still here
        QVERIFY(!trackerContains("A/spurious")); // removed due to full discovery

        fakeFolder.serverErrorPaths().clear();
        fakeFolder.syncJournal().wipeErrorBlacklist();
        tracker.addTouchedPath("A/newspurious"_L1); // will be removed due to successful sync

        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, tracker.localDiscoveryPaths());
        tracker.startSyncPartialDiscovery();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QVERIFY(fakeFolder.currentRemoteState().find("A/a4"_L1));
        QVERIFY(tracker.localDiscoveryPaths().empty());
    }

    void testTrackerRetriesFileChangedDuringSync()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        LocalDiscoveryTracker tracker;
        connect(&fakeFolder.syncEngine(), &SyncEngine::itemCompleted, &tracker, &LocalDiscoveryTracker::slotItemCompleted);
        connect(&fakeFolder.syncEngine(), &SyncEngine::finished, &tracker, &LocalDiscoveryTracker::slotSyncFinished);
        auto trackerContains = [&](const char *path) {
            return tracker.localDiscoveryPaths().find(QString::fromLatin1(path)) != tracker.localDiscoveryPaths().end();
        };
        auto restoreMinimumFileAge = qScopeGuard([] { SyncEngine::minimumFileAgeForUpload = 0s; });

        fakeFolder.localModifier().insert("A/a3"_L1);
        tracker.addTouchedPath("A/a3"_L1);

        SyncEngine::minimumFileAgeForUpload = 1h;
        ItemCompletedSpy completeSpy(fakeFolder);
        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, tracker.localDiscoveryPaths());
        tracker.startSyncPartialDiscovery();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(completeSpy.findItem("A/a3"_L1)->_status, SyncFileItem::Message);
        QVERIFY(!fakeFolder.currentRemoteState().find("A/a3"_L1));
        QVERIFY(trackerContains("A/a3"));

        SyncEngine::minimumFileAgeForUpload = 0s;
        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, tracker.localDiscoveryPaths());
        tracker.startSyncPartialDiscovery();
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QVERIFY(tracker.localDiscoveryPaths().empty());
    }

    void testDirectoryAndSubDirectory()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        fakeFolder.localModifier().mkdir("A/newDir"_L1);
        fakeFolder.localModifier().mkdir("A/newDir/subDir"_L1);
        fakeFolder.localModifier().insert("A/newDir/subDir/file"_L1, 10_B);

        // Only "A" was modified according to the file system tracker
        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, {"A"_L1});

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
        QVERIFY(fakeFolder.currentRemoteState().find("A/newDir/subDir/file"_L1));
    }

    // Items that were skipped because they are hidden are not in the database, so an incremental
    // local discovery does not find them in a folder the watcher did not report: only a full local
    // discovery does. See Folder::setIgnoreHiddenFiles().
    void testHiddenFilesNeedFullLocalDiscovery()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        fakeFolder.syncEngine().setIgnoreHiddenFiles(true);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        // the user creates a hidden folder while hidden files are still ignored
        fakeFolder.localModifier().mkdir("B/.hello"_L1);
        fakeFolder.localModifier().insert("B/.hello/Text File.txt"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(!fakeFolder.currentRemoteState().find("B/.hello"_L1));

        // the user enables hidden files, but an incremental sync only looks at the paths the
        // folder watcher reported and the hidden folder is not among them
        fakeFolder.syncEngine().setIgnoreHiddenFiles(false);
        fakeFolder.localModifier().insert("A/a3"_L1);
        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::DatabaseAndFilesystem, {"A"_L1});
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(fakeFolder.currentRemoteState().find("A/a3"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("B/.hello"_L1));

        // a full local discovery, as requested by Folder::setIgnoreHiddenFiles(), picks it up
        fakeFolder.syncEngine().setLocalDiscoveryOptions(LocalDiscoveryStyle::FilesystemOnly);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(fakeFolder.currentRemoteState().find("B/.hello/Text File.txt"_L1));
    }

    // Tests the behavior of invalid filename detection
    void testServerBlacklist()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        auto cap = TestUtils::testCapabilities();
        cap.insert({{"files"_L1, QVariantMap{{"blacklisted_files"_L1, QVariantList{".foo"_L1, "bar"_L1}}}}});
        fakeFolder.account()->setCapabilities({fakeFolder.account()->url(), cap});
        fakeFolder.localModifier().insert("C/.foo"_L1);
        fakeFolder.localModifier().insert("C/bar"_L1);
        fakeFolder.localModifier().insert("C/moo"_L1);
        fakeFolder.localModifier().insert("C/.moo"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(fakeFolder.currentRemoteState().find("C/moo"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("C/.moo"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("C/.foo"_L1));
        QVERIFY(!fakeFolder.currentRemoteState().find("C/bar"_L1));
    }
};

QTEST_GUILESS_MAIN(TestLocalDiscovery)
#include "testlocaldiscovery.moc"
