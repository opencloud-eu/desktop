/*
 * Copyright (C) 2025 OpenCloud GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

import Foundation
import FileProvider
import CryptoKit

/// SHA256 helper for file content comparison
extension SHA256 {
    /// Compute SHA256 of a file's contents, returning raw digest bytes
    static func hash(contentsOf url: URL) -> Data? {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        var hasher = SHA256()
        do {
            while let chunk = try handle.read(upToCount: 1024 * 1024), !chunk.isEmpty {
                hasher.update(data: chunk)
            }
            return Data(hasher.finalize())
        } catch {
            return nil
        }
    }
}

/// Error types for WebDAV operations
enum WebDAVError: Error, LocalizedError {
    case invalidURL
    case notAuthenticated
    case httpError(statusCode: Int, message: String?)
    case networkError(Error)
    case parseError(String)
    case fileNotFound
    case permissionDenied
    case serverError
    case cancelled
    case conflict

    var errorDescription: String? {
        switch self {
        case .invalidURL:
            return "Invalid server URL"
        case .notAuthenticated:
            return "Not authenticated"
        case .httpError(let code, let message):
            return "HTTP error \(code): \(message ?? "Unknown")"
        case .networkError(let error):
            return "Network error: \(error.localizedDescription)"
        case .parseError(let reason):
            return "Parse error: \(reason)"
        case .fileNotFound:
            return "File not found"
        case .permissionDenied:
            return "Permission denied"
        case .serverError:
            return "Server error"
        case .cancelled:
            return "Operation cancelled"
        case .conflict:
            return "Conflict: server version changed"
        }
    }
}

extension WebDAVError {
    func fileProviderError(isWrite: Bool = false, itemIdentifier: NSFileProviderItemIdentifier? = nil) -> Error {
        switch self {
        case .notAuthenticated:
            return NSFileProviderError(.notAuthenticated)
        case .permissionDenied:
            return CocoaError(isWrite ? .fileWriteNoPermission : .fileReadNoPermission)
        case .fileNotFound:
            if let identifier = itemIdentifier {
                return NSError.fileProviderErrorForNonExistentItem(withIdentifier: identifier)
            }
            return NSFileProviderError(.noSuchItem)
        case .httpError(statusCode: 507, message: _):
            return NSFileProviderError(.insufficientQuota)
        case .networkError, .serverError:
            return NSFileProviderError(.serverUnreachable)
        case .cancelled:
            return CocoaError(.userCancelled)
        default:
            return NSFileProviderError(.cannotSynchronize)
        }
    }
}

/// Keep native FileProvider/Cocoa errors intact and classify transport failures once.
func fileProviderErrorDiagnostic(_ error: Error) -> String {
    if let error = error as? WebDAVError {
        switch error {
        case .invalidURL: return "dav-invalid-url"
        case .notAuthenticated: return "dav-http-401"
        case .httpError(let status, _): return "dav-http-\(status)"
        case .networkError(let underlying): return "dav-" + fileProviderErrorDiagnostic(underlying)
        case .parseError: return "dav-invalid-multistatus"
        case .fileNotFound: return "dav-http-404"
        case .permissionDenied: return "dav-http-403"
        case .serverError: return "dav-http-5xx"
        case .cancelled: return "cancelled"
        case .conflict: return "dav-conflict"
        }
    }
    let error = error as NSError
    let category: String
    switch error.domain {
    case NSURLErrorDomain: category = "url"
    case NSCocoaErrorDomain: category = "cocoa"
    case NSFileProviderErrorDomain: category = "fileprovider"
    case NSPOSIXErrorDomain: category = "posix"
    default: category = "other"
    }
    // Only fixed categories and numeric codes are public. Underlying messages
    // can contain account URLs, filenames or HTTP response bodies.
    return "\(category)-\(error.code)"
}

func fileProviderError(_ error: Error, isWrite: Bool = false, itemIdentifier: NSFileProviderItemIdentifier? = nil) -> Error {
    if let error = error as? WebDAVError {
        return error.fileProviderError(isWrite: isWrite, itemIdentifier: itemIdentifier)
    }
    if error is CancellationError || (error as? URLError)?.code == .cancelled {
        return CocoaError(.userCancelled)
    }
    if error is URLError { return NSFileProviderError(.serverUnreachable) }
    let native = error as NSError
    if native.domain == NSFileProviderErrorDomain || native.domain == NSCocoaErrorDomain { return error }
    return NSFileProviderError(.cannotSynchronize)
}

/// Represents a parsed item from WebDAV PROPFIND response.
/// Models the key properties returned by OpenCloud/ownCloud/Nextcloud servers.
struct WebDAVItem: Sendable {
    /// Server-assigned unique identifier (oc:id from PROPFIND).
    /// This is used as NSFileProviderItemIdentifier.
    let ocId: String

    /// Server-assigned file ID (oc:fileid from PROPFIND).
    let fileId: String

    /// Full remote path on the server (e.g., "/remote.php/webdav/Documents/file.txt")
    let remotePath: String

    /// Filename only (e.g., "file.txt")
    let filename: String

    /// ETag from server (used for versioning and change detection)
    let etag: String

    /// MIME content type (e.g., "text/plain", "httpd/unix-directory" for folders)
    let contentType: String

    /// File size in bytes (0 for directories)
    let size: Int64

    /// Last modification date
    let lastModified: Date?

    /// Creation date (if provided by server)
    let creationDate: Date?

    /// Whether this is a directory/collection
    let isDirectory: Bool

    /// Permissions string from server (e.g., "RGDNVW")
    let permissions: String

    /// Owner ID
    let ownerId: String

    /// Owner display name
    let ownerDisplayName: String

    /// Parent remote path (e.g., "/remote.php/webdav/Documents" for "/remote.php/webdav/Documents/file.txt")
    var parentPath: String {
        let normalizedPath = remotePath.hasSuffix("/") ? String(remotePath.dropLast()) : remotePath
        if let lastSlash = normalizedPath.lastIndex(of: "/") {
            let parent = String(normalizedPath[..<lastSlash])
            return parent.isEmpty ? "/" : parent
        }
        return "/"
    }

    /// Extract filename from remote path
    static func extractFilename(from remotePath: String) -> String {
        // remotePath has already been decoded by the XML parser. Literal
        // percent sequences in filenames must not be decoded a second time.
        let normalizedPath = remotePath.hasSuffix("/") ? String(remotePath.dropLast()) : remotePath
        if let lastSlash = normalizedPath.lastIndex(of: "/") {
            return String(normalizedPath[normalizedPath.index(after: lastSlash)...])
        }
        return normalizedPath
    }

    static func path(fromIdentifier identifier: String) -> String? {
        let token = identifier.hasPrefix("path:") ? String(identifier.dropFirst(5)) : identifier
        let base64 = token.replacingOccurrences(of: "_", with: "/").replacingOccurrences(of: "-", with: "+")
        let padded = base64 + String(repeating: "=", count: (4 - base64.count % 4) % 4)
        guard let data = Data(base64Encoded: padded), let path = String(data: data, encoding: .utf8),
              path.hasPrefix("/") else { return nil }
        return path
    }

    /// Generate a fallback identifier from path if server doesn't provide ocId
    static func generateIdentifier(from remotePath: String) -> String {
        let normalizedPath = remotePath.hasSuffix("/") ? String(remotePath.dropLast()) : remotePath

        // Use simple base64 encoding of path for fallback
        // In production, server should always provide ocId
        if let data = normalizedPath.data(using: .utf8) {
            return "path:" + data.base64EncodedString()
                .replacingOccurrences(of: "/", with: "_")
                .replacingOccurrences(of: "+", with: "-")
                .replacingOccurrences(of: "=", with: "")
        }
        return UUID().uuidString
    }
}
