// Integration test stubs for VfsNSFP -- macOS NSFileProvider VFS plugin.
//
// These tests require a real macOS 12+ system with the NSFileProvider daemon
// running. They are stubs that document the expected integration test scenarios
// and ensure the test infrastructure is in place for future CI.

// Use __APPLE__ (compiler-defined) instead of Q_OS_MACOS (Qt-defined via qglobal.h) because
// this check appears before any Qt headers are included, so Q_OS_MACOS would never be defined.
#if defined(__APPLE__)

#include <QtTest>

class TestVfsNSFPIntegration : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDomainRegistration()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would register a domain via NsfpDomainManager::addDomain() "
              "and verify it appears in [NSFileProviderManager getDomainsWithCompletionHandler:].");
    }

    void testPlaceholderAppearance()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would call createPlaceholder() with a SyncFileItem and verify "
              "the item appears as a cloud-only file in Finder via the File Provider framework.");
    }

    void testHydrationFlow()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would open a placeholder file and verify that the File Provider "
              "extension receives a fetchContents request via XPC and the file becomes "
              "available locally with its full contents.");
    }

    void testEvictionFlow()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would evict a hydrated file via NsfpDomainManager::evictItem() "
              "and verify the file reverts to a dehydrated placeholder state, freeing "
              "local disk space while keeping the cloud reference.");
    }

    void testUploadFlow()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would copy a new file into the domain folder and verify that "
              "the sync engine picks it up, uploads it to the server, and the file is "
              "subsequently eligible for eviction.");
    }

    void testPinStateAlwaysLocal()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would set a folder to PinState::AlwaysLocal and verify that "
              "all child placeholder files are hydrated (downloaded) automatically, "
              "ensuring the folder contents are always available offline.");
    }

    void testMigrationFromXattr()
    {
        QSKIP("Requires macOS 12+ with NSFileProvider daemon. "
              "This test would set up a sync folder with existing xattr-based VFS "
              "placeholders, switch to NSFP mode, and verify that the migration "
              "completes without data loss and all files are accessible.");
    }
};

QTEST_GUILESS_MAIN(TestVfsNSFPIntegration)
#include "testvfsnsfp_integration.moc"

#else
// Non-macOS: provide an empty main so the build does not fail.
#include <QtTest>
class TestVfsNSFPIntegration : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSkipped() { QSKIP("VfsNSFP integration tests are macOS-only"); }
};
QTEST_GUILESS_MAIN(TestVfsNSFPIntegration)
#include "testvfsnsfp_integration.moc"
#endif
