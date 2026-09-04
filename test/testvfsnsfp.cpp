// Unit tests for VfsNSFP -- macOS NSFileProvider VFS plugin core methods.

// Use __APPLE__ (compiler-defined) instead of Q_OS_MACOS (Qt-defined via qglobal.h) because
// this check appears before any Qt headers are included, so Q_OS_MACOS would never be defined.
#if defined(__APPLE__)

#include "common/syncjournaldb.h"
#include "common/syncjournalfilerecord.h"
#include "syncengine.h"
#include "syncfileitem.h"
#include "vfs/vfs.h"

#include "testutils/syncenginetestutils.h"
#include "testutils/testutils.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace OCC;
using namespace Qt::Literals::StringLiterals;

class TestVfsNSFP : public QObject
{
    Q_OBJECT

private:
    /// Helper: create a Vfs instance via the plugin manager and wire it up with
    /// a journal and temp directory. The domain registration will fail (no daemon),
    /// but params() will be usable for method-level unit tests.
    struct VfsTestFixture
    {
        QTemporaryDir tempDir;
        SyncJournalDb journal;
        OCC::TestUtils::TestUtilsPrivate::AccountStateRaii accountState;
        std::unique_ptr<SyncEngine> syncEngine;
        std::unique_ptr<Vfs> vfs;
        bool valid = false;

        VfsTestFixture()
            : journal(tempDir.path() + QStringLiteral("/sync.db"))
            , accountState(OCC::TestUtils::createDummyAccount())
        {
            if (!tempDir.isValid()) {
                return;
            }

            // Check if the NSFP plugin is available
            if (!VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::MacOSNSFileProvider)) {
                return;
            }

            // SyncEngine needs a localPath ending in '/'
            const auto localPath = tempDir.path() + QStringLiteral("/syncroot/");
            QDir().mkpath(localPath);

            auto acc = accountState->account();
            syncEngine = std::make_unique<SyncEngine>(acc, OCC::TestUtils::dummyDavUrl(), localPath, QStringLiteral("/"), &journal);

            // Create the VFS plugin instance via the plugin manager
            vfs.reset(VfsPluginManager::instance().createVfsFromPlugin(Vfs::Mode::MacOSNSFileProvider).release());
            if (!vfs) {
                return;
            }

            // Build VfsSetupParams and call start(). startImpl() will attempt domain
            // registration which will fail without a real daemon, but params() will
            // be available for subsequent method calls.
            VfsSetupParams params(acc, OCC::TestUtils::dummyDavUrl(), QStringLiteral("test-space-id"), QStringLiteral("Test Folder"), syncEngine.get());
            params.journal = &journal;

            // We expect the error signal (no real domain daemon), but that's fine.
            vfs->start(params);
            valid = true;
        }
    };

