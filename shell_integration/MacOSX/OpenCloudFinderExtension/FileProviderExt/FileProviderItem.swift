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

import FileProvider
import CryptoKit
import UniformTypeIdentifiers

/// Implementation of NSFileProviderItem protocol representing a file or folder.
/// Initialized from ItemMetadata (database) or directly for special containers.
final class FileProviderItem: NSObject, NSFileProviderItem {

    // MARK: - Stored Properties

    /// The item's metadata from database
    let metadata: ItemMetadata?

    // MARK: - Required Properties

    let itemIdentifier: NSFileProviderItemIdentifier
    let parentItemIdentifier: NSFileProviderItemIdentifier
    let filename: String

    // MARK: - Optional Properties

    let contentType: UTType
    let documentSize: NSNumber?
    let creationDate: Date?
    let contentModificationDate: Date?
    private let _etag: String
    private let _permissions: String
    private let _isDownloaded: Bool
    private let _isDownloading: Bool
    private let _isUploaded: Bool
    private let _isUploading: Bool
    private let supportsTrash: Bool

    var capabilities: NSFileProviderItemCapabilities {
        if itemIsTrashed {
            var caps: NSFileProviderItemCapabilities = [.allowsDeleting, .allowsReparenting, .allowsRenaming]
            if contentType == .folder { caps.insert(.allowsContentEnumerating) }
            return caps
        }
        var caps: NSFileProviderItemCapabilities = [.allowsReading]
        let perms = _permissions.uppercased()

        // Reading is implicit in oc/oCIS: if PROPFIND returns the item, it's readable.
        // Folders always need content enumeration.
        if contentType == .folder {
            caps.insert(.allowsContentEnumerating)
        }

        // D = deletable
        if perms.contains("D") {
            caps.insert(.allowsDeleting)
            if supportsTrash { caps.insert(.allowsTrashing) }
        }

        // W = writable (for files)
        if perms.contains("W"), contentType != .folder {
            caps.insert(.allowsWriting)
        }

        // NV = renameable, moveable
        if perms.contains("N") { caps.insert(.allowsRenaming) }
        if perms.contains("V") { caps.insert(.allowsReparenting) }

        // CK = folder allows adding sub-items
        if (perms.contains("C") || perms.contains("K")), contentType == .folder {
            caps.insert(.allowsAddingSubItems)
        }

        return caps
    }

    var userInfo: [AnyHashable: Any]? {
        ["openCloudActions": metadata != nil && !itemIsTrashed,
         "openCloudCanShowVersions": metadata != nil && !itemIsTrashed && metadata?.isDirectory == false,
         "openCloudCanShare": metadata != nil && !itemIsTrashed && (metadata?.permissions.uppercased().contains("R") ?? false)]
    }

    var itemIsTrashed: Bool { metadata?.isTrashed ?? false }

    var contentPolicy: NSFileProviderContentPolicy {
        if itemIdentifier == .rootContainer { return .downloadLazily }
        return NSFileProviderContentPolicy(rawValue: metadata?.finderMetadata.contentPolicy ?? 0) ?? .inherited
    }
    var tagData: Data? { metadata?.finderMetadata.tagData }
    var lastUsedDate: Date? { metadata?.finderMetadata.lastUsedDate }
    var extendedAttributes: [String: Data] { metadata?.finderMetadata.extendedAttributes ?? [:] }
    var fileSystemFlags: NSFileProviderFileSystemFlags {
        // Restoring or purging a child needs write permission on its local
        // trash directory even though the server does not support content edits.
        if itemIdentifier == .trashContainer || (itemIsTrashed && contentType == .folder) {
            return [.userReadable, .userWritable, .userExecutable]
        }
        if let raw = metadata?.finderMetadata.fileSystemFlags { return NSFileProviderFileSystemFlags(rawValue: raw) }
        var flags: NSFileProviderFileSystemFlags = [.userReadable]
        if capabilities.contains(.allowsWriting) || capabilities.contains(.allowsAddingSubItems) { flags.insert(.userWritable) }
        if contentType == .folder { flags.insert(.userExecutable) }
        return flags
    }
    var uploadingError: Error? {
        metadata?.status == .uploadError ? NSFileProviderError(.cannotSynchronize) : nil
    }
    var downloadingError: Error? {
        metadata?.status == .downloadError ? NSFileProviderError(.cannotSynchronize) : nil
    }

