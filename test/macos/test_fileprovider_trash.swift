import Foundation
import FileProvider
import SQLite3

private final class TrashProtocol: URLProtocol {
    static var handler: ((URLRequest) -> (Int, Data))!
    static var pauseNext = false
    static var pendingResponse: (() -> Void)?
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let (code, data) = Self.handler(request)
        let deliver = {
            self.client?.urlProtocol(self, didReceive: HTTPURLResponse(url: self.request.url!, statusCode: code, httpVersion: nil, headerFields: nil)!, cacheStoragePolicy: .notAllowed)
            self.client?.urlProtocol(self, didLoad: data)
            self.client?.urlProtocolDidFinishLoading(self)
        }
        if Self.pauseNext { Self.pauseNext = false; Self.pendingResponse = deliver }
        else { deliver() }
    }
    override func stopLoading() {}
}

@main struct TrashTests {
    static func listing(_ child: Bool, key: String = "opaque", original: String = "file%20.txt") -> Data {
        let root = "/prefix/dav/spaces/trash-bin/space"
        let entry = child ? """
        <d:response><d:href>\(root)/\(key)</d:href><d:propstat><d:prop>
        <d:resourcetype/><d:getcontentlength>3</d:getcontentlength>
        <oc:trashbin-original-location>\(original)</oc:trashbin-original-location>
        <oc:trashbin-delete-timestamp>1234</oc:trashbin-delete-timestamp>
        </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>
        <d:propstat><d:prop><oc:trashbin-original-location>wrong</oc:trashbin-original-location></d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat></d:response>
        """ : ""
        return Data("<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\"><d:response><d:href>\(root)/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>\(entry)</d:multistatus>".utf8)
    }
    static func remote(_ path: String) -> WebDAVItem {
        WebDAVItem(ocId: "oc-identity", fileId: "storage$space!opaque", remotePath: path, filename: "file%20.txt", etag: "v1", contentType: "text/plain", size: 3, lastModified: nil, creationDate: nil, isDirectory: false, permissions: "RDNVW", ownerId: "", ownerDisplayName: "")
    }
    static func main() async throws {
        let duplicateNames = ["same.txt", "SAME.txt", "same.txt"].enumerated().map { index, name in
            WebDAVTrashItem(key: "key-\(index)", originalPath: "folder-\(index)/" + name, filename: name,
                            isDirectory: false, size: 0, deletionDate: nil)
        }
        let uniqueNames = FileProviderTrash.displayNames(for: duplicateNames)
        precondition(Set(uniqueNames.values.map { $0.lowercased() }).count == 3)
        precondition(uniqueNames == FileProviderTrash.displayNames(for: duplicateNames.reversed()), "Trash collision names must not depend on server response order")
        precondition(uniqueNames.values.allSatisfy { $0.utf8.count <= 255 && $0.lowercased().hasSuffix(".txt") })
        let literalCollision = WebDAVTrashItem(key: "key--1", originalPath: "literal", filename: uniqueNames["key-1"]!, isDirectory: false, size: 0, deletionDate: nil)
        let unicodeNames = ["cafe\u{301}.txt", "café.txt"].enumerated().map { index, name in
            WebDAVTrashItem(key: "unicode-\(index)", originalPath: name, filename: name, isDirectory: false, size: 0, deletionDate: nil)
        }
        let collisionEntries = duplicateNames + [literalCollision] + unicodeNames
        let allNames = FileProviderTrash.displayNames(for: collisionEntries)
        precondition(Set(allNames.values.map { $0.precomposedStringWithCanonicalMapping.lowercased() }).count == collisionEntries.count,
                     "Generated suffixes must avoid literal basenames and canonically equivalent Unicode names")
        precondition(allNames == FileProviderTrash.displayNames(for: collisionEntries.reversed()))
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [TrashProtocol.self]
        let server = URL(string: "https://example.test")!
        let client = WebDAVClient(serverURL: server, davPath: "/prefix/dav/spaces/space", username: "u", password: "p", sessionConfiguration: config)
        let generic = WebDAVClient(serverURL: server, davPath: "/dav/files/user", username: "u", password: "p", sessionConfiguration: config)
        let support = await client.supportsTrash
        let genericSupport = await generic.supportsTrash
        precondition(support && !genericSupport, "Trash advertised only for space domains")
        let sharesID = "a0ca6a90-a365-4782-871e-d44447bbc668$a0ca6a90-a365-4782-871e-d44447bbc668"
        precondition(!WebDAVClient.supportsTrashPath("/dav/spaces/" + sharesID))
        precondition(!WebDAVClient.supportsTrashPath("/deployment/dav/spaces/" + sharesID + "/"))
        precondition(!WebDAVClient.supportsTrashPath("/dav/spaces/" + sharesID + "!received-share"))
        precondition(WebDAVClient.supportsTrashPath("/deployment/dav/spaces/actual-storage$actual-space/"))
        let shares = WebDAVClient(serverURL: server, davPath: "/dav/spaces/" + sharesID, username: "u", password: "p", sessionConfiguration: config)
        TrashProtocol.handler = { _ in preconditionFailure("Virtual Shares must never probe unsupported recycle API") }
        do { _ = try await shares.listTrash(); preconditionFailure("Virtual Shares advertised trash") }
        catch let error as CocoaError { precondition(error.code == .featureUnsupported) }
        let root = URL(string: "https://example.test/prefix/dav/spaces/trash-bin/space")!
        let parsed = try WebDAVClient.parseTrash(data: listing(true), endpoint: root, root: root)
        precondition(parsed[0].originalPath == "file%20.txt" && parsed[0].deletionDate == Date(timeIntervalSince1970: 1234))
        let foreign = String(decoding: listing(true), as: UTF8.self).replacingOccurrences(of: "/trash-bin/space/opaque", with: "/trash-bin/foreign/opaque")
        do { _ = try WebDAVClient.parseTrash(data: Data(foreign.utf8), endpoint: root, root: root); preconditionFailure("Cross-space listing accepted") }
        catch WebDAVError.parseError {}
        do { _ = try WebDAVClient.parseTrash(data: listing(true, original: "../escape.txt"), endpoint: root, root: root); preconditionFailure("Trash original path traversal accepted") }
        catch WebDAVError.parseError {}
        let invalidFilename = String(decoding: listing(true), as: UTF8.self).replacingOccurrences(of: "<oc:trashbin-delete-timestamp>", with: "<oc:trashbin-original-filename>../escape.txt</oc:trashbin-original-filename><oc:trashbin-delete-timestamp>")
        do { _ = try WebDAVClient.parseTrash(data: Data(invalidFilename.utf8), endpoint: root, root: root); preconditionFailure("Trash filename traversal accepted") }
        catch WebDAVError.parseError {}
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: temp) }
        let db = try ItemDatabase(containerURL: temp, domainIdentifier: "trash")
        let original = try await db.mergeServerMetadata(ItemMetadata(from: remote("/prefix/dav/spaces/space/file%20.txt"), parentOcId: ItemDatabase.rootContainerId))
        var privateData = FinderMetadata()
        privateData.tagData = Data([1, 2])
        _ = try await db.updateLocalMetadata(ocId: original.ocId, metadata: privateData, fields: [.tagData])
        var deleted = false
        var purged = false
        TrashProtocol.handler = { request in
            let path = request.url!.path
            if request.httpMethod == "PROPFIND" && path.contains("/trash-bin/") { return (207, listing(deleted && !purged)) }
            if request.httpMethod == "DELETE" {
                if path.contains("/trash-bin/") { purged = true }
                else { precondition(request.value(forHTTPHeaderField: "If-Match") == "\"v1\""); deleted = true }
                return (204, Data())
            }
            return (404, Data())
        }
        let trashed = try await FileProviderTrash.trash(metadata: original, webdav: client, database: db)
        precondition(trashed.ocId == original.ocId && trashed.isTrashed && trashed.finderMetadata.tagData == privateData.tagData)
        precondition(trashed.parentOcId == NSFileProviderItemIdentifier.trashContainer.rawValue)
        let retry = try await FileProviderTrash.trash(metadata: trashed, webdav: client, database: db)
        precondition(retry.ocId == original.ocId)
        TrashProtocol.handler = { request in
            if request.httpMethod == "MOVE" {
                precondition(request.value(forHTTPHeaderField: "Overwrite") == "F", "Restore cannot overwrite existing data")
                precondition(request.value(forHTTPHeaderField: "Destination") == "https://example.test/prefix/dav/spaces/space/restored.txt")
                return (412, Data())
            }
            return (404, Data())
        }
        do { _ = try await FileProviderTrash.restore(metadata: trashed, to: "/restored.txt", parentOcId: ItemDatabase.rootContainerId, webdav: client, database: db); preconditionFailure("Restore conflict overwritten") }
        catch WebDAVError.conflict {}
        let afterConflict = await db.itemMetadata(ocId: original.ocId)
        precondition(afterConflict?.isTrashed == true)
        TrashProtocol.handler = { request in
            if request.httpMethod == "MOVE" { return (201, Data()) }
            let props = "<oc:id>oc-identity</oc:id><oc:fileid>storage$space!opaque</oc:fileid><d:getetag>v1</d:getetag>"
            return (207, Data("<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\"><d:response><d:href>/prefix/dav/spaces/space/restored.txt</d:href><d:propstat><d:prop>\(props)</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>".utf8))
        }
        let restored = try await FileProviderTrash.restore(metadata: trashed, to: "/restored.txt", parentOcId: ItemDatabase.rootContainerId, webdav: client, database: db)
        precondition(restored.ocId == original.ocId && !restored.isTrashed && restored.finderMetadata.tagData == privateData.tagData)
        // An externally deleted uncached resource receives a stable identity through restoration.
        TrashProtocol.handler = { _ in (207, listing(true, key: "external")) }
        let external = try await FileProviderTrash.refresh(webdav: client, database: db).first!
        let serverVersion = WebDAVItem(ocId: "fileid:space!external", fileId: "space!external", remotePath: "/external", filename: "external", etag: "", contentType: "", size: 0, lastModified: nil, creationDate: nil, isDirectory: false, permissions: "", ownerId: "", ownerDisplayName: "")
        let accepted = try await db.mergeServerMetadata(ItemMetadata(from: serverVersion), preservingIdentifier: external.ocId)
        let subsequent = try await db.mergeServerMetadata(ItemMetadata(from: serverVersion))
        precondition(accepted.ocId == external.ocId && subsequent.ocId == external.ocId, "Restored external trash identity must survive later listing")
        TrashProtocol.handler = { request in precondition(request.httpMethod == "DELETE" && request.url!.path.contains("/trash-bin/")); return (204, Data()) }
        try await FileProviderTrash.purge(metadata: external, webdav: client, database: db)
        let afterPurge = await db.itemMetadata(ocId: external.ocId)
        precondition(afterPurge == nil)
        let directory = WebDAVItem(ocId: "dir-provider", fileId: "space!dir", remotePath: "/prefix/dav/spaces/space/folder", filename: "folder", etag: "dir-v1", contentType: "", size: 0, lastModified: nil, creationDate: nil, isDirectory: true, permissions: "RDNVW", ownerId: "", ownerDisplayName: "")
        let child = WebDAVItem(ocId: "child-provider", fileId: "space!child", remotePath: directory.remotePath + "/child.txt", filename: "child.txt", etag: "v1", contentType: "text/plain", size: 3, lastModified: nil, creationDate: nil, isDirectory: false, permissions: "RDNVW", ownerId: "", ownerDisplayName: "")
        let folder = try await db.mergeServerMetadata(ItemMetadata(from: directory, parentOcId: ItemDatabase.rootContainerId))
        _ = try await db.mergeServerMetadata(ItemMetadata(from: child, parentOcId: folder.ocId))
        try await db.setDownloaded(ocId: child.ocId, downloaded: true)
        var folderDeleted = false
        func folderListing() -> Data {
            let plain = String(decoding: listing(folderDeleted, key: "dir", original: "folder"), as: UTF8.self)
            return Data(plain.replacingOccurrences(of: "<d:resourcetype/>", with: "<d:resourcetype><d:collection/></d:resourcetype>").utf8)
        }
        TrashProtocol.handler = { request in
            if request.httpMethod == "DELETE" { folderDeleted = true; return (204, Data()) }
            return (207, folderListing())
        }
        let trashedFolder = try await FileProviderTrash.trash(metadata: folder, webdav: client, database: db)
        let trashedChild = await db.itemMetadata(ocId: child.ocId)!
        precondition(trashedChild.isTrashed && trashedChild.isDownloaded && trashedChild.finderMetadata.trash?.key == "dir/child.txt")
        precondition(trashedChild.finderMetadata.trash?.originalPath == child.remotePath && trashedChild.parentOcId == folder.ocId)
        var restoreCalls = 0
        TrashProtocol.handler = { request in
            if request.httpMethod == "MOVE" { restoreCalls += 1; return (restoreCalls == 1 ? 201 : 404, Data()) }
            let props = "<oc:id>dir-provider</oc:id><oc:fileid>space!dir</oc:fileid><d:getetag>dir-v1</d:getetag><d:resourcetype><d:collection/></d:resourcetype>"
            return (207, Data("<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\"><d:response><d:href>/prefix/dav/spaces/space/restored-folder</d:href><d:propstat><d:prop>\(props)</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>".utf8))
        }
        _ = try await FileProviderTrash.restore(metadata: trashedFolder, to: "/restored-folder", parentOcId: ItemDatabase.rootContainerId, webdav: client, database: db)
        let restoredChild = await db.itemMetadata(ocId: child.ocId)!
        precondition(!restoredChild.isTrashed && restoredChild.isDownloaded && restoredChild.remotePath == "/prefix/dav/spaces/space/restored-folder/child.txt")
        let repeatedRestore = try await FileProviderTrash.restore(metadata: trashedFolder, to: "/restored-folder", parentOcId: ItemDatabase.rootContainerId, webdav: client, database: db)
        precondition(repeatedRestore.ocId == folder.ocId, "Retry after committed restore must reconcile only stable identity")
        // Cache loss must recover nested trash IDs and ancestry from durable state.
        _ = try await db.storeTrashItem(trashedFolder, trash: trashedFolder.finderMetadata.trash)
        try await db.setDescendantTrashState(of: trashedFolder, trash: trashedFolder.finderMetadata.trash)
        var cache: OpaquePointer?
        precondition(sqlite3_open(temp.appendingPathComponent("FileProvider/items-trash.sqlite").path, &cache) == SQLITE_OK)
        precondition(sqlite3_exec(cache, "DELETE FROM items", nil, nil, nil) == SQLITE_OK)
        sqlite3_close(cache)
        TrashProtocol.handler = { request in
            if request.url!.path.hasSuffix("/dir") {
                let text = String(decoding: listing(true, key: "dir/child.txt", original: "folder/child.txt"), as: UTF8.self)
                    .replacingOccurrences(of: "<d:href>/prefix/dav/spaces/trash-bin/space/</d:href>", with: "<d:href>/prefix/dav/spaces/trash-bin/space/dir/</d:href>")
                return (207, Data(text.utf8))
            }
            return (207, folderListing())
        }
        let recoveredChild = try await FileProviderTrash.resolve(identifier: child.ocId, webdav: client, database: db)
        precondition(recoveredChild?.ocId == child.ocId && recoveredChild?.parentOcId == folder.ocId)
        precondition(recoveredChild?.finderMetadata.trash?.originalParentOcId == folder.ocId)
        // Hold a trash response across a newer local restore, then a purge.
        for purge in [false, true] {
            var initialTrash = trashedFolder
            initialTrash.syncTime = Date()
            _ = try await db.storeTrashItem(initialTrash, trash: initialTrash.finderMetadata.trash)
            TrashProtocol.pendingResponse = nil
            TrashProtocol.pauseNext = true
            let inFlight = Task { try await FileProviderTrash.refresh(webdav: client, database: db) }
            for _ in 0..<100 {
                if TrashProtocol.pendingResponse != nil { break }
                try await Task.sleep(nanoseconds: 10_000_000)
            }
            precondition(TrashProtocol.pendingResponse != nil)
            if purge { try await db.deleteDirectoryAndSubdirectories(ocId: folder.ocId) }
            else {
                var live = folder
                live.syncTime = Date()
                _ = try await db.storeTrashItem(live, trash: nil)
            }
            TrashProtocol.pendingResponse?()
            TrashProtocol.pendingResponse = nil
            _ = try await inFlight.value
            let final = await db.itemMetadata(ocId: folder.ocId)
            precondition(purge ? final == nil : final?.isTrashed == false, "Stale trash listing must not undo a restore or purge")
            let trashChildren = await db.childItems(parentOcId: NSFileProviderItemIdentifier.trashContainer.rawValue)
            precondition(trashChildren.isEmpty, "Stale listing must not recreate a purged entry under a new ID")
        }
        print("FileProvider trash regression tests passed")
    }
}
