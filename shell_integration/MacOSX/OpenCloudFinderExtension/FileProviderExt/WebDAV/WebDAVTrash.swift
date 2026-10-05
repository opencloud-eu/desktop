import Foundation
import OSLog

/// OpenCloud recycle entries have opaque keys, independently of their original names.
struct WebDAVTrashItem: Sendable {
    let key: String
    let originalPath: String
    let filename: String
    let isDirectory: Bool
    let size: Int64
    let deletionDate: Date?
}

extension WebDAVClient {
    /// Shares is a synthetic mountpoint collection. Its storage provider does
    /// not implement recycle operations, despite exposing a normal DAV root.
    nonisolated static func supportsTrashPath(_ path: String) -> Bool {
        let normalized = "/" + path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard let range = normalized.range(of: "/dav/spaces/", options: .backwards) else { return false }
        let space = String(normalized[range.upperBound...])
        guard !space.isEmpty, !space.contains("/"), space != ".", space != ".." else { return false }
        return !isVirtualSharesPath(normalized)
    }

    var supportsTrash: Bool { Self.supportsTrashPath(davPath) }

    private var trashEndpoint: URL? {
        guard Self.supportsTrashPath(davPath), let range = davPath.range(of: "/dav/spaces/", options: .backwards) else { return nil }
        let space = String(davPath[range.upperBound...])
        guard !space.isEmpty, !space.contains("/"), space != ".", space != ".." else { return nil }
        var components = URLComponents(url: serverURL, resolvingAgainstBaseURL: false)
        components?.path = String(davPath[..<range.upperBound]) + "trash-bin/" + space
        return components?.url
    }

    private func trashURL(key: String = "") throws -> URL {
        guard let endpoint = trashEndpoint else { throw CocoaError(.featureUnsupported) }
        guard !key.split(separator: "/", omittingEmptySubsequences: false).contains(where: { $0 == "." || $0 == ".." }), !key.hasPrefix("/") else {
            throw WebDAVError.invalidURL
        }
        var components = URLComponents(url: endpoint, resolvingAgainstBaseURL: false)
        components?.path = endpoint.path + (key.isEmpty ? "" : "/" + key)
        guard let result = components?.url else { throw WebDAVError.invalidURL }
        return result
    }

    private func trashResponse(_ request: URLRequest, expected: Set<Int>) async throws -> Data {
        let (data, response) = try await session.data(for: request)
        guard let response = response as? HTTPURLResponse else { throw WebDAVError.serverError }
        if expected.contains(response.statusCode) { return data }
        Logger(subsystem: "eu.opencloud.desktop.FileProviderExt", category: "WebDAVTrash")
            .error("Trash request failed with HTTP \(response.statusCode, privacy: .public)")
        switch response.statusCode {
        case 401: throw WebDAVError.notAuthenticated
        case 403: throw WebDAVError.permissionDenied
        case 404: throw WebDAVError.fileNotFound
        case 409, 412: throw WebDAVError.conflict
        case 405, 501: throw CocoaError(.featureUnsupported)
        case 500...599: throw WebDAVError.serverError
        default: throw WebDAVError.httpError(statusCode: response.statusCode, message: nil)
        }
    }

    func listTrash(key: String = "") async throws -> [WebDAVTrashItem] {
        let endpoint = try trashURL(key: key)
        var request = authenticatedRequest(url: endpoint, method: "PROPFIND")
        request.setValue("1", forHTTPHeaderField: "Depth")
        request.setValue("application/xml; charset=utf-8", forHTTPHeaderField: "Content-Type")
        request.httpBody = Data("""
        <d:propfind xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns"><d:prop>
        <d:resourcetype/><d:getcontentlength/><oc:size/><oc:trashbin-original-location/>
        <oc:trashbin-original-filename/><oc:trashbin-delete-timestamp/>
        </d:prop></d:propfind>
        """.utf8)
        let data = try await trashResponse(request, expected: [207])
        guard data.count <= 32 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
        return try Self.parseTrash(data: data, endpoint: endpoint, root: try trashURL())
    }

