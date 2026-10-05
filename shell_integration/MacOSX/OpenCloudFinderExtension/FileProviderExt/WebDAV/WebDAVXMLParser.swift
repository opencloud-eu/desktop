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
import OSLog

/// Parser for WebDAV PROPFIND multistatus XML responses.
/// Supports the ownCloud/Nextcloud/OpenCloud extended properties.
final class WebDAVXMLParser: NSObject, XMLParserDelegate {

    private let logger = Logger(subsystem: "eu.opencloud.desktop.FileProviderExt", category: "WebDAVXMLParser")

    /// Base URL used to resolve relative hrefs
    private let baseURL: URL

    /// Parsed items
    private(set) var items: [WebDAVItem] = []

    /// Current parsing state
    private var currentResponse: ResponseBuilder?
    private var currentElement: String = ""
    private var currentText: String = ""
    private var isInPropstat: Bool = false
    private var currentStatus: String = ""
    private var beforePropstat: ResponseBuilder?
    private var sawMultistatus = false
    private var invalidResponse = false

    /// Date formatters for parsing dates
    private static let rfc1123Formatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "EEE, dd MMM yyyy HH:mm:ss zzz"
        return formatter
    }()

    private static let iso8601Formatter: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()

    private static let iso8601FormatterNoFraction: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        return formatter
    }()

    init(baseURL: URL) {
        self.baseURL = baseURL
        super.init()
    }

    /// Parse XML data and return WebDAV items
    func parse(data: Data) -> [WebDAVItem]? {
        items = []
        currentResponse = nil
        sawMultistatus = false
        invalidResponse = false

        let parser = XMLParser(data: data)
        parser.delegate = self
        parser.shouldProcessNamespaces = true

        guard parser.parse() else {
            logger.error("Failed to parse WebDAV XML response: \(parser.parserError?.localizedDescription ?? "unknown error")")
            return nil
        }

        guard sawMultistatus, !invalidResponse else { return nil }
        return items
    }

    // MARK: - XMLParserDelegate

    func parser(_ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?, qualifiedName qName: String?, attributes attributeDict: [String : String] = [:]) {
        currentElement = elementName
        currentText = ""

        switch elementName {
        case "multistatus":
            sawMultistatus = namespaceURI == "DAV:"
        case "response":
            currentResponse = ResponseBuilder()
        case "propstat":
            isInPropstat = true
            currentStatus = ""
            beforePropstat = currentResponse
        default:
            break
        }
    }

    func parser(_ parser: XMLParser, foundCharacters string: String) {
        currentText += string
    }

    func parser(_ parser: XMLParser, didEndElement elementName: String, namespaceURI: String?, qualifiedName qName: String?) {
        let trimmedText = currentText.trimmingCharacters(in: .whitespacesAndNewlines)

        guard var response = currentResponse else { return }

        switch elementName {
        case "response":
            if response.hasSuccessfulProperties, let item = response.build(baseURL: baseURL) {
                items.append(item)
            } else {
                // An incomplete listing must never be interpreted as deletions.
                invalidResponse = true
            }
            currentResponse = nil
            return

        case "propstat":
            if currentStatus.split(separator: " ").dropFirst().first == "200" {
                response.hasSuccessfulProperties = true
            } else if let previous = beforePropstat {
                response = previous
            }
            beforePropstat = nil
            isInPropstat = false

        case "status":
            if isInPropstat {
                currentStatus = trimmedText
            }

        case "href":
            response.href = trimmedText

        case "getcontenttype":
            if !trimmedText.isEmpty {
                response.contentType = trimmedText
            }

        case "getcontentlength":
            let parsed = Int64(trimmedText) ?? 0
            if parsed > 0 {
                response.size = parsed
            }

        case "getlastmodified":
            if !trimmedText.isEmpty, let date = Self.parseDate(trimmedText) {
                response.lastModified = date
            }

        case "creationdate":
            if !trimmedText.isEmpty, let date = Self.parseDate(trimmedText) {
                response.creationDate = date
            }

        case "getetag":
            if !trimmedText.isEmpty {
                response.etag = trimmedText
            }

        case "id": // oc:id
            if !trimmedText.isEmpty {
                response.ocId = trimmedText
            }

        case "fileid": // oc:fileid
            if !trimmedText.isEmpty {
                response.fileId = trimmedText
            }

        case "permissions": // oc:permissions
            if !trimmedText.isEmpty {
                response.permissions = trimmedText
            }

        case "owner-id": // oc:owner-id
            if !trimmedText.isEmpty {
                response.ownerId = trimmedText
            }

        case "owner-display-name": // oc:owner-display-name
            if !trimmedText.isEmpty {
                response.ownerDisplayName = trimmedText
            }

        case "resourcetype":
            break

        case "collection":
            response.isDirectory = true

        default:
            break
        }

        currentResponse = response
        currentText = ""
    }

    // MARK: - Helpers

    private static func parseDate(_ string: String) -> Date? {
        // Try RFC 1123 format first (common for getlastmodified)
        if let date = rfc1123Formatter.date(from: string) {
            return date
        }
        // Try ISO 8601 with fractions
        if let date = iso8601Formatter.date(from: string) {
            return date
        }
        // Try ISO 8601 without fractions
        if let date = iso8601FormatterNoFraction.date(from: string) {
            return date
        }
        return nil
    }
}