    var itemVersion: NSFileProviderItemVersion {
        // Use ETag for content version (consistent with server)
        let contentData = _etag.data(using: .utf8) ?? Data()
        // ETags describe content; rename, permissions and transfer state can
        // change independently and must invalidate the metadata version too.
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        let localMetadata = (try? encoder.encode(metadata?.finderMetadata ?? FinderMetadata()).base64EncodedString()) ?? ""
        let values = [_etag, filename, parentItemIdentifier.rawValue, _permissions,
                      contentType.identifier, documentSize?.stringValue ?? "",
                      creationDate.map { String($0.timeIntervalSince1970) } ?? "",
                      contentModificationDate.map { String($0.timeIntervalSince1970) } ?? "",
                      String(_isDownloaded), String(_isDownloading), String(_isUploaded), String(_isUploading),
                      localMetadata, metadata?.statusError ?? "", String(metadata?.status.rawValue ?? 0)]
        let metadataData = (try? JSONSerialization.data(withJSONObject: values)) ?? Data()
        // FileProvider limits each version component to 128 bytes.
        return NSFileProviderItemVersion(contentVersion: contentData, metadataVersion: Data(SHA256.hash(data: metadataData)))
    }

    // MARK: - Download/Upload State

    var isDownloaded: Bool {
        // Directories are always "downloaded"
        return contentType == .folder || _isDownloaded
    }

    var isDownloading: Bool { _isDownloading }
    var isUploaded: Bool { _isUploaded }
    var isUploading: Bool { _isUploading }

    // MARK: - Initialization from ItemMetadata

    init(metadata: ItemMetadata, parentItemIdentifier: NSFileProviderItemIdentifier, supportsTrash: Bool = false) {
        self.supportsTrash = supportsTrash
        self.metadata = metadata
        self.itemIdentifier = NSFileProviderItemIdentifier(metadata.ocId)
        self.parentItemIdentifier = parentItemIdentifier
        self.filename = metadata.filename

        // Determine content type
        if metadata.isDirectory {
            self.contentType = .folder
        } else if metadata.contentType == "httpd/unix-directory" {
            self.contentType = .folder
        } else if !metadata.contentType.isEmpty, let type = UTType(mimeType: metadata.contentType) {
            self.contentType = type
        } else {
            // Fallback to extension-based detection
            let ext = (metadata.filename as NSString).pathExtension
            self.contentType = UTType(filenameExtension: ext) ?? .data
        }

        self.documentSize = metadata.isDirectory ? nil : NSNumber(value: metadata.size)
        // Unknown dates stay unknown; a cache refresh must not invent a metadata change.
        self.creationDate = metadata.finderMetadata.creationDate ?? metadata.creationDate
        self.contentModificationDate = metadata.finderMetadata.contentModificationDate ?? metadata.lastModified
        // Use stable deterministic fallback when server doesn't provide ETag.
        // Random UUIDs cause contentVersion to differ every time the item is
        // constructed, making the system think content constantly changes.
        self._etag = metadata.etag.isEmpty
            ? "stable-" + SHA256.hash(data: Data(metadata.ocId.utf8)).map { String(format: "%02x", $0) }.joined()
            : metadata.etag
        // Provide default permissions if empty - folders need enumeration, files need reading
        self._permissions = metadata.permissions.isEmpty ? (metadata.isDirectory ? "RGDNVCK" : "RGDNVW") : metadata.permissions
        self._isDownloaded = metadata.isDownloaded
        self._isDownloading = metadata.isDownloading
        self._isUploaded = metadata.isUploaded
        self._isUploading = metadata.isUploading

        super.init()
    }

    // MARK: - Initialization for Special Containers

    /// Create a root container item
    static func rootContainer(supportsCreating: Bool = true) -> FileProviderItem {
        return FileProviderItem(
            identifier: .rootContainer,
            parentIdentifier: .rootContainer,
            filename: "OpenCloud",
            contentType: .folder,
            etag: "root",
            permissions: supportsCreating ? "GCK" : "G"
        )
    }

    /// Create a trash container item
    static func trashContainer() -> FileProviderItem {
        return FileProviderItem(
            identifier: .trashContainer,
            parentIdentifier: .trashContainer,
            filename: "Trash",
            contentType: .folder,
            etag: "trash",
            permissions: "G"
        )
    }

    /// Direct initialization for special containers
    private init(
        identifier: NSFileProviderItemIdentifier,
        parentIdentifier: NSFileProviderItemIdentifier,
        filename: String,
        contentType: UTType,
        documentSize: Int64 = 0,
        creationDate: Date? = nil,
        contentModificationDate: Date? = nil,
        etag: String = "",
        permissions: String = "RGDNVW",
        isDownloaded: Bool = true,
        isDownloading: Bool = false,
        isUploaded: Bool = true,
        isUploading: Bool = false
    ) {
        self.metadata = nil
        self.supportsTrash = false
        self.itemIdentifier = identifier
        self.parentItemIdentifier = parentIdentifier
        self.filename = filename
        self.contentType = contentType
        self.documentSize = NSNumber(value: documentSize)
        self.creationDate = creationDate
        self.contentModificationDate = contentModificationDate
        self._etag = etag
        self._permissions = permissions
        self._isDownloaded = isDownloaded
        self._isDownloading = isDownloading
        self._isUploaded = isUploaded
        self._isUploading = isUploading

        super.init()
    }
}
