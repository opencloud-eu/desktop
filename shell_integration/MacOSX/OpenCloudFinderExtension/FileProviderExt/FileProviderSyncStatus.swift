import Foundation
import FileProvider

/// Domain-scoped activity combined with the system's pending set. Authentication
/// alone never marks a domain synced, and old persisted dates are only history.
final class FileProviderSyncStatus {
    enum Kind { case upload, download, metadata }
    struct Operation { let id: UUID; let key: String; let kind: Kind }
    static func isCancellation(_ error: Error) -> Bool {
        let native = error as NSError
        return error is CancellationError || (native.domain == NSCocoaErrorDomain && native.code == NSUserCancelledError)
            || (native.domain == NSURLErrorDomain && native.code == NSURLErrorCancelled)
    }

    private static let registryLock = NSLock()
    private static var registry: [String: FileProviderSyncStatus] = [:]
    static func shared(domain: NSFileProviderDomain, suiteName: String) -> FileProviderSyncStatus {
        registryLock.lock()
        defer { registryLock.unlock() }
        let key = suiteName + ":" + domain.identifier.rawValue
        if let existing = registry[key] {
            // A new extension instance must not inherit the previous instance's
            // native XPC connection after the system invalidates it.
            DispatchQueue.main.async { existing.attach(domain: domain) }
            return existing
        }
        let status = FileProviderSyncStatus(domainIdentifier: domain.identifier.rawValue,
                                            defaults: UserDefaults(suiteName: suiteName))
        registry[key] = status
        DispatchQueue.main.async { status.attach(domain: domain) }
        return status
    }

    private let lock = NSLock()
    private let identifier: String
    private let defaults: UserDefaults?
    private var active: [UUID: Operation] = [:]
    private var errors: [String: NSError] = [:]
    private var latestOperation: [String: UUID] = [:]
    private var activityRevision = 0
    private var pendingCount = 0
    private var pendingErrors = 0
    private var pendingErrorDescription = ""
    private var pendingKnown = false
    private var pendingTruncated = false
    private var pendingSampledAt = Date.distantPast
    private var lastActivity = Date.distantPast
    private var lastCheckedAt: Date?
    private var lastSyncedAt: Date?
    private var checkedThisProcess = false
    private var revision = 0
    private var settledRevision = -1
    private var settleInterval: TimeInterval = 1
    // The manager, progress objects and pending observer are used on the main queue.
    private var manager: NSFileProviderManager?
    private var uploadProgress: Progress?
    private var downloadProgress: Progress?
    private var pendingObserver: PendingObserver?
    private var attachmentGeneration = UUID()
    private var lastPendingRequest = Date.distantPast
    private var dateKey: String { "fp_sync_history_" + identifier }

    init(domainIdentifier: String, defaults: UserDefaults? = nil) {
        identifier = domainIdentifier
        self.defaults = defaults
        let history = defaults?.dictionary(forKey: "fp_sync_history_" + domainIdentifier)
        lastCheckedAt = history?["checked"] as? Date
        lastSyncedAt = history?["synced"] as? Date
    }

    private func attach(domain: NSFileProviderDomain) {
        attachmentGeneration = UUID()
        pendingObserver?.cancel()
        pendingObserver = nil
        invalidateNativeSample()
        let manager = NSFileProviderManager(for: domain)
        self.manager = manager
        uploadProgress = manager?.globalProgress(for: .uploading)
        downloadProgress = manager?.globalProgress(for: .downloading)
        lastPendingRequest = .distantPast
        refreshPending()
    }

    func invalidateNativeSample() {
        lock.lock()
        activityRevision += 1
        revision += 1
        pendingKnown = false
        lock.unlock()
    }

    func begin(_ kind: Kind, key: String, now: Date = Date()) -> Operation {
        lock.lock()
        defer { lock.unlock() }
        let operation = Operation(id: UUID(), key: key, kind: kind)
        active[operation.id] = operation
        latestOperation[key] = operation.id
        activityRevision += 1
        pendingKnown = false
        revision += 1
        lastActivity = now
        return operation
    }