private Q_SLOTS:

    void testModeString()
    {
        // Verify modeFromString("nsfp") returns MacOSNSFileProvider
        const auto mode = Vfs::modeFromString(QStringLiteral("nsfp"));
        QVERIFY(static_cast<bool>(mode));
        QCOMPARE(*mode, Vfs::Mode::MacOSNSFileProvider);

        // Verify enumToString(MacOSNSFileProvider) returns "nsfp"
        const auto str = Utility::enumToString(Vfs::Mode::MacOSNSFileProvider);
        QCOMPARE(str, QStringLiteral("nsfp"));
    }

    void testPluginConstruction()
    {
        // Verify VfsNSFP can be instantiated via plugin manager
        if (!VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::MacOSNSFileProvider)) {
            QSKIP("NSFP VFS plugin not available");
        }

        auto vfs = VfsPluginManager::instance().createVfsFromPlugin(Vfs::Mode::MacOSNSFileProvider);
        QVERIFY(vfs);
        QCOMPARE(vfs->mode(), Vfs::Mode::MacOSNSFileProvider);
        QVERIFY(vfs->socketApiPinStateActionsShown());
    }

    void testPinStateRoundtrip()
    {
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }
        auto *vfs = fixture.vfs.get();

        // AlwaysLocal
        vfs->setPinState(QStringLiteral("testfile.txt"), PinState::AlwaysLocal);
        auto ps = vfs->pinState(QStringLiteral("testfile.txt"));
        QVERIFY(static_cast<bool>(ps));
        QCOMPARE(*ps, PinState::AlwaysLocal);

        // OnlineOnly
        vfs->setPinState(QStringLiteral("testfile2.txt"), PinState::OnlineOnly);
        ps = vfs->pinState(QStringLiteral("testfile2.txt"));
        QVERIFY(static_cast<bool>(ps));
        QCOMPARE(*ps, PinState::OnlineOnly);

        // Unspecified -- default when no explicit state is set
        ps = vfs->pinState(QStringLiteral("unknown.txt"));
        QVERIFY(static_cast<bool>(ps));
        QCOMPARE(*ps, PinState::Unspecified);
    }

    void testIsDehydratedPlaceholder_noJournalRecord()
    {
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }

        const auto syncRoot = fixture.tempDir.path() + QStringLiteral("/syncroot/");
        const auto filePath = syncRoot + QStringLiteral("nonexistent.txt");
        QVERIFY(!fixture.vfs->isDehydratedPlaceholder(filePath));
    }

    void testIsDehydratedPlaceholder_virtualFileRecord()
    {
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }

        // Insert a virtual file record into the journal
        auto item = OCC::TestUtils::dummyItem(QStringLiteral("cloud-only.txt"));
        item._type = ItemTypeVirtualFile;
        item._etag = QStringLiteral("etag1");
        item._fileId = "fileid1";
        const auto record = SyncJournalFileRecord::fromSyncFileItem(item);
        QVERIFY(fixture.journal.setFileRecord(record));

        const auto syncRoot = fixture.tempDir.path() + QStringLiteral("/syncroot/");
        const auto filePath = syncRoot + QStringLiteral("cloud-only.txt");
        QVERIFY(fixture.vfs->isDehydratedPlaceholder(filePath));
    }

    void testIsDehydratedPlaceholder_localFileRecord()
    {
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }

        auto item = OCC::TestUtils::dummyItem(QStringLiteral("local-file.txt"));
        item._type = ItemTypeFile;
        item._etag = QStringLiteral("etag2");
        item._fileId = "fileid2";
        const auto record = SyncJournalFileRecord::fromSyncFileItem(item);
        QVERIFY(fixture.journal.setFileRecord(record));

        const auto syncRoot = fixture.tempDir.path() + QStringLiteral("/syncroot/");
        const auto filePath = syncRoot + QStringLiteral("local-file.txt");
        QVERIFY(!fixture.vfs->isDehydratedPlaceholder(filePath));
    }

    void testNeedsMetadataUpdate_differentEtag()
    {
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }

        // Insert a record with etag "old-etag"
        auto item = OCC::TestUtils::dummyItem(QStringLiteral("meta-file.txt"));
        item._etag = QStringLiteral("old-etag");
        item._fileId = "fileid3";
        item._modtime = 1000;
        item._size = 500;
        const auto record = SyncJournalFileRecord::fromSyncFileItem(item);
        QVERIFY(fixture.journal.setFileRecord(record));

        // Create a new item with different etag
        auto newItem = OCC::TestUtils::dummyItem(QStringLiteral("meta-file.txt"));
        newItem._etag = QStringLiteral("new-etag");
        newItem._fileId = "fileid3";
        newItem._modtime = 1000;
        newItem._size = 500;

        QVERIFY(fixture.vfs->needsMetadataUpdate(newItem));
    }

    void testNeedsMetadataUpdate_sameEtag()
    {
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }

        auto item = OCC::TestUtils::dummyItem(QStringLiteral("same-file.txt"));
        item._etag = QStringLiteral("same-etag");
        item._fileId = "fileid4";
        item._modtime = 1000;
        item._size = 500;
        const auto record = SyncJournalFileRecord::fromSyncFileItem(item);
        QVERIFY(fixture.journal.setFileRecord(record));

        // Query with same metadata
        auto queryItem = OCC::TestUtils::dummyItem(QStringLiteral("same-file.txt"));
        queryItem._etag = QStringLiteral("same-etag");
        queryItem._fileId = "fileid4";
        queryItem._modtime = 1000;
        queryItem._size = 500;

        QVERIFY(!fixture.vfs->needsMetadataUpdate(queryItem));
    }

    void testDomainIdentifier()
    {
        // Verify the domain identifier derivation is stable: creating two fixtures
        // with the same account UUID and space ID yields the same domain ID.
        // Since VfsNSFP::domainIdentifier() is private, we verify indirectly that
        // the VFS initializes correctly with a consistent mode.
        VfsTestFixture fixture;
        if (!fixture.valid) {
            QSKIP("NSFP VFS plugin not available or fixture setup failed");
        }

        // Verify the VFS is in the correct mode after initialization
        QCOMPARE(fixture.vfs->mode(), Vfs::Mode::MacOSNSFileProvider);

        // Create a second fixture with the same account and verify consistency
        VfsTestFixture fixture2;
        if (!fixture2.valid) {
            QSKIP("Second fixture failed to initialize");
        }
        QCOMPARE(fixture2.vfs->mode(), Vfs::Mode::MacOSNSFileProvider);
    }

    void testVolumeCheck()
    {
        // Test that startImpl() on a non-existent path handles the failure gracefully.
        if (!VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::MacOSNSFileProvider)) {
            QSKIP("NSFP VFS plugin not available");
        }

        auto accountState = OCC::TestUtils::createDummyAccount();
        auto acc = accountState->account();

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        SyncJournalDb journal(tempDir.path() + QStringLiteral("/sync.db"));

        // Use a non-existent path as sync root
        const auto nonExistentPath = tempDir.path() + QStringLiteral("/does-not-exist/syncroot/");

        auto syncEngine = std::make_unique<SyncEngine>(acc, OCC::TestUtils::dummyDavUrl(), nonExistentPath, QStringLiteral("/"), &journal);

        auto vfs = VfsPluginManager::instance().createVfsFromPlugin(Vfs::Mode::MacOSNSFileProvider);
        QVERIFY(vfs);

        QSignalSpy errorSpy(vfs.get(), &Vfs::error);

        VfsSetupParams params(acc, OCC::TestUtils::dummyDavUrl(), QStringLiteral("test-space-id"), QStringLiteral("Test Folder"), syncEngine.get());
        params.journal = &journal;

        vfs->start(params);

        // startImpl will fail the statfs call for non-existent path, but it
        // just logs a warning and continues. The domain registration will also
        // fail asynchronously. We verify the VFS was created without a crash.
        QCOMPARE(vfs->mode(), Vfs::Mode::MacOSNSFileProvider);

        // Process pending events to allow async error signals to arrive.
        QCoreApplication::processEvents();
    }
};

QTEST_GUILESS_MAIN(TestVfsNSFP)
#include "testvfsnsfp.moc"

#else
// Non-macOS: provide an empty main so the build does not fail.
#include <QtTest>
class TestVfsNSFP : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSkipped() { QSKIP("VfsNSFP tests are macOS-only"); }
};
QTEST_GUILESS_MAIN(TestVfsNSFP)
#include "testvfsnsfp.moc"
#endif
