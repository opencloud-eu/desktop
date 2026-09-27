import FileProvider
import UniformTypeIdentifiers

extension FileProviderExtension: NSFileProviderSearching {
    func searchEnumerator(for request: NSFileProviderStringSearchRequest) -> any NSFileProviderSearchEnumerator {
        ProviderSearchEnumerator(query: request.query, desiredResults: request.desiredNumberOfResults,
                                 client: webdavClient, database: database)
    }
}

/// Each search holds a bounded snapshot. Finder can cancel it as soon as the
/// user types another character, without leaving a remote traversal running.
final class ProviderSearchEnumerator: NSObject, NSFileProviderSearchEnumerator {
    private let query: String
    private let limit: Int
    private let client: WebDAVClient?
    private let database: ItemDatabase?
    private let lock = NSLock()
    private var task: Task<Void, Never>?
    private var invalidated = false
    private var results: [ProviderSearchResult]?
    private var requestID = UUID()

    init(query: String, desiredResults: Int, client: WebDAVClient?, database: ItemDatabase?) {
        self.query = query
        limit = min(max(desiredResults, 100), 1000)
        self.client = client
        self.database = database
    }

    func invalidate() {
        lock.lock()
        invalidated = true
        let pending = task
        task = nil
        lock.unlock()
        pending?.cancel()
    }

    func enumerateSearchResults(for observer: any NSFileProviderSearchEnumerationObserver, startingAt page: NSFileProviderPage?) {
        lock.lock()
        guard !invalidated else {
            lock.unlock()
            observer.finishEnumeratingWithError(CocoaError(.userCancelled))
            return
        }
        task?.cancel()
        let identifier = UUID()
        requestID = identifier
        let cachedResults = results
        task = Task {
            do {
                guard let client, let database else { throw NSFileProviderError(.notAuthenticated) }
                let offset: Int
                if let page {
                    guard let text = String(data: page.rawValue, encoding: .utf8), let parsed = Int(text), parsed >= 0 else {
                        throw NSFileProviderError(.pageExpired)
                    }
                    offset = parsed
                } else { offset = 0 }
                var snapshot = cachedResults
                if snapshot == nil {
                    guard offset == 0 else { throw NSFileProviderError(.pageExpired) }
                    let started = Date()
                    let found = try await client.searchFiles(query: query, limit: limit)
                    var fetched: [ProviderSearchResult] = []
                    for remote in found {
                        try Task.checkCancellation()
                        let metadata = try await database.mergeSearchResult(remote, webdav: client, fetchedAfter: started)
                        fetched.append(ProviderSearchResult(metadata: metadata))
                    }
                    try Task.checkCancellation()
                    guard store(fetched, for: identifier) else { throw CancellationError() }
                    snapshot = fetched
                }
                try Task.checkCancellation()
                let pageResults = snapshot ?? []
                guard offset <= pageResults.count else { throw NSFileProviderError(.pageExpired) }
                // Exceeding Finder's page limit terminates the extension process.
                let pageSize = min(observer.maximumNumberOfResultsPerPage, 500)
                guard pageSize > 0 else { throw CocoaError(.coderInvalidValue) }
                let end = offset + min(pageSize, pageResults.count - offset)
                observer.didEnumerate(Array(pageResults[offset..<end]))
                observer.finishEnumerating(upTo: end < pageResults.count ? NSFileProviderPage(Data(String(end).utf8)) : nil)
            } catch {
                observer.finishEnumeratingWithError(fileProviderError(error))
            }
        }
        lock.unlock()
    }

    private func store(_ value: [ProviderSearchResult], for identifier: UUID) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard !invalidated, requestID == identifier else { return false }
        results = value
        return true
    }
}

final class ProviderSearchResult: NSObject, NSFileProviderSearchResult {
    private let item: FileProviderItem
    let lastUsedDate: Date?
    init(metadata: ItemMetadata) {
        item = FileProviderItem(metadata: metadata, parentItemIdentifier: NSFileProviderItemIdentifier(metadata.parentOcId))
        lastUsedDate = metadata.finderMetadata.lastUsedDate
    }
    var itemIdentifier: NSFileProviderItemIdentifier { item.itemIdentifier }
    var filename: String { item.filename }
    var creationDate: Date? { item.creationDate }
    var contentModificationDate: Date? { item.contentModificationDate }
    var contentType: UTType { item.contentType }
    var documentSize: NSNumber? { item.documentSize }
}
