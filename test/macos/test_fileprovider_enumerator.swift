import Foundation
import FileProvider

// Transport and extension doubles keep these tests independent of Finder and credentials.
actor WebDAVClient {
    var supportsTrash = false
    var listings: [String: [WebDAVItem]] = [:]
    var fail = false
    var failure: WebDAVError?
    var delay: UInt64 = 0
    var requests = 0
    func delayRequests(_ nanoseconds: UInt64) { delay = nanoseconds }
    func requestCount() -> Int { requests }
    func configure(_ listings: [String: [WebDAVItem]], fail: Bool = false, failure: WebDAVError? = nil) {
        self.listings = listings
        self.fail = fail
        self.failure = failure
    }
    func isRootPath(_ path: String) -> Bool { path == "/" }
    func listDirectory(path: String) async throws -> [WebDAVItem] {
        requests += 1
        if delay > 0 { try await Task.sleep(nanoseconds: delay) }
        if let failure = failure { throw failure }
        if fail { throw WebDAVError.serverError }
        guard let listing = listings[path] else { throw WebDAVError.fileNotFound }
        return listing
    }
}
final class FileProviderExtension {
    let syncStatus = FileProviderSyncStatus(domainIdentifier: "test")
    func remoteCheckSucceeded() {}
    var supportsTrash = false
    var database: ItemDatabase?
    var webdavClient: WebDAVClient?
    var isAuthenticated = true
    func resolveItemFromServer(identifier: NSFileProviderItemIdentifier, webdav: WebDAVClient, database: ItemDatabase) async throws -> ItemMetadata? { nil }
}

enum FileProviderTrash {
    static func refresh(webdav: WebDAVClient, database: ItemDatabase, parent: ItemMetadata? = nil) async throws -> [ItemMetadata] { [] }
}

final class ChangeObserver: NSObject, NSFileProviderChangeObserver {
    var updated: [NSFileProviderItem] = []
    var deleted: [NSFileProviderItemIdentifier] = []
    var anchor: NSFileProviderSyncAnchor?
    var error: Error?
    var moreComing = false
    let completion: () -> Void
    init(completion: @escaping () -> Void) { self.completion = completion }
    func didUpdate(_ updatedItems: [NSFileProviderItem]) { updated += updatedItems }
    func didDeleteItems(withIdentifiers deletedItemIdentifiers: [NSFileProviderItemIdentifier]) { deleted += deletedItemIdentifiers }
    func finishEnumeratingChanges(upTo syncAnchor: NSFileProviderSyncAnchor, moreComing: Bool) {
        anchor = syncAnchor
        self.moreComing = moreComing
        completion()
    }
    func finishEnumeratingWithError(_ error: Error) { self.error = error; completion() }
}

final class EnumerationObserver: NSObject, NSFileProviderEnumerationObserver {
    var items: [NSFileProviderItem] = []
    var next: NSFileProviderPage?
    var error: Error?
    let completion: () -> Void
    init(completion: @escaping () -> Void) { self.completion = completion }
    func didEnumerate(_ updatedItems: [NSFileProviderItem]) { items += updatedItems }
    func finishEnumerating(upTo nextPage: NSFileProviderPage?) { next = nextPage; completion() }
    func finishEnumeratingWithError(_ error: Error) { self.error = error; completion() }
}

