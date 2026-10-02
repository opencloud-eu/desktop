import Foundation
import FileProvider
import ImageIO
import CoreGraphics

final class FileProviderExtension: NSObject {
    var webdavClient: WebDAVClient?
    var database: ItemDatabase?
}

private final class SearchProtocol: URLProtocol {
    static var requests: [URLRequest] = []
    static var handler: ((URLRequest) -> (Int, Data))!
    static var stalled = false
    static var stopped = 0
    static var headers: [String: String] = [:]
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        Self.requests.append(request)
        if Self.stalled { return }
        let (status, data) = Self.handler(request)
        client?.urlProtocol(self, didReceive: HTTPURLResponse(url: request.url!, statusCode: status,
                                                            httpVersion: "HTTP/1.1", headerFields: Self.headers)!, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() { Self.stopped += 1 }
}

private final class SearchObserver: NSObject, NSFileProviderSearchEnumerationObserver {
    let maximumNumberOfResultsPerPage = 2
    var items: [any NSFileProviderSearchResult] = []
    let done: ([any NSFileProviderSearchResult], NSFileProviderPage?, Error?) -> Void
    init(done: @escaping ([any NSFileProviderSearchResult], NSFileProviderPage?, Error?) -> Void) { self.done = done }
    func didEnumerate(_ searchResults: [any NSFileProviderSearchResult]) {
        precondition(searchResults.count <= maximumNumberOfResultsPerPage)
        items += searchResults
    }
    func finishEnumerating(upTo nextPage: NSFileProviderPage?) { done(items, nextPage, nil) }
    func finishEnumeratingWithError(_ error: Error) { done(items, nil, error) }
}

@main struct SearchTests {
    static func page(_ enumerator: ProviderSearchEnumerator, _ page: NSFileProviderPage? = nil) async
        -> ([any NSFileProviderSearchResult], NSFileProviderPage?, Error?) {
        await withCheckedContinuation { continuation in
            enumerator.enumerateSearchResults(for: SearchObserver { continuation.resume(returning: ($0, $1, $2)) }, startingAt: page)
        }
    }
    static func body(_ request: URLRequest) -> String {
        if let data = request.httpBody { return String(decoding: data, as: UTF8.self) }
        guard let stream = request.httpBodyStream else { return "" }
        stream.open()
        defer { stream.close() }
        var bytes = [UInt8](repeating: 0, count: 8192)
        let count = stream.read(&bytes, maxLength: bytes.count)
        return count > 0 ? String(decoding: bytes.prefix(count), as: UTF8.self) : ""
    }
    static func response(_ path: String, id: String) -> String {
        "<d:response><d:href>\(path)</d:href><d:propstat><d:prop><oc:id>\(id)</oc:id><d:getetag>v1</d:getetag></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
    }
    static func main() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let database = try ItemDatabase(containerURL: directory, domainIdentifier: "search-test")
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [SearchProtocol.self]
        let client = WebDAVClient(serverURL: URL(string: "https://example.test")!, davPath: "/dav/spaces/a$b",
                                  username: "test", password: "fixture", sessionConfiguration: configuration)
        let sharesID = "a0ca6a90-a365-4782-871e-d44447bbc668$a0ca6a90-a365-4782-871e-d44447bbc668"
        let shares = WebDAVClient(serverURL: URL(string: "https://example.test")!, davPath: "/dav/spaces/" + sharesID,
                                 username: "test", password: "fixture", sessionConfiguration: configuration)
        SearchProtocol.handler = { _ in preconditionFailure("Synthetic Shares must not use an unsupported space search scope") }
        do { _ = try await shares.searchFiles(query: "x", limit: 1); preconditionFailure("Shares search unexpectedly supported") }
        catch let error as CocoaError { precondition(error.code == .featureUnsupported) }
        let responses = (0..<7).map { response("/dav/spaces/a$b/file-\($0).txt", id: "result-\($0)") }.joined()
            + response("/dav/spaces/a$bc/other.txt", id: "foreign")
            + response("/dav/spaces/a$b/file-0.txt", id: "result-0")
            + response("/dav/spaces/a$b/../other/escape.txt", id: "dot-escape")
        SearchProtocol.handler = { request in
            precondition(request.httpMethod == "REPORT", "Unexpected \(request.httpMethod ?? "nil") request for \(request.url!.path)")
            precondition(request.url!.path == "/dav/spaces/a$b")
            precondition(request.value(forHTTPHeaderField: "Authorization")?.hasPrefix("Basic ") == true)
            let text = body(request)
            precondition(!text.contains("scope:"), "Space scope belongs in the REPORT URL, not an invented query directive")
            precondition(text.contains("&amp;") && text.contains("&lt;"))
            precondition(text.contains("\\\""))
            return (207, Data("<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\">\(responses)</d:multistatus>".utf8))
        }
        let enumerator = ProviderSearchEnumerator(query: "a\" & <", desiredResults: 7, client: client, database: database)
        var cursor: NSFileProviderPage?
        var found: [any NSFileProviderSearchResult] = []
        repeat {
            let (items, next, error) = await page(enumerator, cursor)
            if let error { throw error }
            found += items
            cursor = next
        } while cursor != nil
        precondition(found.count == 7 && SearchProtocol.requests.count == 1)
        precondition(Set(found.map { $0.itemIdentifier.rawValue }).count == 7)
        for result in found {
            let metadata = await database.itemMetadata(ocId: result.itemIdentifier.rawValue)
            precondition(metadata?.parentOcId == ItemDatabase.rootContainerId)
        }
        // Use an exact shared timestamp to exercise the persisted clock boundary
        // without depending on how far apart two consecutive Date() calls land.
        let staleSearchStarted = Date(timeIntervalSince1970: 1_700_000_000)
        let newer = WebDAVItem(ocId: "result-0", fileId: "result-0", remotePath: "/dav/spaces/a$b/new-name.txt", filename: "new-name.txt", etag: "newer-version", contentType: "text/plain", size: 42, lastModified: nil, creationDate: nil, isDirectory: false, permissions: "", ownerId: "", ownerDisplayName: "")
        var newerMetadata = ItemMetadata(from: newer, parentOcId: ItemDatabase.rootContainerId)
        newerMetadata.syncTime = staleSearchStarted
        _ = try await database.mergeServerMetadata(newerMetadata)
        let stale = WebDAVItem(ocId: "result-0", fileId: "result-0", remotePath: "/dav/spaces/a$b/removed-parent/file-0.txt", filename: "file-0.txt", etag: "old-version", contentType: "text/plain", size: 1, lastModified: nil, creationDate: nil, isDirectory: false, permissions: "", ownerId: "", ownerDisplayName: "")
        let reconciled = try await database.mergeSearchResult(stale, webdav: client, fetchedAfter: staleSearchStarted)
        precondition(reconciled.etag == "newer-version" && reconciled.remotePath == newer.remotePath,
                     "Stale search must not overwrite a newer upload or move")
        let (_, _, badPage) = await page(enumerator, NSFileProviderPage(Data("999999".utf8)))
        precondition(badPage != nil)
        let unauthenticated = ProviderSearchEnumerator(query: "x", desiredResults: 1, client: nil, database: database)
        let (_, _, authError) = await page(unauthenticated)
        precondition((authError as NSError?)?.code == NSFileProviderError.notAuthenticated.rawValue)

