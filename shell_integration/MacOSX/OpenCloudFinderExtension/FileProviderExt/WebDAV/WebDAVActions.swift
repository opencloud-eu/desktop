import Foundation

enum ProviderBrowserAction: String {
    case open = "eu.opencloud.open-in-browser"
    case share = "eu.opencloud.share"
    case versions = "eu.opencloud.versions"
    case copyLink = "eu.opencloud.copy-private-link"

    func destination(privateLink: URL) throws -> URL {
        guard var parts = URLComponents(url: privateLink, resolvingAgainstBaseURL: false),
              ["http", "https"].contains(parts.scheme?.lowercased() ?? ""),
              let host = parts.host, !host.isEmpty, parts.user == nil, parts.password == nil else {
            throw WebDAVError.invalidURL
        }
        if self == .share || self == .versions {
            func setPanel(_ route: inout URLComponents) {
                var query = (route.queryItems ?? []).filter { $0.name != "details" }
                query.append(URLQueryItem(name: "details", value: self == .share ? "sharing" : "versions"))
                route.queryItems = query
            }
            // OpenCloud Web also supports hash routing. Its router reads the
            // fragment's query in that mode, not the outer document's query.
            if let fragment = parts.percentEncodedFragment, fragment.hasPrefix("/"),
               var route = URLComponents(string: fragment) {
                setPanel(&route)
                parts.percentEncodedFragment = route.string
            } else {
                setPanel(&parts)
            }
        }
        guard let result = parts.url else { throw WebDAVError.invalidURL }
        return result
    }
}

extension WebDAVClient {
    /// Read an existing private link. This never creates a share or public link.
    func privateLink(path: String) async throws -> URL {
        guard let resource = url(for: path), let root = url(for: "/") else { throw WebDAVError.invalidURL }
        let rootPath = root.standardized.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        let resourcePath = resource.standardized.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard resourcePath == rootPath || resourcePath.hasPrefix(rootPath + "/") else { throw WebDAVError.invalidURL }
        var request = authenticatedRequest(url: resource, method: "PROPFIND")
        request.setValue("0", forHTTPHeaderField: "Depth")
        request.setValue("application/xml; charset=utf-8", forHTTPHeaderField: "Content-Type")
        request.httpBody = Data("""
            <d:propfind xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns"><d:prop><oc:privatelink/></d:prop></d:propfind>
            """.utf8)
        let (data, response) = try await session.data(for: request)
        try Task.checkCancellation()
        guard let http = response as? HTTPURLResponse else { throw WebDAVError.invalidURL }
        switch http.statusCode {
        case 207:
            guard data.count <= 4 * 1024 * 1024,
                  let result = PrivateLinkParser(resource: resource).parse(data) else {
                throw WebDAVError.parseError("The server did not return a private link for this item")
            }
            return try ProviderBrowserAction.open.destination(privateLink: result)
        case 401: throw WebDAVError.notAuthenticated
        case 403: throw WebDAVError.permissionDenied
        case 404: throw WebDAVError.fileNotFound
        default: throw WebDAVError.httpError(statusCode: http.statusCode, message: nil)
        }
    }
}

/// Only accept a successful ownCloud property on the requested DAV resource.
private final class PrivateLinkParser: NSObject, XMLParserDelegate {
    let resource: URL
    private var depth = 0
    private var responseDepth = 0
    private var propstatDepth = 0
    private var text = ""
    private var href = ""
    private var status = ""
    private var property = ""
    private var responseLinks: [String] = []
    private var links: [URL] = []
    init(resource: URL) { self.resource = resource }
    func parse(_ data: Data) -> URL? {
        let parser = XMLParser(data: data)
        parser.shouldProcessNamespaces = true
        parser.shouldResolveExternalEntities = false
        parser.delegate = self
        guard parser.parse(), links.count == 1 else { return nil }
        return links[0]
    }
    func parser(_ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String]) {
        depth += 1
        text = ""
        if depth == 1 && (namespaceURI != "DAV:" || name != "multistatus") { parser.abortParsing(); return }
        if namespaceURI == "DAV:", name == "response" {
            responseDepth = depth
            href = ""
            responseLinks = []
        } else if namespaceURI == "DAV:", name == "propstat", responseDepth > 0 {
            propstatDepth = depth
            property = ""
            status = ""
        }
    }
    func parser(_ parser: XMLParser, foundCharacters string: String) { text += string }
    func parser(_ parser: XMLParser, didEndElement name: String, namespaceURI: String?, qualifiedName: String?) {
        defer { depth -= 1; text = "" }
        let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if namespaceURI == "DAV:", name == "href", depth == responseDepth + 1 { href = value }
        if namespaceURI == "http://owncloud.org/ns", name == "privatelink", propstatDepth > 0 { property = value }
        if namespaceURI == "DAV:", name == "status", depth == propstatDepth + 1 { status = value }
        if namespaceURI == "DAV:", name == "propstat", depth == propstatDepth {
            if status.split(whereSeparator: { $0.isWhitespace }).dropFirst().first == "200", !property.isEmpty { responseLinks.append(property) }
            propstatDepth = 0
        }
        if namespaceURI == "DAV:", name == "response", depth == responseDepth {
            let requested = resource.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            if let item = URL(string: href, relativeTo: resource)?.absoluteURL,
               item.host == resource.host, item.scheme == resource.scheme, item.port == resource.port,
               item.path.trimmingCharacters(in: CharacterSet(charactersIn: "/")) == requested {
                links += responseLinks.compactMap { URL(string: $0) }
            }
            responseDepth = 0
        }
    }
}