    static func parseTrash(data: Data, endpoint: URL, root: URL) throws -> [WebDAVTrashItem] {
        let xml = try XMLDocument(data: data, options: [.nodeLoadExternalEntitiesNever])
        guard xml.rootElement()?.localName == "multistatus", xml.rootElement()?.uri == "DAV:" else {
            throw WebDAVError.parseError("Invalid trash listing")
        }
        let responses = try xml.nodes(forXPath: "/*[local-name()='multistatus']/*[local-name()='response']")
        var result: [WebDAVTrashItem] = []
        var keys = Set<String>()
        var sawRoot = false
        for response in responses {
            try Task.checkCancellation()
            let href = try response.nodes(forXPath: "./*[local-name()='href']").first?.stringValue ?? ""
            guard let target = URL(string: href, relativeTo: endpoint)?.absoluteURL,
                  target.scheme == root.scheme, target.host == root.host, target.port == root.port else {
                throw WebDAVError.parseError("Trash href outside the configured server")
            }
            let path = target.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            let requested = endpoint.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            if path == requested {
                let statuses = try response.nodes(forXPath: "./*[local-name()='propstat']/*[local-name()='status']")
                guard statuses.contains(where: { $0.stringValue?.split(separator: " ").dropFirst().first == "200" }) else {
                    throw WebDAVError.parseError("Incomplete trash root response")
                }
                sawRoot = true
                continue
            }
            let prefix = root.path.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/"
            guard path.hasPrefix(prefix) else { throw WebDAVError.parseError("Trash href outside the configured space") }
            let key = String(path.dropFirst(prefix.count))
            guard path.hasPrefix(requested + "/"), !path.dropFirst(requested.count + 1).contains("/"),
                  !key.split(separator: "/").contains(where: { $0 == "." || $0 == ".." }), keys.insert(key).inserted else {
                throw WebDAVError.parseError("Invalid trash child")
            }
            var properties: [String: XMLNode] = [:]
            for propstat in try response.nodes(forXPath: "./*[local-name()='propstat']") {
                let status = try propstat.nodes(forXPath: "./*[local-name()='status']").first?.stringValue ?? ""
                guard status.split(separator: " ").dropFirst().first == "200" else { continue }
                for prop in try propstat.nodes(forXPath: "./*[local-name()='prop']/*") {
                    if let name = prop.localName { properties[(prop.uri ?? "") + name] = prop }
                }
            }
            let oc = "http://owncloud.org/ns"
            guard let original = properties[oc + "trashbin-original-location"]?.stringValue, !original.isEmpty,
                  !original.split(separator: "/").contains(where: { $0 == "." || $0 == ".." }) else {
                throw WebDAVError.parseError("Missing trash original location")
            }
            let isDirectory = properties["DAV:resourcetype"]?.children?.contains(where: { $0.localName == "collection" && $0.uri == "DAV:" }) == true
            let size = Int64(properties["DAV:getcontentlength"]?.stringValue ?? properties[oc + "size"]?.stringValue ?? "0") ?? 0
            let deleted = properties[oc + "trashbin-delete-timestamp"]?.stringValue.flatMap(TimeInterval.init).map(Date.init(timeIntervalSince1970:))
            let filename = properties[oc + "trashbin-original-filename"]?.stringValue ?? (original as NSString).lastPathComponent
            guard !filename.isEmpty, filename != ".", filename != "..", !filename.contains("/"), !filename.contains("\0") else {
                throw WebDAVError.parseError("Invalid trash filename")
            }
            result.append(WebDAVTrashItem(key: key, originalPath: original,
                filename: filename,
                isDirectory: isDirectory, size: max(0, size), deletionDate: deleted))
        }
        guard sawRoot else { throw WebDAVError.parseError("Missing trash root response") }
        return result
    }

    func restoreTrash(key: String, to destination: String) async throws {
        guard let target = url(for: destination), target.path.hasPrefix(davPath + "/"),
              !target.path.split(separator: "/").contains(where: { $0 == "." || $0 == ".." }) else { throw WebDAVError.invalidURL }
        var request = authenticatedRequest(url: try trashURL(key: key), method: "MOVE")
        request.setValue(target.absoluteString, forHTTPHeaderField: "Destination")
        request.setValue("F", forHTTPHeaderField: "Overwrite")
        _ = try await trashResponse(request, expected: [201, 204])
    }

    func purgeTrash(key: String) async throws {
        guard !key.isEmpty else { throw WebDAVError.invalidURL }
        let request = authenticatedRequest(url: try trashURL(key: key), method: "DELETE")
        _ = try await trashResponse(request, expected: [200, 204, 404])
    }

    func livePath(forTrashOriginalPath original: String) throws -> String {
        guard let path = url(for: "/" + original.trimmingCharacters(in: CharacterSet(charactersIn: "/")))?.path,
              path.hasPrefix(davPath + "/") else { throw WebDAVError.invalidURL }
        return path
    }
}
