import Foundation
import ImageIO

extension WebDAVClient {
    /// Ask OpenCloud's preview service for an image without hydrating the file.
    /// Generic DAV roots do not promise preview support and must not receive a
    /// GET that could accidentally retrieve an entire document.
    func thumbnail(remotePath: String, etag: String, width: Int, height: Int) async throws -> Data? {
        guard davPath.contains("/dav/spaces/"), remotePath.hasPrefix(davPath + "/") else { return nil }
        guard let resourceURL = url(for: remotePath),
              var components = URLComponents(url: resourceURL, resolvingAgainstBaseURL: false) else { throw WebDAVError.invalidURL }
        components.queryItems = [URLQueryItem(name: "preview", value: "1"),
                                 URLQueryItem(name: "x", value: String(min(max(width, 1), 1024))),
                                 URLQueryItem(name: "y", value: String(min(max(height, 1), 1024))),
                                 URLQueryItem(name: "a", value: "1"),
                                 URLQueryItem(name: "scalingup", value: "0"),
                                 URLQueryItem(name: "c", value: etag.replacingOccurrences(of: "\"", with: ""))]
        guard let previewURL = components.url else { throw WebDAVError.invalidURL }
        var request = authenticatedRequest(url: previewURL, method: "GET")
        request.setValue("image/*", forHTTPHeaderField: "Accept")
        let (bytes, response) = try await session.bytes(for: request)
        defer { bytes.task.cancel() }
        guard let response = response as? HTTPURLResponse else { throw WebDAVError.serverError }
        switch response.statusCode {
        case 200: break
        case 404, 405, 415, 501: return nil
        case 401: throw WebDAVError.notAuthenticated
        case 403: throw WebDAVError.permissionDenied
        case 500...599: throw WebDAVError.serverError
        default: throw WebDAVError.httpError(statusCode: response.statusCode, message: nil)
        }
        let limit = 8 * 1024 * 1024
        guard response.mimeType?.hasPrefix("image/") == true,
              response.expectedContentLength <= limit else { return nil }
        var data = Data()
        data.reserveCapacity(Int(max(0, response.expectedContentLength)))
        for try await byte in bytes {
            try Task.checkCancellation()
            guard data.count < limit else { return nil }
            data.append(byte)
        }
        guard let source = CGImageSourceCreateWithData(data as CFData, nil), CGImageSourceGetCount(source) > 0,
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let pixelsWide = properties[kCGImagePropertyPixelWidth] as? NSNumber,
              let pixelsHigh = properties[kCGImagePropertyPixelHeight] as? NSNumber,
              (1...1024).contains(pixelsWide.intValue), (1...1024).contains(pixelsHigh.intValue) else { return nil }
        // An endpoint ignoring the preview dimensions must not hand Finder a
        // compressed image whose decoded memory is many times this byte limit.
        return data
    }
}
