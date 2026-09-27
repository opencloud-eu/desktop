import Foundation
import FileProvider
import CryptoKit

/// Bridge Finder's trash hierarchy to OpenCloud's recoverable recycle-bin API.
enum FileProviderTrash {
    static let containerIdentifier = NSFileProviderItemIdentifier.trashContainer.rawValue

    private static func syntheticIdentifier(_ key: String) -> String {
        "trash:" + SHA256.hash(data: Data(key.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    /// Trash flattens unrelated original folders, so its names must be unique on
    /// macOS even when the server retained several copies of the same basename.
    static func displayNames(for entries: [WebDAVTrashItem]) -> [String: String] {
        var names: [String: String] = [:]
        var used = Set<String>()
        func comparisonKey(_ name: String) -> String { name.precomposedStringWithCanonicalMapping.lowercased() }
        for entry in entries.sorted(by: { $0.key < $1.key }) {
            var name = entry.filename
            if used.contains(comparisonKey(name)) {
                let originalExtension = (name as NSString).pathExtension
                let fileExtension = originalExtension.utf8.count <= 32 ? originalExtension : ""
                var stem = fileExtension.isEmpty ? name : (name as NSString).deletingPathExtension
                while stem.utf8.count > 180 { stem.removeLast() }
                let key = String(syntheticIdentifier(entry.key).dropFirst(6).prefix(12))
                var attempt = 0
                repeat {
                    let suffix = attempt == 0 ? key : key + "-" + String(attempt)
                    name = stem + " (deleted " + suffix + ")" + (fileExtension.isEmpty ? "" : "." + fileExtension)
                    attempt += 1
                } while used.contains(comparisonKey(name))
            }
            used.insert(comparisonKey(name))
            names[entry.key] = name
        }
        return names
    }

    /// Recover the trash ancestry from durable private metadata after cache loss.
    static func resolve(identifier: String, webdav: WebDAVClient, database: ItemDatabase) async throws -> ItemMetadata? {
        guard await webdav.supportsTrash else { return nil }
        let saved = try await database.finderMetadata(ocId: identifier).trash
        var children = try await refresh(webdav: webdav, database: database)
        if let item = children.first(where: { $0.ocId == identifier }) { return item }
        guard let saved = saved else { return nil }
        let segments = saved.key.split(separator: "/")
        var key = ""
        for (index, segment) in segments.enumerated() {
            key += (key.isEmpty ? "" : "/") + segment
            guard let child = children.first(where: { $0.finderMetadata.trash?.key == key }) else { return nil }
            if index == segments.count - 1 { return child.ocId == identifier ? child : nil }
            guard child.isDirectory else { return nil }
            children = try await refresh(webdav: webdav, database: database, parent: child)
        }
        return nil
    }

    static func refresh(webdav: WebDAVClient, database: ItemDatabase, parent: ItemMetadata? = nil) async throws -> [ItemMetadata] {
        let key = parent?.finderMetadata.trash?.key ?? ""
        let parentID = parent?.ocId ?? containerIdentifier
        let started = Date()
        let previous = await database.childItems(parentOcId: parentID)
        let previousKeys = Dictionary(previous.compactMap { item in
            item.finderMetadata.trash.map { ($0.key, item.ocId) }
        }, uniquingKeysWith: { first, _ in first })
        let entries = try await webdav.listTrash(key: key)
        let names = displayNames(for: entries)
        var updated: [ItemMetadata] = []
        for entry in entries {
            try Task.checkCancellation()
            let knownID = try await database.identifierForTrashKey(entry.key)
            let cached = await database.itemMetadata(remotePath: "/.opencloud-trash/" + entry.key)
            let id = cached?.ocId ?? knownID ?? syntheticIdentifier(entry.key)
            let existing = await database.itemMetadata(ocId: id)
            if let previousID = previousKeys[entry.key], await database.itemMetadata(ocId: previousID) == nil {
                // A purge completed while this listing was in flight.
                continue
            }
            if let existing, existing.syncTime > started {
                // Restore or another refresh committed a newer location/state.
                if existing.parentOcId == parentID { updated.append(existing) }
                continue
            }
            let livePath = try await webdav.livePath(forTrashOriginalPath: entry.originalPath)
            let durable = try await database.finderMetadata(ocId: id)
            let originalParent = durable.trash?.originalParentOcId ?? existing?.parentOcId ?? ItemDatabase.rootContainerId
            let trash = TrashMetadata(key: entry.key, originalPath: livePath, originalParentOcId: originalParent, deletionDate: entry.deletionDate)
            let metadata = ItemMetadata(ocId: id, fileId: existing?.fileId ?? "", parentOcId: parentID,
                remotePath: "/.opencloud-trash/" + entry.key, filename: names[entry.key] ?? entry.filename,
                etag: existing?.etag ?? "", contentType: existing?.contentType ?? "", size: entry.size,
                lastModified: existing?.lastModified, creationDate: existing?.creationDate,
                isDirectory: entry.isDirectory, permissions: "DV", ownerId: existing?.ownerId ?? "",
                ownerDisplayName: existing?.ownerDisplayName ?? "", isDownloaded: existing?.isDownloaded ?? false,
                isDownloading: false, isUploaded: true, isUploading: false, status: .normal,
                statusError: nil, syncTime: Date())
            if let item = try await database.mergeTrashSnapshot(metadata, trash: trash, fetchedAfter: started,
                                                               previousIdentifier: previousKeys[entry.key]) {
                updated.append(item)
            }
        }
        let ids = Set(updated.map(\.ocId))
        for old in previous where !ids.contains(old.ocId) { try await database.deleteIfUnchanged(old) }
        return updated
    }

    static func trash(metadata: ItemMetadata, webdav: WebDAVClient, database: ItemDatabase) async throws -> ItemMetadata {
        if metadata.isTrashed { return metadata }
        guard await webdav.supportsTrash else { throw CocoaError(.featureUnsupported) }
        // Both supported OpenCloud storage drivers use the opaque resource ID as
        // recycle key. Refuse a provider lacking that ID before deleting anything.
        let serverID = metadata.fileId.isEmpty ? metadata.ocId : metadata.fileId
        guard !serverID.hasPrefix("local:"), !serverID.hasPrefix("path:"), !serverID.hasPrefix("trash:") else {
            throw CocoaError(.featureUnsupported)
        }
        let rawID = serverID.hasPrefix("fileid:") ? String(serverID.dropFirst(7)) : serverID
        let key = rawID.split(separator: "!").last.map(String.init) ?? rawID
        guard !key.isEmpty, !key.contains("/") else { throw CocoaError(.featureUnsupported) }
        let prior = try await webdav.listTrash()
        if !prior.contains(where: { $0.key == key }) {
            try await webdav.deleteItem(at: metadata.remotePath, ifMatchEtag: metadata.etag)
        } else {
            // A previous callback may already have deleted this resource. A live
            // replacement at the old path must never be deleted on retry.
            do {
                _ = try await webdav.listDirectory(path: metadata.remotePath)
                throw WebDAVError.conflict
            } catch WebDAVError.fileNotFound {}
        }
        let items = try await refresh(webdav: webdav, database: database)
        guard let trashed = items.first(where: { $0.finderMetadata.trash?.key == key }) else {
            throw WebDAVError.parseError("Deleted item is not present in the server recycle bin")
        }
        guard trashed.ocId == metadata.ocId else { throw WebDAVError.parseError("Trash identity did not match the deleted resource") }
        return trashed
    }

    static func restore(metadata: ItemMetadata, to path: String, parentOcId: String, webdav: WebDAVClient, database: ItemDatabase) async throws -> ItemMetadata {
        guard let trash = metadata.finderMetadata.trash else { throw CocoaError(.featureUnsupported) }
        do {
            try await webdav.restoreTrash(key: trash.key, to: path)
        } catch {
            // A committed MOVE with a lost response can be acknowledged only by
            // the stable server ID, never by a name or matching file contents.
            guard let candidate = try? await webdav.listDirectory(path: path).first else { throw error }
            let opaque = candidate.fileId.split(separator: "!").last.map(String.init)
            guard (!metadata.fileId.isEmpty && candidate.fileId == metadata.fileId)
                    || (!trash.key.contains("/") && opaque == trash.key) else { throw error }
        }
        guard let remote = try await webdav.listDirectory(path: path).first else { throw WebDAVError.fileNotFound }
        let restored = try await database.mergeServerMetadata(ItemMetadata(from: remote, parentOcId: parentOcId), preservingIdentifier: metadata.ocId)
        let item = try await database.storeTrashItem(restored, trash: nil)
        return item
    }

    static func purge(metadata: ItemMetadata, webdav: WebDAVClient, database: ItemDatabase) async throws {
        guard let trash = metadata.finderMetadata.trash else { throw CocoaError(.featureUnsupported) }
        try await webdav.purgeTrash(key: trash.key)
        if metadata.isDirectory { try await database.deleteDirectoryAndSubdirectories(ocId: metadata.ocId) }
        else { try await database.deleteItemMetadata(ocId: metadata.ocId) }
    }
}
