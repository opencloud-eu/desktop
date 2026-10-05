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

/// Metadata for a file or folder, stored in the local database.
/// This is a simplified version of Nextcloud's SendableItemMetadata.
struct ItemMetadata: Sendable, Equatable {
    /// Server-assigned unique identifier (oc:id from PROPFIND).
    /// Used as NSFileProviderItemIdentifier.
    var ocId: String

    /// Server-assigned file ID (oc:fileid from PROPFIND).
    let fileId: String

    /// Parent item's ocId (for building hierarchy)
    var parentOcId: String

    /// Full remote path on the server
    let remotePath: String

    /// Filename only
    let filename: String

    /// ETag from server (used for versioning)
    let etag: String

    /// MIME content type
    let contentType: String

    /// File size in bytes
    var size: Int64

    /// Last modification date
    let lastModified: Date?

    /// Creation date
    let creationDate: Date?

    /// Whether this is a directory
    let isDirectory: Bool

    /// Permissions string from server (e.g., "RGDNVW")
    let permissions: String

    /// Owner ID
    let ownerId: String

    /// Owner display name
    let ownerDisplayName: String

    /// Whether the file is downloaded locally
    var isDownloaded: Bool

    /// Whether the file is currently downloading
    var isDownloading: Bool

    /// Whether the file has been uploaded (for newly created items)
    var isUploaded: Bool

    /// Whether the file is currently uploading
    var isUploading: Bool

    /// Status code (normal, downloading, uploading, error, etc.)
    var status: ItemStatus

    /// Error message if any
    var statusError: String?

    /// Timestamp when this metadata was last synced from server
    var syncTime: Date

    /// Finder metadata is private to this account and device.
    var finderMetadata = FinderMetadata()
    var isTrashed: Bool { finderMetadata.trash != nil }

    /// Computed parent path from remote path
    var parentPath: String {
        let normalizedPath = remotePath.hasSuffix("/") ? String(remotePath.dropLast()) : remotePath
        if let lastSlash = normalizedPath.lastIndex(of: "/") {
            let parent = String(normalizedPath[..<lastSlash])
            return parent.isEmpty ? "/" : parent
        }
        return "/"
    }

    /// Initialize from WebDAVItem (server response)
    init(from webDAVItem: WebDAVItem, parentOcId: String = "") {
        self.ocId = webDAVItem.ocId
        self.fileId = webDAVItem.fileId
        self.parentOcId = parentOcId
        self.remotePath = webDAVItem.remotePath
        self.filename = webDAVItem.filename
        self.etag = webDAVItem.etag
        self.contentType = webDAVItem.contentType
        self.size = webDAVItem.size
        self.lastModified = webDAVItem.lastModified
        self.creationDate = webDAVItem.creationDate
        self.isDirectory = webDAVItem.isDirectory
        self.permissions = webDAVItem.permissions
        self.ownerId = webDAVItem.ownerId
        self.ownerDisplayName = webDAVItem.ownerDisplayName
        self.isDownloaded = false
        self.isDownloading = false
        self.isUploaded = true  // Items from server are already uploaded
        self.isUploading = false
        self.status = .normal
        self.statusError = nil
        self.syncTime = Date()
    }

    /// Initialize with all fields (for database loading)
    init(
        ocId: String,
        fileId: String,
        parentOcId: String,
        remotePath: String,
        filename: String,
        etag: String,
        contentType: String,
        size: Int64,
        lastModified: Date?,
        creationDate: Date?,
        isDirectory: Bool,
        permissions: String,
        ownerId: String,
        ownerDisplayName: String,
        isDownloaded: Bool,
        isDownloading: Bool,
        isUploaded: Bool,
        isUploading: Bool,
        status: ItemStatus,
        statusError: String?,
        syncTime: Date,
        finderMetadata: FinderMetadata = FinderMetadata()
    ) {
        self.ocId = ocId
        self.fileId = fileId
        self.parentOcId = parentOcId
        self.remotePath = remotePath
        self.filename = filename
        self.etag = etag
        self.contentType = contentType
        self.size = size
        self.lastModified = lastModified
        self.creationDate = creationDate
        self.isDirectory = isDirectory
        self.permissions = permissions
        self.ownerId = ownerId
        self.ownerDisplayName = ownerDisplayName
        self.isDownloaded = isDownloaded
        self.isDownloading = isDownloading
        self.isUploaded = isUploaded
        self.isUploading = isUploading
        self.status = status
        self.statusError = statusError
        self.syncTime = syncTime
        self.finderMetadata = finderMetadata
    }
}

/// Status of an item
enum ItemStatus: Int, Sendable {
    case normal = 0
    case downloading = 1
    case uploading = 2
    case downloadError = 3
    case uploadError = 4
}

/// User-private metadata supplied by Finder. It is not published as shared DAV properties.
struct FinderMetadata: Codable, Sendable, Equatable {
    var tagData: Data?
    var lastUsedDate: Date?
    var creationDate: Date?
    var contentModificationDate: Date?
    var trash: TrashMetadata?
    var contentPolicy: Int = 0
    var fileSystemFlags: UInt?
    var extendedAttributes: [String: Data]?

    enum Field: Hashable, Sendable {
        case tagData, lastUsedDate, contentPolicy, fileSystemFlags, extendedAttributes, creationDate, contentModificationDate, trash
    }

    mutating func apply(_ value: FinderMetadata, fields: Set<Field>) {
        if fields.contains(.creationDate) { creationDate = value.creationDate }
        if fields.contains(.contentModificationDate) { contentModificationDate = value.contentModificationDate }
        if fields.contains(.trash) { trash = value.trash }
        if fields.contains(.tagData) { tagData = value.tagData }
        if fields.contains(.lastUsedDate) { lastUsedDate = value.lastUsedDate }
        if fields.contains(.contentPolicy) { contentPolicy = value.contentPolicy }
        if fields.contains(.fileSystemFlags) { fileSystemFlags = value.fileSystemFlags }
        if fields.contains(.extendedAttributes) { extendedAttributes = value.extendedAttributes }
    }
}

struct TrashMetadata: Codable, Sendable, Equatable {
    let key: String
    let originalPath: String
    let originalParentOcId: String
    let deletionDate: Date?
}
