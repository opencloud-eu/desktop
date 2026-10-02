import Foundation
import FileProvider

private final class MockDAVProtocol: URLProtocol {
    static var requests: [URLRequest] = []
    static var handler: ((URLRequest) -> (Int, Data))!
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        Self.requests.append(request)
        let (status, data) = Self.handler(request)
        let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}

@main struct WebDAVRegressionTests {
    static func response(_ href: String, etag: String = "&quot;version-1&quot;", extra: String = "") -> String {
        """
        <d:response><d:href>\(href)</d:href><d:propstat><d:prop>
        <d:getetag>\(etag)</d:getetag><d:getcontentlength>0</d:getcontentlength>
        </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\(extra)</d:response>
        """
    }
    static func document(_ responses: String) -> Data {
        Data("<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\">\(responses)</d:multistatus>".utf8)
    }
    static func check(_ condition: @autoclosure () -> Bool, _ message: String) {
        precondition(condition(), message)
    }
    static func main() async throws {
        check(FileProviderItem.rootContainer().itemVersion.metadataVersion == FileProviderItem.rootContainer().itemVersion.metadataVersion,
              "root metadata version must remain stable between calls")
        check(FileProviderItem.trashContainer().itemVersion.metadataVersion == FileProviderItem.trashContainer().itemVersion.metadataVersion,
              "trash metadata version must remain stable between calls")
        check(FileProviderItem.trashContainer().fileSystemFlags.contains(.userWritable),
              "native restore and purge require a writable trash container")
        check(!FileProviderItem.rootContainer(supportsCreating: false).capabilities.contains(.allowsAddingSubItems),
              "virtual Shares root cannot create arbitrary files or folders")
        check(FileProviderItem.rootContainer().capabilities.contains(.allowsAddingSubItems),
              "ordinary writable roots continue accepting local uploads")
        check(!FileProviderItem.rootContainer().capabilities.contains(.allowsDeleting)
              && !FileProviderItem.rootContainer().capabilities.contains(.allowsRenaming),
              "special root containers cannot be deleted or renamed")
        let deniedRead = fileProviderError(WebDAVError.permissionDenied) as NSError
        check(fileProviderErrorDiagnostic(WebDAVError.httpError(statusCode: 503, message: "secret URL")) == "dav-http-503",
              "public diagnostics never expose HTTP response messages")
        check(fileProviderErrorDiagnostic(WebDAVError.parseError("private filename")) == "dav-invalid-multistatus",
              "public diagnostics never expose parser input")
        let deniedWrite = fileProviderError(WebDAVError.permissionDenied, isWrite: true) as NSError
        check(deniedRead.domain == NSCocoaErrorDomain && deniedRead.code == CocoaError.fileReadNoPermission.rawValue,
              "read permissions use Cocoa read error")
        check(deniedWrite.domain == NSCocoaErrorDomain && deniedWrite.code == CocoaError.fileWriteNoPermission.rawValue,
              "write permissions use Cocoa write error")
        check((fileProviderError(WebDAVError.httpError(statusCode: 507, message: nil)) as NSError).code == NSFileProviderError.insufficientQuota.rawValue,
              "quota remains distinct from permissions")
        check((fileProviderError(WebDAVError.notAuthenticated) as NSError).code == NSFileProviderError.notAuthenticated.rawValue,
              "authentication remains distinct from permissions")
        let identifier = WebDAVItem.generateIdentifier(from: "/folder/literal%20 name.txt")
        check(identifier.hasPrefix("path:"), "fallback IDs are explicit")
        check(WebDAVItem.path(fromIdentifier: identifier) == "/folder/literal%20 name.txt", "path IDs round trip")
        check(WebDAVItem.path(fromIdentifier: String(identifier.dropFirst(5))) == "/folder/literal%20 name.txt", "old path IDs remain readable")
        check(WebDAVItem.path(fromIdentifier: "fileid:12345") == nil, "server IDs are not decoded as paths")
        let base = URL(string: "https://example.test/remote.php/webdav/")!
        let parser = WebDAVXMLParser(baseURL: base)
        let failedProperties = """
        <d:propstat><d:prop><d:getetag>incorrect</d:getetag><d:resourcetype><d:collection/></d:resourcetype></d:prop>
        <d:status>HTTP/1.1 404 Not Found</d:status></d:propstat>
        """
        let items = parser.parse(data: document(response("/remote.php/webdav/literal%2520name.txt", extra: failedProperties)))!
        check(items.count == 1, "parse one resource")
        check(items[0].filename == "literal%20name.txt", "decode literal percent filenames only once")
        check(items[0].etag == "\"version-1\"", "preserve HTTP entity-tag quotes")
        check(!items[0].isDirectory, "ignore properties from failed propstat")
        let fileIDResponse = "<d:propstat><d:prop><oc:fileid>12345</oc:fileid></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>"
        let fileIDItem = parser.parse(data: document(response("/remote.php/webdav/file.txt", extra: fileIDResponse)))!.first!
        check(fileIDItem.ocId == "fileid:12345", "server fileid supplies stable identity when oc:id is absent")
        let absolute = parser.parse(data: document(response("https://example.test/remote.php/webdav/literal%252Fname.txt")))!
        check(absolute[0].filename == "literal%2Fname.txt", "absolute href percent decoding")
        check(parser.parse(data: Data("<html/>".utf8)) == nil, "reject non-WebDAV responses")
        let failedItem = "<d:response><d:href>/remote.php/webdav/missing</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>"
        check(parser.parse(data: document(response("/remote.php/webdav/") + failedItem)) == nil,
              "partial listing must not be treated as server deletions")

        let fileMetadata = ItemMetadata(from: items[0], parentOcId: "parent")
        let fileItem = FileProviderItem(metadata: fileMetadata, parentItemIdentifier: NSFileProviderItemIdentifier("parent"))
        check(fileItem.documentSize?.int64Value == 0, "zero-byte files have a known document size")
        let movedItem = FileProviderItem(metadata: fileMetadata, parentItemIdentifier: NSFileProviderItemIdentifier("other-parent"))
        check(fileItem.itemVersion.contentVersion == movedItem.itemVersion.contentVersion, "moving preserves content version")
        check(fileItem.itemVersion.metadataVersion != movedItem.itemVersion.metadataVersion, "moving changes metadata version")
        check(fileItem.itemVersion.metadataVersion.count <= 128, "metadata versions fit the FileProvider size limit")
        var refreshedMetadata = fileMetadata
        refreshedMetadata.syncTime = fileMetadata.syncTime.addingTimeInterval(60)
        let refreshedItem = FileProviderItem(metadata: refreshedMetadata, parentItemIdentifier: NSFileProviderItemIdentifier("parent"))
        check(fileItem.creationDate == nil && fileItem.contentModificationDate == nil, "missing server dates stay unknown")
        check(fileItem.itemVersion.metadataVersion == refreshedItem.itemVersion.metadataVersion,
              "refreshing undated metadata must not change its version")
        refreshedMetadata.finderMetadata.tagData = Data("tag blob".utf8)
        refreshedMetadata.finderMetadata.fileSystemFlags = NSFileProviderFileSystemFlags.hidden.rawValue
        let taggedItem = FileProviderItem(metadata: refreshedMetadata, parentItemIdentifier: NSFileProviderItemIdentifier("parent"))
        check(taggedItem.tagData == Data("tag blob".utf8) && taggedItem.fileSystemFlags.contains(.hidden), "Finder metadata is returned intact")
        check(taggedItem.itemVersion.metadataVersion != fileItem.itemVersion.metadataVersion, "Finder metadata changes invalidate the item version")
        check(fileItem.contentPolicy == .inherited && FileProviderItem.rootContainer().contentPolicy == .downloadLazily,
              "native content policies inherit on-demand defaults")
        var trashedMetadata = fileMetadata
        trashedMetadata.finderMetadata.trash = TrashMetadata(key: "deleted-file", originalPath: fileMetadata.remotePath,
                                                             originalParentOcId: "parent", deletionDate: nil)
        let trashedItem = FileProviderItem(metadata: trashedMetadata, parentItemIdentifier: .trashContainer)
        check(trashedItem.capabilities.contains(.allowsRenaming) && trashedItem.capabilities.contains(.allowsReparenting),
              "restoring under a new name requires both native capabilities")
        check(!trashedItem.capabilities.contains(.allowsWriting), "trash does not advertise unsupported content edits")

        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [MockDAVProtocol.self]
        let client = WebDAVClient(serverURL: URL(string: "https://example.test")!, username: "user", password: "test", sessionConfiguration: config)
        let rootMatches = await client.isRootPath("/remote.php/webdav/")
        let nestedMatches = await client.isRootPath("/remote.php/webdav/nested")
        check(rootMatches && !nestedMatches, "only DAV root may be assigned root-container identity")
        MockDAVProtocol.handler = { _ in
            (207, document(response("/remote.php/webdav/child.txt") + response("/remote.php/webdav/")))
        }
        let listing = try await client.listDirectory(path: "/")
        check(listing.first?.remotePath == "/remote.php/webdav/", "resource itself need not be first on the wire")
        MockDAVProtocol.handler = { _ in (207, document(response("/remote.php/webdav/child.txt"))) }
        do {
            _ = try await client.listDirectory(path: "/")
            preconditionFailure("missing directory response must fail")
        } catch WebDAVError.parseError { }

        let trailingSlashClient = WebDAVClient(serverURL: URL(string: "https://example.test")!, davPath: "/review/",
                                               username: "user", password: "test", sessionConfiguration: config)
        MockDAVProtocol.requests = []
        MockDAVProtocol.handler = { request in (207, document(response(request.url!.path))) }
        _ = try await trailingSlashClient.listDirectory(path: "/")
        check(MockDAVProtocol.requests.last?.url?.absoluteString == "https://example.test/review/", "DAV root with trailing slash must not produce a double slash")
        _ = try await trailingSlashClient.listDirectory(path: "child.txt")
        check(MockDAVProtocol.requests.last?.url?.path == "/review/child.txt", "relative child joins DAV root once")
        _ = try await trailingSlashClient.listDirectory(path: "/review/child.txt")
        check(MockDAVProtocol.requests.last?.url?.path == "/review/child.txt", "absolute DAV child keeps its path")
        _ = try await trailingSlashClient.listDirectory(path: "/review-other/child.txt")
        check(MockDAVProtocol.requests.last?.url?.path == "/review/review-other/child.txt", "DAV prefix matching respects path component boundaries")
        let serverRootClient = WebDAVClient(serverURL: URL(string: "https://example.test")!, davPath: "/",
                                            username: "user", password: "test", sessionConfiguration: config)
        _ = try await serverRootClient.listDirectory(path: "child.txt")
        check(MockDAVProtocol.requests.last?.url?.path == "/child.txt", "DAV rooted at server handles relative child")

        let upload = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try Data("local edit".utf8).write(to: upload)
        defer { try? FileManager.default.removeItem(at: upload) }
        MockDAVProtocol.requests = []
        MockDAVProtocol.handler = { _ in (412, Data()) }
        do {
            _ = try await client.uploadFile(from: upload, to: "/file.txt", ifMatchEtag: "old-version",
                                            modificationDate: Date(timeIntervalSince1970: 1234567890))
            preconditionFailure("conflicting edit must fail")
        } catch WebDAVError.conflict { }
        check(MockDAVProtocol.requests.count == 1, "never retry a conflicting upload unconditionally")
        check(MockDAVProtocol.requests[0].value(forHTTPHeaderField: "If-Match") == "\"old-version\"", "quote legacy cached ETags")
        check(MockDAVProtocol.requests[0].value(forHTTPHeaderField: "Content-Type") == "text/plain", "upload media type uses destination filename rather than temporary contents URL")
        check(MockDAVProtocol.requests[0].value(forHTTPHeaderField: "X-OC-Mtime") == "1234567890", "upload preserves the requested native modification time")
        do {
            _ = try await client.uploadFile(from: upload, to: "/file.txt", ifNoneMatch: true)
            preconditionFailure("creation collision must fail")
        } catch WebDAVError.conflict { }
        check(MockDAVProtocol.requests.last?.value(forHTTPHeaderField: "If-None-Match") == "*", "new files cannot overwrite a remote collision")
        MockDAVProtocol.requests = []
        do {
            try await client.deleteItem(at: "/file.txt", ifMatchEtag: "old-version")
            preconditionFailure("conflicting delete must fail")
        } catch WebDAVError.conflict { }
        check(MockDAVProtocol.requests.count == 1, "a rejected deletion must not retry")
        check(MockDAVProtocol.requests[0].value(forHTTPHeaderField: "If-Match") == "\"old-version\"", "deletion checks the version on disk")
        for method in ["PUT", "MKCOL"] {
            MockDAVProtocol.requests = []
            MockDAVProtocol.handler = { request in
                if request.httpMethod == method { return (201, Data()) }
                let reads = MockDAVProtocol.requests.filter { $0.httpMethod == "PROPFIND" }.count
                return reads == 1 ? (503, Data()) : (207, document(response("/remote.php/webdav/new-item")))
            }
            if method == "PUT" {
                _ = try await client.uploadFile(from: upload, to: "/new-item", ifNoneMatch: true)
            } else {
                _ = try await client.createDirectory(at: "/new-item")
            }
            check(MockDAVProtocol.requests.filter { $0.httpMethod == method }.count == 1,
                  "successful mutation must not repeat when its metadata read fails")
            check(MockDAVProtocol.requests.filter { $0.httpMethod == "PROPFIND" }.count == 2,
                  "retry only the metadata read after successful mutation")
        }

        let movedResponse = document(response("/remote.php/webdav/renamed.txt", extra:
            "<d:propstat><d:prop><oc:id>stable-server-id</oc:id></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>"))
        MockDAVProtocol.requests = []
        MockDAVProtocol.handler = { request in
            if request.httpMethod == "MOVE" { return (201, Data()) }
            let reads = MockDAVProtocol.requests.filter { $0.httpMethod == "PROPFIND" }.count
            return reads == 1 ? (503, Data()) : (207, movedResponse)
        }
        let moved = try await client.moveItem(from: "/source.txt", to: "/renamed.txt", expectedIdentifier: "stable-server-id")
        check(moved?.ocId == "stable-server-id", "recover metadata after a successful MOVE")
        check(MockDAVProtocol.requests.filter { $0.httpMethod == "MOVE" }.count == 1,
              "metadata retry must not issue another MOVE")
        check(MockDAVProtocol.requests.filter { $0.httpMethod == "PROPFIND" }.count == 2,
              "retry the failed metadata read")

        MockDAVProtocol.handler = { request in
            request.httpMethod == "MOVE" ? (404, Data()) : (207, movedResponse)
        }
        let recovered = try await client.moveItem(from: "/source.txt", to: "/renamed.txt", expectedIdentifier: "stable-server-id")
        check(recovered?.ocId == "stable-server-id", "retry after lost MOVE response identifies destination safely")
        do {
            _ = try await client.moveItem(from: "/source.txt", to: "/renamed.txt", expectedIdentifier: "different-server-id")
            preconditionFailure("unrelated destination must not count as a completed MOVE")
        } catch WebDAVError.conflict { }
        do {
            let fallback = WebDAVItem.generateIdentifier(from: "/remote.php/webdav/source.txt")
            _ = try await client.moveItem(from: "/source.txt", to: "/renamed.txt", expectedIdentifier: fallback)
            preconditionFailure("path-derived identity cannot prove prior MOVE success")
        } catch WebDAVError.fileNotFound { }
        MockDAVProtocol.requests = []
        MockDAVProtocol.handler = { request in
            if request.httpMethod == "PUT" { return (201, Data()) }
            return (207, document(response(request.url!.path, extra:
                "<d:propstat><d:prop><oc:id>conflict-copy-id</oc:id></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>")))
        }
        let conflictCopy = try await client.uploadConflictCopy(from: upload, directory: "/", filename: "report.txt")
        check(conflictCopy.ocId == "conflict-copy-id", "conflicting edit gets a separate server identity")
        check(conflictCopy.filename.contains(" (conflict ") && conflictCopy.filename.hasSuffix(".txt"),
              "conflict copy keeps a recognizable filename and extension")
        let copyWrites = MockDAVProtocol.requests.filter { $0.httpMethod == "PUT" }
        check(copyWrites.count == 1 && copyWrites[0].url?.path != "/remote.php/webdav/report.txt", "conflict recovery never writes original")
        check(copyWrites[0].value(forHTTPHeaderField: "If-None-Match") == "*", "conflict recovery cannot overwrite another file")
        let longCopy = try await client.uploadConflictCopy(from: upload, directory: "/", filename: String(repeating: "é", count: 120) + ".txt")
        check(longCopy.filename.utf8.count <= 255 && longCopy.filename.hasSuffix(".txt"),
              "conflict suffix respects filesystem byte limit for Unicode filenames")
        print("FileProvider WebDAV regression tests passed")
    }
}