    func finish(_ operation: Operation, error: Error?, checkedRemote: Bool = false, now: Date = Date()) {
        lock.lock()
        defer { lock.unlock() }
        guard active.removeValue(forKey: operation.id) != nil else { return }
        lastActivity = now
        pendingKnown = false
        revision += 1
        activityRevision += 1
        guard latestOperation[operation.key] == operation.id else { return }
        latestOperation.removeValue(forKey: operation.key)
        if let error = error as NSError? {
            // Cancellation and expired cursors are normal control flow, not sync failures.
            let cancelled = Self.isCancellation(error)
            let expired = error.domain == NSFileProviderErrorDomain
                && [NSFileProviderError.Code.syncAnchorExpired.rawValue, NSFileProviderError.Code.pageExpired.rawValue].contains(error.code)
            if !cancelled && !expired { errors[operation.key] = error }
        } else {
            errors.removeValue(forKey: operation.key)
            if checkedRemote {
                lastCheckedAt = now
                checkedThisProcess = true
                persistDates()
            }
        }
    }

    func clearAuthenticationErrors(requireRemoteCheck: Bool = false) {
        lock.lock()
        let previousCount = errors.count
        errors = errors.filter { !($0.value.domain == NSFileProviderErrorDomain && $0.value.code == NSFileProviderError.notAuthenticated.rawValue) }
        if requireRemoteCheck || errors.count != previousCount { checkedThisProcess = false }
        activityRevision += 1
        revision += 1
        pendingKnown = false
        lock.unlock()
    }

    /// Include parents of failed creates, whose local items have no server ID yet.
    func trackedItemIdentifiers() -> Set<String> {
        lock.lock()
        defer { lock.unlock() }
        return Set(Set(errors.keys).union(latestOperation.keys).compactMap(Self.itemIdentifier))
    }

    private static func itemIdentifier(for key: String) -> String? {
        for prefix in ["download:", "modify:", "delete:", "enumerate:"] where key.hasPrefix(prefix) {
            return String(key.dropFirst(prefix.count))
        }
        if key.hasPrefix("create:"), let separator = key.lastIndex(of: "/") {
            return String(key[key.index(key.startIndex, offsetBy: 7)..<separator])
        }
        return nil
    }

    /// Retire only confirmed removed items. Unrelated enumeration success cannot
    /// clear failed uploads, including new files absent from the native pending set.
    func retireItems(identifiers: Set<String>, creationKeys: Set<String> = [], preserving operation: Operation? = nil) {
        lock.lock()
        defer { lock.unlock() }
        func matches(_ key: String) -> Bool {
            creationKeys.contains(key) || Self.itemIdentifier(for: key).map { identifiers.contains($0) } == true
        }
        errors = errors.filter { !matches($0.key) }
        latestOperation = latestOperation.filter { !matches($0.key) || $0.value == operation?.id }
        activityRevision += 1
        revision += 1
        pendingKnown = false
    }

    func reset() {
        lock.lock()
        activityRevision += 1
        active.removeAll()
        errors.removeAll()
        latestOperation.removeAll()
        pendingKnown = false
        pendingErrors = 0
        checkedThisProcess = false
        lastCheckedAt = nil
        lastSyncedAt = nil
        revision += 1
        defaults?.removeObject(forKey: dateKey)
        lock.unlock()
    }