@main
struct EnumeratorTests {
    static func remote(_ id: String, path: String, directory: Bool = false, etag: String = "v1") -> WebDAVItem {
        WebDAVItem(ocId: id, fileId: id, remotePath: path, filename: id, etag: etag,
                   contentType: "", size: 10, lastModified: nil, creationDate: nil,
                   isDirectory: directory, permissions: "", ownerId: "", ownerDisplayName: "")
    }
    static func changes(_ enumerator: FileProviderEnumerator, since anchor: Data) async -> ChangeObserver {
        var observer: ChangeObserver!
        await withCheckedContinuation { continuation in
            observer = ChangeObserver { continuation.resume() }
            enumerator.enumerateChanges(for: observer, from: NSFileProviderSyncAnchor(anchor))
        }
        return observer
    }
    static func items(_ enumerator: FileProviderEnumerator, page: NSFileProviderPage) async -> EnumerationObserver {
        var observer: EnumerationObserver!
        await withCheckedContinuation { continuation in
            observer = EnumerationObserver { continuation.resume() }
            enumerator.enumerateItems(for: observer, startingAt: page)
        }
        return observer
    }
    static func main() async throws {
        let container = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: container) }
        let database = try ItemDatabase(containerURL: container, domainIdentifier: "enumeration")
        let server = WebDAVClient()
        let ext = FileProviderExtension()
        ext.database = database
        ext.webdavClient = server
        let domain = NSFileProviderDomain(identifier: NSFileProviderDomainIdentifier("test"), displayName: "Tests")
        let enumerator = FileProviderEnumerator(enumeratedItemIdentifier: .workingSet, domain: domain, fpExtension: ext)
        let root = remote("root", path: "/", directory: true)
        let folder = remote("folder", path: "/folder", directory: true)
        let child = remote("child", path: "/folder/child")
        try await database.addItemMetadata(ItemMetadata(from: folder, parentOcId: ItemDatabase.rootContainerId))
        try await database.addItemMetadata(ItemMetadata(from: child, parentOcId: folder.ocId))
        let anchor = try await database.currentSyncAnchor()
        let changedChild = remote("child", path: "/folder/child", etag: "v2")
        await server.configure(["/": [root, folder], "/folder": [folder, changedChild]])
        let first = await changes(enumerator, since: anchor)
        precondition(first.error == nil)
        precondition(first.updated.contains { $0.itemIdentifier.rawValue == "child" }, "Working set must refresh nested files")
        let replay = await changes(enumerator, since: anchor)
        precondition(replay.updated.contains { $0.itemIdentifier.rawValue == "child" }, "Changes must replay after the cache was updated")
        await server.configure([:], fail: true)
        let failed = await changes(enumerator, since: first.anchor!.rawValue)
        precondition(failed.error != nil && failed.anchor == nil, "A failed request must not finish successfully or advance the anchor")
        await server.configure([:], failure: .permissionDenied)
        let forbidden = await changes(enumerator, since: first.anchor!.rawValue)
        let forbiddenError = forbidden.error as NSError?
        precondition(forbiddenError?.domain == NSCocoaErrorDomain && forbiddenError?.code == CocoaError.fileReadNoPermission.rawValue,
                     "Forbidden reads must report a permission error, not invalid credentials or insufficient quota")
        precondition(forbidden.anchor == nil, "Forbidden enumeration must not advance its anchor")
        await server.configure([:], failure: .notAuthenticated)
        let unauthenticated = await changes(enumerator, since: first.anchor!.rawValue)
        precondition((unauthenticated.error as NSError?)?.domain == NSFileProviderErrorDomain &&
                     (unauthenticated.error as NSError?)?.code == NSFileProviderError.notAuthenticated.rawValue,
                     "Authentication failure must remain distinct from permission denial")
        await server.configure(["/": [root, folder], "/folder": [folder]])
        let removed = await changes(enumerator, since: first.anchor!.rawValue)
        precondition(removed.deleted.map(\.rawValue).contains("child"), "Working set must report nested deletions")
        let expired = await changes(enumerator, since: Data("old timestamp".utf8))
        precondition((expired.error as NSError?)?.code == NSFileProviderError.syncAnchorExpired.rawValue, "Legacy timestamp anchors must request full enumeration")
        let a = remote("a", path: "/a", directory: true)
        let b = remote("b", path: "/b", directory: true)
        let moving = remote("moving", path: "/a/moving", directory: true)
        let nested = remote("nested", path: "/a/moving/nested")
        for parent in [a, b] {
            try await database.addItemMetadata(ItemMetadata(from: parent, parentOcId: ItemDatabase.rootContainerId))
        }
        try await database.addItemMetadata(ItemMetadata(from: moving, parentOcId: "a"))
        try await database.addItemMetadata(ItemMetadata(from: nested, parentOcId: "moving"))
        try await database.setDownloaded(ocId: "nested", downloaded: true)
        let moveAnchor = try await database.currentSyncAnchor()
        let moved = remote("moving", path: "/b/moving", directory: true)
        await server.configure(["/": [root, a, b], "/a": [a], "/b": [b, moved],
                                "/b/moving": [moved, remote("nested", path: "/b/moving/nested")]])
        let movedChanges = await changes(enumerator, since: moveAnchor)
        precondition(movedChanges.error == nil)
        precondition(!movedChanges.deleted.map(\.rawValue).contains("moving"), "A cross-parent move must not become a deletion")
        let preserved = await database.itemMetadata(ocId: "nested")
        precondition(preserved?.remotePath == "/b/moving/nested" && preserved?.isDownloaded == true,
                     "Moving a folder between known parents must preserve downloaded descendants")
        // A regular source-folder enumeration must also preserve cross-parent moves.
        let moveBackAnchor = try await database.currentSyncAnchor()
        await server.configure(["/": [root, a, b], "/a": [a, moving], "/b": [b], "/a/moving": [moving, nested]])
        let sourceEnumerator = FileProviderEnumerator(enumeratedItemIdentifier: NSFileProviderItemIdentifier("b"), domain: domain, fpExtension: ext)
        let movedBack = await changes(sourceEnumerator, since: moveBackAnchor)
        precondition(movedBack.error == nil && movedBack.deleted.map(\.rawValue).contains("moving"), "Source folder must report a move out")
        let preservedAgain = await database.itemMetadata(ocId: "nested")
        precondition(preservedAgain?.remotePath == "/a/moving/nested" && preservedAgain?.isDownloaded == true,
                     "Source-folder enumeration must preserve moved descendants")
        // Persistent path identities survive local moves and metadata-cache loss.
        let idlessFolder = remote(WebDAVItem.generateIdentifier(from: "/idless"), path: "/idless", directory: true)
        let idlessChild = remote(WebDAVItem.generateIdentifier(from: "/idless/child"), path: "/idless/child")
        var identityDB: ItemDatabase? = try ItemDatabase(containerURL: container, domainIdentifier: "identity")
        let folderIdentity = try await identityDB!.mergeServerMetadata(ItemMetadata(from: idlessFolder, parentOcId: ItemDatabase.rootContainerId))
        let childIdentity = try await identityDB!.mergeServerMetadata(ItemMetadata(from: idlessChild, parentOcId: folderIdentity.ocId))
        precondition(folderIdentity.ocId.hasPrefix("local:") && childIdentity.ocId.hasPrefix("local:"))
        let movedFolder = remote(WebDAVItem.generateIdentifier(from: "/renamed"), path: "/renamed", directory: true)
        let movedChild = remote(WebDAVItem.generateIdentifier(from: "/renamed/child"), path: "/renamed/child")
        let movedIdentity = try await identityDB!.mergeServerMetadata(ItemMetadata(from: movedFolder, parentOcId: ItemDatabase.rootContainerId), preservingIdentifier: folderIdentity.ocId)
        precondition(movedIdentity.ocId == folderIdentity.ocId)
        let childAtNewPath = try await identityDB!.mergeServerMetadata(ItemMetadata(from: movedChild, parentOcId: folderIdentity.ocId))
        precondition(childAtNewPath.ocId == childIdentity.ocId, "Directory moves must preserve descendant IDs")
        identityDB = nil
        try FileManager.default.removeItem(at: container.appendingPathComponent("FileProvider/items-identity.sqlite"))
        let recoveredDB = try ItemDatabase(containerURL: container, domainIdentifier: "identity")
        await server.configure(["/": [root, movedFolder], "/renamed": [movedFolder, movedChild], "/renamed/child": [movedChild]])
        let recovered = try await recoveredDB.resolveItem(identifier: childIdentity.ocId, webdav: server)
        precondition(recovered?.ocId == childIdentity.ocId && recovered?.parentOcId == folderIdentity.ocId,
                     "Sidecar recovery must preserve nested identity and reconstruct parents")
        try await recoveredDB.deleteItemMetadata(ocId: childIdentity.ocId)
        let replacement = try await recoveredDB.mergeServerMetadata(ItemMetadata(from: movedChild, parentOcId: folderIdentity.ocId))
        precondition(replacement.ocId != childIdentity.ocId, "Observed deletion must retire identity before path reuse")
        let unmatched = try await recoveredDB.resolveItem(identifier: "local:missing", webdav: server)
        precondition(unmatched == nil, "Unrelated id-less resources must never match by etag")
        let cold = try ItemDatabase(containerURL: container, domainIdentifier: "cold")
        await server.configure(["/": [root, a], "/a": [a, remote("server-id", path: "/a/file")]])
        let restored = try await cold.resolveItem(identifier: "server-id", webdav: server)
        precondition(restored?.ocId == "server-id" && restored?.parentOcId == "a",
                     "Unknown server IDs must be rediscovered with their actual parents")

        let largeDB = try ItemDatabase(containerURL: container, domainIdentifier: "large-enum")
        let largeExtension = FileProviderExtension()
        largeExtension.database = largeDB
        largeExtension.webdavClient = server
        let largeEnumerator = FileProviderEnumerator(enumeratedItemIdentifier: .rootContainer, domain: domain, fpExtension: largeExtension)
        let initialLargeAnchor = try await largeDB.currentSyncAnchor()
        let many = (0..<1203).map { remote("large-\($0)", path: "/large-\($0)") }
        await server.configure(["/": [root] + many])
        var page = NSFileProviderPage(NSFileProviderPage.initialPageSortedByName as Data)
        var itemIDs = Set<String>()
        var pageCount = 0
        let beforeRequests = await server.requestCount()
        while true {
            let batch = await items(largeEnumerator, page: page)
            precondition(batch.error == nil && batch.items.count <= 500)
            itemIDs.formUnion(batch.items.map { $0.itemIdentifier.rawValue })
            pageCount += 1
            guard let next = batch.next else { break }
            page = next
        }
        let afterRequests = await server.requestCount()
        precondition(itemIDs.count == 1203 && pageCount == 3 && afterRequests - beforeRequests == 1,
                     "Continuation pages must not repeat the network refresh")
        var largeChangeAnchor = initialLargeAnchor
        var changedIDs = Set<String>()
        var changePageCount = 0
        while true {
            let batch = await changes(largeEnumerator, since: largeChangeAnchor)
            precondition(batch.error == nil && batch.updated.count + batch.deleted.count <= 500)
            changedIDs.formUnion(batch.updated.map { $0.itemIdentifier.rawValue })
            largeChangeAnchor = batch.anchor!.rawValue
            changePageCount += 1
            if !batch.moreComing { break }
        }
        precondition(changedIDs.count == 1203 && changePageCount == 3,
                     "Change observers must receive all changes through bounded pages")
        let badPage = await items(largeEnumerator, page: NSFileProviderPage(Data("invalid".utf8)))
        precondition((badPage.error as NSError?)?.code == NSFileProviderError.pageExpired.rawValue)
        let cancelEnumerator = FileProviderEnumerator(enumeratedItemIdentifier: .rootContainer, domain: domain, fpExtension: largeExtension)
        await server.delayRequests(10_000_000_000)
        let pending = Task { await items(cancelEnumerator, page: NSFileProviderPage(NSFileProviderPage.initialPageSortedByName as Data)) }
        try await Task.sleep(nanoseconds: 30_000_000)
        cancelEnumerator.invalidate()
        let cancelled = await pending.value
        precondition((cancelled.error as NSError?)?.code == CocoaError.userCancelled.rawValue && cancelled.items.isEmpty,
                     "Invalidation must cancel network enumeration without publishing partial items")
        await server.delayRequests(0)
        largeExtension.isAuthenticated = false
        let unauthenticatedEnumerator = FileProviderEnumerator(enumeratedItemIdentifier: .rootContainer, domain: domain, fpExtension: largeExtension)
        let authFailure = await items(unauthenticatedEnumerator, page: NSFileProviderPage(NSFileProviderPage.initialPageSortedByName as Data))
        precondition((authFailure.error as NSError?)?.code == NSFileProviderError.notAuthenticated.rawValue,
                     "Missing authentication must fail immediately instead of polling")
        let recoveryDB = try ItemDatabase(containerURL: container, domainIdentifier: "status-recovery")
        let recoveryStatus = FileProviderSyncStatus(domainIdentifier: "status-recovery")
        let removedFolder = try await recoveryDB.mergeServerMetadata(ItemMetadata(from: remote("removed-folder", path: "/removed", directory: true), parentOcId: ItemDatabase.rootContainerId))
        _ = try await recoveryDB.mergeServerMetadata(ItemMetadata(from: remote("removed-child", path: "/removed/child"), parentOcId: removedFolder.ocId))
        _ = try await recoveryDB.mergeServerMetadata(ItemMetadata(from: remote("moved-child", path: "/removed/moved"), parentOcId: removedFolder.ocId))
        for key in ["modify:removed-child", "download:moved-child", "create:removed-folder/uncreated"] {
            recoveryStatus.finish(recoveryStatus.begin(.upload, key: key), error: NSFileProviderError(.insufficientQuota))
        }
        let scope = await recoveryStatus.removalScope(metadata: removedFolder, database: recoveryDB)
        _ = try await recoveryDB.mergeServerMetadata(ItemMetadata(from: remote("moved-child", path: "/safe-moved"), parentOcId: ItemDatabase.rootContainerId))
        try await recoveryDB.deleteDirectoryAndSubdirectories(ocId: removedFolder.ocId)
        await recoveryStatus.retireRemovedItems(scope, database: recoveryDB)
        precondition(recoveryStatus.snapshot(isAuthenticated: true)["errorCount"] as! Int == 1,
                     "Confirmed subtree deletion retires descendant/create failures but preserves a concurrently moved child's failure")

        print("FileProvider enumerator regression tests passed")
    }
}