        let png = Data(base64Encoded: "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=")!
        SearchProtocol.headers = ["Content-Type": "image/png"]
        SearchProtocol.handler = { request in
            let components = URLComponents(url: request.url!, resolvingAgainstBaseURL: false)!
            precondition(request.httpMethod == "GET" && components.path == "/dav/spaces/a$b/file-0.txt")
            precondition(components.queryItems?.contains(URLQueryItem(name: "preview", value: "1")) == true)
            precondition(components.queryItems?.contains(URLQueryItem(name: "x", value: "1024")) == true)
            return (200, png)
        }
        let thumbnail = try await client.thumbnail(remotePath: "/dav/spaces/a$b/file-0.txt", etag: "v1", width: 5000, height: 32)
        precondition(thumbnail == png)
        let oversizedPixels = Data(repeating: 255, count: 2048 * 4)
        let provider = CGDataProvider(data: oversizedPixels as CFData)!
        let oversizedImage = CGImage(width: 2048, height: 1, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: 2048 * 4, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
        let oversizedPNG = NSMutableData()
        let destination = CGImageDestinationCreateWithData(oversizedPNG, "public.png" as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, oversizedImage, nil)
        precondition(CGImageDestinationFinalize(destination))
        SearchProtocol.handler = { _ in (200, oversizedPNG as Data) }
        let oversizedThumbnail = try await client.thumbnail(remotePath: "/dav/spaces/a$b/file-0.txt", etag: "v1", width: 5000, height: 32)
        precondition(oversizedThumbnail == nil, "Preview responses exceeding requested pixel bounds must be rejected")
        SearchProtocol.handler = { _ in (200, png) }
        SearchProtocol.headers = ["Content-Type": "application/octet-stream"]
        let original = try await client.thumbnail(remotePath: "/dav/spaces/a$b/file-0.txt", etag: "v1", width: 5000, height: 32)
        precondition(original == nil, "Original document responses are never used as thumbnails")
        let countBeforeForeignPreview = SearchProtocol.requests.count
        let foreignThumbnail = try await client.thumbnail(remotePath: "/dav/spaces/other/file.txt", etag: "v1", width: 5000, height: 32)
        precondition(foreignThumbnail == nil && SearchProtocol.requests.count == countBeforeForeignPreview)

        SearchProtocol.stalled = true
        let countBeforeCancellation = SearchProtocol.requests.count
        let cancelled = ProviderSearchEnumerator(query: "x", desiredResults: 1, client: client, database: database)
        let pending = Task { await page(cancelled) }
        for _ in 0..<100 {
            if SearchProtocol.requests.count > countBeforeCancellation { break }
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        cancelled.invalidate()
        let (_, _, cancelError) = await pending.value
        precondition((cancelError as NSError?)?.code == NSUserCancelledError)
        precondition(SearchProtocol.stopped > 0)
        let (_, _, afterInvalidation) = await page(cancelled)
        precondition(afterInvalidation != nil)
        print("PASS native search paging, scope isolation, query escaping, identity, authentication, cancellation and bounded thumbnail requests")
    }
}