    /// Native pending enumeration only; status polling never scans the server.
    func refreshPending() {
        if !Thread.isMainThread {
            DispatchQueue.main.async { self.refreshPending() }
            return
        }
        guard let manager = manager, pendingObserver == nil,
              Date().timeIntervalSince(lastPendingRequest) >= 1 else { return }
        lastPendingRequest = Date()
        lock.lock()
        let requestedRevision = activityRevision
        lock.unlock()
        let requestedAttachment = attachmentGeneration
        let enumerator = manager.enumeratorForPendingItems()
        let observer = PendingObserver(enumerator: enumerator) { [weak self] count, errors, description, failure in
            guard let self = self else { return }
            DispatchQueue.main.async {
                guard self.attachmentGeneration == requestedAttachment else { return }
                self.recordPending(count: count, errors: errors, errorDescription: description,
                                   truncated: enumerator.isMaximumSizeReached,
                                   refreshInterval: enumerator.refreshInterval, failure: failure, expectedActivityRevision: requestedRevision)
                self.pendingObserver = nil
            }
        }
        pendingObserver = observer
        observer.start()
    }

    func recordPending(count: Int, errors: Int, errorDescription: String = "", truncated: Bool,
                       refreshInterval: TimeInterval = 1, failure: Error? = nil, expectedActivityRevision: Int? = nil, now: Date = Date()) {
        lock.lock()
        defer { lock.unlock() }
        guard expectedActivityRevision == nil || expectedActivityRevision == activityRevision else { return }
        pendingKnown = failure == nil
        if failure == nil {
            if pendingCount != count || pendingErrors != errors || pendingTruncated != truncated { revision += 1 }
            pendingCount = count
            pendingErrors = errors
            pendingErrorDescription = errorDescription
            pendingTruncated = truncated
            pendingSampledAt = now
            settleInterval = max(1, refreshInterval)
        }
    }

    func snapshot(isAuthenticated: Bool, completionHandler: @escaping ([String: Any]) -> Void) {
        DispatchQueue.main.async {
            self.refreshPending()
            completionHandler(self.snapshot(isAuthenticated: isAuthenticated,
                                             upload: self.uploadProgress, download: self.downloadProgress))
        }
    }

    func snapshot(isAuthenticated: Bool, upload: Progress? = nil, download: Progress? = nil,
                  now: Date = Date()) -> [String: Any] {
        lock.lock()
        defer { lock.unlock() }
        func hasOutstandingWork(_ progress: Progress?) -> Bool {
            guard let progress = progress, !progress.isFinished, !progress.isCancelled else { return false }
            return progress.totalUnitCount > progress.completedUnitCount
                || (progress.fileTotalCount ?? 0) > (progress.fileCompletedCount ?? 0)
        }
        // Some OS versions expose an unfinished 0/0 progress before the first
        // operation. Own callbacks and pending items cover unknown-length work.
        let uploadActive = hasOutstandingWork(upload)
        let downloadActive = hasOutstandingWork(download)
        let freshPending = pendingKnown && now.timeIntervalSince(pendingSampledAt) <= max(5, settleInterval * 2)
        let nativeUploads = uploadActive ? max(1, (upload?.fileTotalCount ?? 0) - (upload?.fileCompletedCount ?? 0)) : 0
        let nativeDownloads = downloadActive ? max(1, (download?.fileTotalCount ?? 0) - (download?.fileCompletedCount ?? 0)) : 0
        let idle = isAuthenticated && checkedThisProcess && freshPending && !pendingTruncated
            && pendingCount == 0 && pendingErrors == 0 && errors.isEmpty && active.isEmpty
            && !uploadActive && !downloadActive
            && now.timeIntervalSince(lastActivity) >= settleInterval
            && pendingSampledAt.timeIntervalSince(lastActivity) >= settleInterval
        if idle && settledRevision != revision {
            settledRevision = revision
            lastSyncedAt = now
            persistDates()
        }
        let firstError = errors.sorted { $0.key < $1.key }.first?.value
        let result: [String: Any] = [
            "schemaVersion": 1, "domainIdentifier": identifier,
            "isAuthenticated": isAuthenticated, "isSynced": idle,
            "activeUploads": max(active.values.filter { $0.kind == .upload }.count, Int(nativeUploads)),
            "activeDownloads": max(active.values.filter { $0.kind == .download }.count, Int(nativeDownloads)),
            "activeMetadata": active.values.filter { $0.kind == .metadata }.count,
            "pendingItems": pendingCount, "pendingKnown": freshPending, "pendingTruncated": pendingTruncated,
            "errorCount": errors.count + pendingErrors,
            "errorDescription": firstError.map(Self.safeDescription) ?? pendingErrorDescription,
            "uploadedBytes": uploadActive ? max(0, upload?.completedUnitCount ?? 0) : 0,
            "uploadTotalBytes": uploadActive ? max(0, upload?.totalUnitCount ?? 0) : 0,
            "downloadedBytes": downloadActive ? max(0, download?.completedUnitCount ?? 0) : 0,
            "downloadTotalBytes": downloadActive ? max(0, download?.totalUnitCount ?? 0) : 0,
            "sampledAt": now.timeIntervalSince1970,
            "lastCheckedAt": lastCheckedAt?.timeIntervalSince1970 ?? 0.0,
            "lastSyncedAt": lastSyncedAt?.timeIntervalSince1970 ?? 0.0
        ]
        return result
    }