// MARK: - Response Builder

private struct ResponseBuilder {
    var hasSuccessfulProperties = false
    var href: String?
    var contentType: String?
    var size: Int64 = 0
    var lastModified: Date?
    var creationDate: Date?
    var etag: String?
    var ocId: String?
    var fileId: String?
    var permissions: String = ""
    var ownerId: String = ""
    var ownerDisplayName: String = ""
    var isDirectory: Bool = false

    func build(baseURL: URL) -> WebDAVItem? {
        guard let href = href else {
            return nil
        }


        // URL decode the href first
        let decodedHref = href.removingPercentEncoding ?? href

        // Resolve href to full path
        let remotePath: String
        if decodedHref.hasPrefix("/") {
            remotePath = decodedHref
        } else if let url = URL(string: href, relativeTo: baseURL) {
            remotePath = url.path
        } else {
            remotePath = decodedHref
        }

        // Determine if directory from content type, resourcetype, or trailing slash
        // Folders often have trailing slash in href or httpd/unix-directory content type
        let isDir = isDirectory || contentType == "httpd/unix-directory" || href.hasSuffix("/")

        // Use ocId if available, otherwise generate from path
        let identifier = ocId ?? fileId.map { "fileid:" + $0 } ?? WebDAVItem.generateIdentifier(from: remotePath)
        let fileIdentifier = fileId ?? identifier

        // Extract filename from path (after removing /remote.php/webdav prefix if present)
        var cleanPath = remotePath
        if let range = cleanPath.range(of: "/remote.php/webdav") {
            cleanPath = String(cleanPath[range.upperBound...])
        }
        if let range = cleanPath.range(of: "/remote.php/dav/files/") {
            // Handle /remote.php/dav/files/<user>/ format
            let afterPrefix = cleanPath[range.upperBound...]
            if let userSlash = afterPrefix.firstIndex(of: "/") {
                cleanPath = String(afterPrefix[userSlash...])
            }
        }
        let filename = WebDAVItem.extractFilename(from: cleanPath)

        return WebDAVItem(
            ocId: identifier,
            fileId: fileIdentifier,
            remotePath: remotePath,
            filename: filename,
            etag: etag ?? "",
            contentType: isDir ? "httpd/unix-directory" : (contentType ?? "application/octet-stream"),
            size: size,
            lastModified: lastModified,
            creationDate: creationDate,
            isDirectory: isDir,
            permissions: permissions,
            ownerId: ownerId,
            ownerDisplayName: ownerDisplayName
        )
    }
}
