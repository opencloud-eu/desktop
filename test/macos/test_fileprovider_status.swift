import Foundation
import FileProvider

@main struct StatusTests {
    static func main() {
        let start = Date(timeIntervalSince1970: 100)
        let status = FileProviderSyncStatus(domainIdentifier: "test")
        func synced(_ snapshot: [String: Any]) -> Bool { snapshot["isSynced"] as! Bool }
        func snapshot(_ offset: Double = 4) -> [String: Any] {
            status.snapshot(isAuthenticated: true, now: start.addingTimeInterval(offset))
        }
        assert(!synced(snapshot()), "Authentication alone cannot claim synchronization")
        status.recordPending(count: 0, errors: 0, truncated: false, now: start)
        assert(!synced(snapshot()), "An empty pending set does not prove remote enumeration succeeded")
        let check = status.begin(.metadata, key: "workingSet", now: start)
        status.finish(check, error: nil, checkedRemote: true, now: start)
        assert(!synced(snapshot()), "A remote check invalidates the old pending sample")
        status.recordPending(count: 0, errors: 0, truncated: false, now: start.addingTimeInterval(0.5))
        assert(!synced(snapshot()), "Wait for the system's pending refresh interval")
        status.recordPending(count: 0, errors: 0, truncated: false, now: start.addingTimeInterval(2))
        assert(synced(snapshot()))
        let lastSync = snapshot()["lastSyncedAt"] as! Double
        assert((snapshot(20)["lastSyncedAt"] as! Double) == lastSync, "Polling cannot manufacture newer sync timestamps")
        status.invalidateNativeSample()
        assert(snapshot()["pendingKnown"] as! Bool == false, "Replacing native handles invalidates old pending state")
        status.recordPending(count: 0, errors: 0, truncated: false, expectedActivityRevision: 2,
                             now: start.addingTimeInterval(3))
        assert(snapshot()["pendingKnown"] as! Bool == false, "An old native observer cannot restore a stale sample")
        assert(snapshot()["lastSyncedAt"] as! Double == lastSync, "Native handle replacement preserves synchronization history")

        let upload = status.begin(.upload, key: "new-file", now: start.addingTimeInterval(20))
        assert(snapshot()["activeUploads"] as! Int == 1)
        status.recordPending(count: 0, errors: 0, truncated: false, now: start.addingTimeInterval(22))
        assert(!synced(snapshot(25)), "New uploads can be absent from the native pending set")
        status.finish(upload, error: NSFileProviderError(.insufficientQuota), now: start.addingTimeInterval(25))
        status.recordPending(count: 0, errors: 0, truncated: false, now: start.addingTimeInterval(27))
        assert(!synced(snapshot(30)))
        assert(snapshot()["errorCount"] as! Int == 1)
        let unrelated = status.begin(.metadata, key: "workingSet", now: start.addingTimeInterval(30))
        status.finish(unrelated, error: nil, checkedRemote: true, now: start.addingTimeInterval(30))
        assert(snapshot()["errorCount"] as! Int == 1, "Unrelated success cannot clear an upload error")
        let retry = status.begin(.upload, key: "new-file", now: start.addingTimeInterval(35))
        status.finish(retry, error: nil, now: start.addingTimeInterval(35))
        status.recordPending(count: 0, errors: 0, truncated: true, now: start.addingTimeInterval(37))
        assert(!synced(snapshot(40)), "A capped pending set is incomplete")
        status.recordPending(count: 1, errors: 1, errorDescription: "Failure", truncated: false, now: start.addingTimeInterval(37))
        assert(!synced(snapshot(40)))
        status.recordPending(count: 0, errors: 0, truncated: false, now: start.addingTimeInterval(38))
        assert(synced(snapshot(40)))
        assert(!synced(snapshot(60)), "Stale pending state must not claim the domain is currently synced")
        status.recordPending(count: 0, errors: 0, truncated: false, now: start.addingTimeInterval(44))
        let progress = Progress(totalUnitCount: 500)
        progress.fileTotalCount = 10
        progress.fileCompletedCount = 3
        progress.completedUnitCount = 250
        let native = status.snapshot(isAuthenticated: true, upload: progress, now: start.addingTimeInterval(45))
        assert(!synced(native))
        assert(native["activeUploads"] as! Int == 7)
        assert(native["uploadedBytes"] as! Int64 == 250)
        assert(native["uploadTotalBytes"] as! Int64 == 500)

        let emptyNative = Progress(totalUnitCount: 0)
        let idleNative = status.snapshot(isAuthenticated: true, upload: emptyNative, download: emptyNative,
                                         now: start.addingTimeInterval(45))
        assert(idleNative["activeUploads"] as! Int == 0)
        assert(idleNative["activeDownloads"] as! Int == 0)
        assert(synced(idleNative), "An initial native 0/0 progress is not evidence of a transfer")

        let old = status.begin(.upload, key: "overlap", now: start)
        let new = status.begin(.upload, key: "overlap", now: start)
        status.finish(new, error: NSFileProviderError(.serverUnreachable), now: start)
        status.finish(old, error: nil, now: start)
        assert(snapshot()["errorCount"] as! Int == 1, "An old completion cannot clear a newer failure")
        status.reset()
        assert(snapshot()["lastSyncedAt"] as! Double == 0)
        assert(!synced(snapshot()))

        assert(FileProviderSyncStatus.isCancellation(CancellationError()))
        assert(FileProviderSyncStatus.isCancellation(URLError(.cancelled)))
        assert(FileProviderSyncStatus.isCancellation(CocoaError(.userCancelled)))
        assert(!FileProviderSyncStatus.isCancellation(NSFileProviderError(.serverUnreachable)))
        let recovery = FileProviderSyncStatus(domainIdentifier: "recovery")
        func fail(_ key: String) {
            recovery.finish(recovery.begin(.upload, key: key), error: NSFileProviderError(.insufficientQuota))
        }
        func errorCount() -> Int { recovery.snapshot(isAuthenticated: true)["errorCount"] as! Int }
        fail("modify:removed")
        fail("download:unrelated")
        fail("create:parent/new-file")
        let late = recovery.begin(.upload, key: "modify:removed")
        recovery.retireItems(identifiers: ["removed"])
        recovery.finish(late, error: NSFileProviderError(.serverUnreachable))
        assert(errorCount() == 2, "Confirmed removal retires item errors and stale completions, preserving unrelated failures")
        recovery.retireItems(identifiers: ["parent"])
        assert(errorCount() == 1, "A removed directory retires failed creates within it")
        fail("create:root/same-name")
        recovery.retireItems(identifiers: ["new-server-id"], creationKeys: ["create:root/same-name"])
        assert(errorCount() == 1, "Observed deletion can retire the exact original create key")
        let trash = recovery.begin(.metadata, key: "modify:trashed")
        recovery.retireItems(identifiers: ["trashed"], preserving: trash)
        recovery.finish(trash, error: NSFileProviderError(.cannotSynchronize))
        assert(errorCount() == 2, "The active operation can still report a later metadata failure")

        let auth = FileProviderSyncStatus(domainIdentifier: "auth-revision")
        auth.clearAuthenticationErrors(requireRemoteCheck: true)
        auth.recordPending(count: 0, errors: 0, truncated: false, expectedActivityRevision: 0)
        assert(auth.snapshot(isAuthenticated: true)["pendingKnown"] as! Bool == false,
               "Authentication changes invalidate an in-flight pending sample")
        auth.reset()
        auth.recordPending(count: 0, errors: 0, truncated: false, expectedActivityRevision: 1)
        assert(auth.snapshot(isAuthenticated: true)["pendingKnown"] as! Bool == false,
               "Reset invalidates an in-flight pending sample")

        let other = FileProviderSyncStatus(domainIdentifier: "other")
        assert(other.snapshot(isAuthenticated: true)["errorCount"] as! Int == 0)
        let suite = "eu.opencloud.status-tests." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        defaults.set(["checked": start, "synced": start], forKey: "fp_sync_history_restarted")
        let restarted = FileProviderSyncStatus(domainIdentifier: "restarted", defaults: defaults)
        let restored = restarted.snapshot(isAuthenticated: true)
        assert(restored["lastSyncedAt"] as! Double == start.timeIntervalSince1970)
        assert(!synced(restored), "History after restart must not be treated as current synchronization")
        print("FileProvider status tests passed")
    }
}