    private func persistDates() {
        var dates: [String: Date] = [:]
        dates["checked"] = lastCheckedAt
        dates["synced"] = lastSyncedAt
        defaults?.set(dates, forKey: dateKey)
    }

    private static func safeDescription(_ error: NSError) -> String {
        // Never send server response bodies, URLs or filenames over the status channel.
        if error.domain == NSFileProviderErrorDomain {
            switch error.code {
            case NSFileProviderError.notAuthenticated.rawValue: return "Sign in to resume syncing."
            case NSFileProviderError.serverUnreachable.rawValue: return "The server is unreachable."
            case NSFileProviderError.insufficientQuota.rawValue: return "The server has insufficient storage."
            default: return "Some items could not be synced."
            }
        }
        if error.domain == NSCocoaErrorDomain && [NSFileReadNoPermissionError, NSFileWriteNoPermissionError].contains(error.code) {
            return "Permission is required to sync some items."
        }
        return "Some items could not be synced."
    }

    private final class PendingObserver: NSObject, NSFileProviderEnumerationObserver {
        let enumerator: NSFileProviderPendingSetEnumerator
        let completion: (Int, Int, String, Error?) -> Void
        private var count = 0
        private var errorCount = 0
        private var errorDescription = ""
        private var finished = false
        private let lock = NSLock()
        init(enumerator: NSFileProviderPendingSetEnumerator, completion: @escaping (Int, Int, String, Error?) -> Void) {
            self.enumerator = enumerator
            self.completion = completion
        }
        func start() {
            enumerator.enumerateItems(for: self, startingAt: NSFileProviderPage(NSFileProviderPage.initialPageSortedByName as Data))
            DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
                self?.finish(NSFileProviderError(.cannotSynchronize))
            }
        }
        func didEnumerate(_ items: [NSFileProviderItem]) {
            lock.lock()
            defer { lock.unlock() }
            guard !finished else { return }
            count += items.count
            for item in items {
                if let error = (item.uploadingError ?? nil) ?? (item.downloadingError ?? nil) {
                    errorCount += 1
                    if errorDescription.isEmpty { errorDescription = FileProviderSyncStatus.safeDescription(error as NSError) }
                }
            }
        }
        func finishEnumerating(upTo nextPage: NSFileProviderPage?) {
            lock.lock()
            let shouldContinue = !finished
            lock.unlock()
            guard shouldContinue else { return }
            if let page = nextPage {
                enumerator.enumerateItems(for: self, startingAt: page)
            } else { finish(nil) }
        }
        func finishEnumeratingWithError(_ error: Error) { finish(error) }
        func cancel() { finish(CocoaError(.userCancelled)) }
        private func finish(_ error: Error?) {
            lock.lock()
            guard !finished else { lock.unlock(); return }
            finished = true
            let result = (count, errorCount, errorDescription)
            lock.unlock()
            enumerator.invalidate()
            completion(result.0, result.1, result.2, error)
        }
    }
}
