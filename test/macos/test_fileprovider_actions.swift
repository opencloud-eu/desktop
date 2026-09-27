import AppKit
import FileProvider

final class FileProviderExtension: NSObject {
    var webdavClient: WebDAVClient?
    var database: ItemDatabase?
}

private final class ActionDAVProtocol: URLProtocol {
    static var requests: [URLRequest] = []
    static var status = 207
    static var response = Data()
    static var suspend = false
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        Self.requests.append(request)
        if Self.suspend { return }
        client?.urlProtocol(self, didReceive: HTTPURLResponse(url: request.url!, statusCode: Self.status,
                            httpVersion: "HTTP/1.1", headerFields: nil)!, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Self.response)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}

@main struct FileProviderActionTests {
    static func document(link: String = "https://web.example.test/f/file?x=1&amp;details=old", href: String = "/dav/spaces/team/report.txt", status: Int = 200) -> Data {
        Data("""
        <d:multistatus xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns"><d:response><d:href>\(href)</d:href>
        <d:propstat><d:prop><oc:privatelink>\(link)</oc:privatelink></d:prop><d:status>HTTP/1.1 \(status) Status</d:status></d:propstat>
        </d:response></d:multistatus>
        """.utf8)
    }
    static func expectFailure(_ client: WebDAVClient) async {
        do { _ = try await client.privateLink(path: "report.txt"); preconditionFailure("Invalid private link was accepted") }
        catch {}
    }
    static func main() async throws {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [ActionDAVProtocol.self]
        let client = WebDAVClient(serverURL: URL(string: "https://dav.example.test")!, davPath: "/dav/spaces/team",
                                  username: "test", password: "not-a-real-secret", sessionConfiguration: config)
        ActionDAVProtocol.response = document()
        let link = try await client.privateLink(path: "report.txt")
        precondition(link.host == "web.example.test", "A distinct legitimate web UI origin is supported")
        let share = try ProviderBrowserAction.share.destination(privateLink: link)
        let shareQuery = URLComponents(url: share, resolvingAgainstBaseURL: false)!.queryItems!
        precondition(shareQuery.filter { $0.name == "details" }.map { $0.value } == ["sharing"])
        precondition(shareQuery.contains { $0.name == "x" && $0.value == "1" }, "Preserve unrelated private-link parameters")
        let versions = try ProviderBrowserAction.versions.destination(privateLink: link)
        let copiedLink = try ProviderBrowserAction.copyLink.destination(privateLink: link)
        precondition(versions.query!.contains("details=versions"))
        precondition(copiedLink == link)
        let hashLink = URL(string: "https://web.example.test/?outer=keep#/f/file?label=a%252Fb&details=old")!
        let hashShare = try ProviderBrowserAction.share.destination(privateLink: hashLink)
        let hashParts = URLComponents(url: hashShare, resolvingAgainstBaseURL: false)!
        let hashRoute = URLComponents(string: hashParts.percentEncodedFragment!)!
        precondition(hashParts.query == "outer=keep")
        precondition(hashRoute.queryItems!.filter { $0.name == "details" }.map { $0.value } == ["sharing"])
        precondition(hashRoute.queryItems!.first { $0.name == "label" }?.value == "a%2Fb", "Preserve escaped hash-route query values")
        let hashCopy = try ProviderBrowserAction.copyLink.destination(privateLink: hashLink)
        precondition(hashCopy == hashLink)
        let request = ActionDAVProtocol.requests.last!
        precondition(request.httpMethod == "PROPFIND" && request.value(forHTTPHeaderField: "Depth") == "0")
        precondition(request.url!.path == "/dav/spaces/team/report.txt")
        if let body = request.httpBody {
            precondition(String(data: body, encoding: .utf8)!.contains("privatelink"))
        } else {
            let stream = request.httpBodyStream!
            stream.open()
            var buffer = [UInt8](repeating: 0, count: 4096)
            let count = stream.read(&buffer, maxLength: buffer.count)
            stream.close()
            precondition(count > 0 && String(bytes: buffer.prefix(count), encoding: .utf8)!.contains("privatelink"))
        }
        precondition(ActionDAVProtocol.requests.allSatisfy { $0.httpMethod == "PROPFIND" }, "Actions never create public shares")
        for invalid in ["file:///etc/passwd", "javascript:alert(1)", "https://user:password@web.example.test/file", "https://user@web.example.test/file"] {
            ActionDAVProtocol.response = document(link: invalid)
            await expectFailure(client)
        }
        ActionDAVProtocol.response = document(status: 404)
        await expectFailure(client)
        ActionDAVProtocol.response = document(href: "/dav/spaces/other/report.txt")
        await expectFailure(client)
        ActionDAVProtocol.response = document(href: "https://other.example.test/dav/spaces/team/report.txt")
        await expectFailure(client)
        ActionDAVProtocol.response = document()
        ActionDAVProtocol.status = 403
        do { _ = try await client.privateLink(path: "report.txt"); preconditionFailure("Denied action succeeded") }
        catch WebDAVError.permissionDenied {}
        ActionDAVProtocol.status = 207
        do { _ = try await client.privateLink(path: "/dav/spaces/team/../../outside"); preconditionFailure("Action escaped its space") }
        catch WebDAVError.invalidURL {}
        let provider = FileProviderExtension()
        let unsupported: Error? = await withCheckedContinuation { continuation in
            _ = provider.performAction(identifier: NSFileProviderExtensionActionIdentifier(ProviderBrowserAction.open.rawValue),
                                       onItemsWithIdentifiers: [.rootContainer, .rootContainer]) { continuation.resume(returning: $0) }
        }
        precondition((unsupported as NSError?)?.code == CocoaError.featureUnsupported.rawValue, "Browser actions require exactly one item")
        let unauthenticated: Error? = await withCheckedContinuation { continuation in
            _ = provider.performAction(identifier: NSFileProviderExtensionActionIdentifier(ProviderBrowserAction.open.rawValue),
                                       onItemsWithIdentifiers: [NSFileProviderItemIdentifier("file")]) { continuation.resume(returning: $0) }
        }
        precondition((unauthenticated as NSError?)?.code == NSFileProviderError.notAuthenticated.rawValue)
        let plist = try PropertyListSerialization.propertyList(from: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])), format: nil) as! [String: Any]
        let declarations = (plist["NSExtension"] as! [String: Any])["NSExtensionFileProviderActions"] as! [[String: String]]
        func item(permissions: String, directory: Bool = false) -> FileProviderItem {
            let type = directory ? "<d:resourcetype><d:collection/></d:resourcetype>" : ""
            let xml = String(data: document(), encoding: .utf8)!.replacingOccurrences(of: "</d:prop>", with: "<oc:permissions>\(permissions)</oc:permissions>\(type)</d:prop>")
            let remote = WebDAVXMLParser(baseURL: URL(string: "https://dav.example.test/dav/spaces/team/report.txt")!).parse(data: Data(xml.utf8))!.first!
            return FileProviderItem(metadata: ItemMetadata(from: remote), parentItemIdentifier: .rootContainer)
        }
        for declaration in declarations {
            let predicate = NSPredicate(format: declaration["NSExtensionFileProviderActionActivationRule"]!)
            precondition(predicate.evaluate(with: ["fileproviderItems": [item(permissions: "R")]]), "Action accepts a server item")
            precondition(!predicate.evaluate(with: ["fileproviderItems": [FileProviderItem.rootContainer()]]), "Special containers have no browser actions")
            precondition(!predicate.evaluate(with: ["fileproviderItems": [item(permissions: "R"), item(permissions: "R")]]), "Actions require a single selection")
            if declaration["NSExtensionFileProviderActionIdentifier"] == ProviderBrowserAction.versions.rawValue {
                precondition(!predicate.evaluate(with: ["fileproviderItems": [item(permissions: "R", directory: true)]]), "Directories have no file-version history")
            }
            if declaration["NSExtensionFileProviderActionIdentifier"] == ProviderBrowserAction.share.rawValue {
                precondition(!predicate.evaluate(with: ["fileproviderItems": [item(permissions: "S")]]), "S means shared, not permission to reshare")
                precondition(!predicate.evaluate(with: ["fileproviderItems": [item(permissions: "")]]), "Unknown permissions cannot enable sharing")
            }
        }
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("fileprovider-action-" + UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: temporary) }
        let database = try ItemDatabase(containerURL: temporary, domainIdentifier: "test-actions")
        let metadata = item(permissions: "R").metadata!
        try await database.addItemMetadata(metadata)
        provider.database = database
        provider.webdavClient = client
        ActionDAVProtocol.suspend = true
        let cancelled: Error? = await withCheckedContinuation { continuation in
            let progress = provider.performAction(identifier: NSFileProviderExtensionActionIdentifier(ProviderBrowserAction.open.rawValue),
                onItemsWithIdentifiers: [NSFileProviderItemIdentifier(metadata.ocId)]) { continuation.resume(returning: $0) }
            Task {
                try? await Task.sleep(nanoseconds: 50_000_000)
                progress.cancel()
            }
        }
        ActionDAVProtocol.suspend = false
        precondition((cancelled as NSError?)?.code == CocoaError.userCancelled.rawValue, "Cancelling the action cancels its network request")
        print("FileProvider private-link, permission, scope and action validation tests passed")
    }
}
