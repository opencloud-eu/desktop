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
import SQLite3

/// SQLite database for storing item metadata.
/// Thread-safe via actor isolation.
actor ItemDatabase {

    private let logger = Logger(subsystem: "eu.opencloud.desktop.FileProviderExt", category: "ItemDatabase")

    /// SQLite database handle
    private var db: OpaquePointer?

    /// Database file URL
    private let databaseURL: URL

    /// Identity survives metadata-cache replacement. Id-less DAV resources have path
    /// identity until an observed deletion; only a confirmed local MOVE changes it.
    private struct Identity {
        var identifier: String
        var path: String
        var serverIdentifier: String
    }
    private let identitiesURL: URL

    /// Root container identifier (special value)
    static let rootContainerId = "rootContainer"

    init(containerURL: URL, domainIdentifier: String, changeRetentionLimit: Int = 100_000) throws {
        // Create database in the container's support directory
        let supportDir = containerURL.appendingPathComponent("FileProvider", isDirectory: true)
        try FileManager.default.createDirectory(at: supportDir, withIntermediateDirectories: true)

        // Use domain identifier in filename to separate databases per account
        let dbName = "items-\(domainIdentifier).sqlite"
        let dbURL = supportDir.appendingPathComponent(dbName)
        self.databaseURL = dbURL
        self.identitiesURL = supportDir.appendingPathComponent("identities-\(domainIdentifier).sqlite")

        logger.info("Opening database at \(dbURL.path)")

        // Open or create database
        var dbHandle: OpaquePointer?
        if sqlite3_open(dbURL.path, &dbHandle) != SQLITE_OK {
            let error = String(cString: sqlite3_errmsg(dbHandle))
            sqlite3_close(dbHandle)
            throw DatabaseError.openFailed(error)
        }
        sqlite3_busy_timeout(dbHandle, 5_000)
        self.db = dbHandle

        // Attach the durable identity store so metadata and path changes commit
        // together, while replacing the metadata cache does not discard identities.
        var attach: OpaquePointer?
        guard sqlite3_prepare_v2(dbHandle, "ATTACH DATABASE ? AS identities", -1, &attach, nil) == SQLITE_OK else {
            throw DatabaseError.openFailed(String(cString: sqlite3_errmsg(dbHandle)))
        }
        sqlite3_bind_text(attach, 1, identitiesURL.path, -1, SQLITE_TRANSIENT)
        let attachResult = sqlite3_step(attach)
        sqlite3_finalize(attach)
        guard attachResult == SQLITE_DONE else {
            throw DatabaseError.openFailed(String(cString: sqlite3_errmsg(dbHandle)))
        }
        // Create tables using static helper (doesn't need actor isolation)
        try Self.createTables(db: dbHandle, changeRetentionLimit: max(1, changeRetentionLimit))
    }

    /// Static helper for table creation - doesn't require actor isolation
    private static func createTables(db: OpaquePointer?, changeRetentionLimit: Int) throws {
        let createSQL = """
            BEGIN IMMEDIATE;
            CREATE TABLE IF NOT EXISTS identities.item_identities (
                provider_id TEXT PRIMARY KEY,
                remote_path TEXT NOT NULL,
                server_id TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS identities.idx_identity_path ON item_identities(remote_path);
            CREATE INDEX IF NOT EXISTS identities.idx_identity_server ON item_identities(server_id);
            CREATE TABLE IF NOT EXISTS identities.recycle_keys (
                provider_id TEXT PRIMARY KEY,
                recycle_key TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS identities.idx_recycle_key ON recycle_keys(recycle_key);
            CREATE TABLE IF NOT EXISTS identities.finder_metadata (
                provider_id TEXT PRIMARY KEY,
                metadata TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS items (
                oc_id TEXT PRIMARY KEY,
                file_id TEXT NOT NULL,
                parent_oc_id TEXT NOT NULL,
                remote_path TEXT NOT NULL,
                filename TEXT NOT NULL,
                etag TEXT NOT NULL,
                content_type TEXT NOT NULL,
                size INTEGER NOT NULL,
                last_modified REAL,
                creation_date REAL,
                is_directory INTEGER NOT NULL,
                permissions TEXT NOT NULL,
                owner_id TEXT NOT NULL,
                owner_display_name TEXT NOT NULL,
                is_downloaded INTEGER NOT NULL DEFAULT 0,
                is_downloading INTEGER NOT NULL DEFAULT 0,
                is_uploaded INTEGER NOT NULL DEFAULT 1,
                is_uploading INTEGER NOT NULL DEFAULT 0,
                status INTEGER NOT NULL DEFAULT 0,
                status_error TEXT,
                sync_time REAL NOT NULL
            );

            CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_oc_id);
            CREATE INDEX IF NOT EXISTS idx_items_remote_path ON items(remote_path);
            INSERT OR IGNORE INTO identities.item_identities(provider_id, remote_path, server_id)
                SELECT oc_id, CASE WHEN remote_path = '/' THEN '/' ELSE rtrim(remote_path, '/') END, oc_id FROM items;

            INSERT OR IGNORE INTO identities.recycle_keys(provider_id, recycle_key)
                SELECT provider_id, CASE WHEN instr(resource_id, '!') > 0 THEN substr(resource_id, instr(resource_id, '!') + 1)
                    WHEN substr(resource_id, 1, 7) = 'fileid:' THEN substr(resource_id, 8) ELSE resource_id END
                FROM (SELECT i.provider_id, CASE WHEN length(m.file_id) > 0 THEN m.file_id ELSE i.server_id END AS resource_id
                    FROM identities.item_identities i LEFT JOIN items m ON m.oc_id = i.provider_id)
                WHERE resource_id NOT LIKE 'path:%' AND resource_id NOT LIKE 'local:%' AND resource_id NOT LIKE 'trash:%';

            CREATE TABLE IF NOT EXISTS sync_state (epoch TEXT NOT NULL);
            INSERT INTO sync_state(epoch) SELECT lower(hex(randomblob(16)))
                WHERE NOT EXISTS (SELECT 1 FROM sync_state);
            CREATE TABLE IF NOT EXISTS item_changes (
                sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                oc_id TEXT NOT NULL,
                parent_oc_id TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS sync_retention (minimum_sequence INTEGER NOT NULL);
            INSERT INTO sync_retention SELECT 0 WHERE NOT EXISTS (SELECT 1 FROM sync_retention);
            DROP TRIGGER IF EXISTS prune_item_changes;
            CREATE TRIGGER prune_item_changes AFTER INSERT ON item_changes
                WHEN NEW.sequence % \(max(1, min(1000, changeRetentionLimit / 10))) = 0
                BEGIN
                    UPDATE sync_retention SET minimum_sequence = max(minimum_sequence, NEW.sequence - \(changeRetentionLimit));
                    DELETE FROM item_changes WHERE sequence <= (SELECT minimum_sequence FROM sync_retention);
                END;
            UPDATE sync_retention SET minimum_sequence = max(minimum_sequence,
                COALESCE((SELECT MAX(sequence) FROM item_changes), 0) - \(changeRetentionLimit));
            DELETE FROM item_changes WHERE sequence <= (SELECT minimum_sequence FROM sync_retention);
            CREATE INDEX IF NOT EXISTS idx_changes_parent_sequence ON item_changes(parent_oc_id, sequence);
            CREATE TRIGGER IF NOT EXISTS items_before_replace BEFORE INSERT ON items
                WHEN EXISTS (SELECT 1 FROM items WHERE oc_id = NEW.oc_id AND parent_oc_id != NEW.parent_oc_id)
                BEGIN
                    INSERT INTO item_changes(oc_id, parent_oc_id)
                        SELECT oc_id, parent_oc_id FROM items WHERE oc_id = NEW.oc_id;
                END;
            CREATE TRIGGER IF NOT EXISTS items_insert AFTER INSERT ON items BEGIN
                INSERT INTO item_changes(oc_id, parent_oc_id) VALUES (NEW.oc_id, NEW.parent_oc_id);
            END;
            CREATE TRIGGER IF NOT EXISTS items_update AFTER UPDATE ON items
                WHEN OLD.remote_path IS NOT NEW.remote_path
                  OR OLD.is_downloaded IS NOT NEW.is_downloaded
                  OR OLD.is_downloading IS NOT NEW.is_downloading
                  OR OLD.is_uploading IS NOT NEW.is_uploading
                  OR OLD.is_uploaded IS NOT NEW.is_uploaded
                  OR OLD.status IS NOT NEW.status
                  OR OLD.status_error IS NOT NEW.status_error
                  OR OLD.size IS NOT NEW.size
                BEGIN
                INSERT INTO item_changes(oc_id, parent_oc_id) VALUES (NEW.oc_id, NEW.parent_oc_id);
            END;
            CREATE TRIGGER IF NOT EXISTS items_delete AFTER DELETE ON items BEGIN
                INSERT INTO item_changes(oc_id, parent_oc_id) VALUES (OLD.oc_id, OLD.parent_oc_id);
            END;
            COMMIT;
            """

        var errMsg: UnsafeMutablePointer<CChar>?
        if sqlite3_exec(db, createSQL, nil, nil, &errMsg) != SQLITE_OK {
            let error = errMsg != nil ? String(cString: errMsg!) : "Unknown error"
            sqlite3_free(errMsg)
            sqlite3_exec(db, "ROLLBACK", nil, nil, nil)
            throw DatabaseError.createTableFailed(error)
        }
    }

    deinit {
        if db != nil {
            sqlite3_close(db)
        }
    }

    // MARK: - CRUD Operations

    /// Get item metadata by ocId
    func itemMetadata(ocId: String) -> ItemMetadata? {
        let sql = "SELECT * FROM items WHERE oc_id = ?"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            logger.error("Failed to prepare SELECT statement")
            return nil
        }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_text(stmt, 1, ocId, -1, SQLITE_TRANSIENT)

        guard sqlite3_step(stmt) == SQLITE_ROW else {
            return nil
        }

        return metadataFromRow(stmt)
    }

    /// Get item metadata by remote path
    func itemMetadata(remotePath: String) -> ItemMetadata? {
        let sql = "SELECT * FROM items WHERE remote_path = ?"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            logger.error("Failed to prepare SELECT statement")
            return nil
        }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_text(stmt, 1, remotePath, -1, SQLITE_TRANSIENT)

        guard sqlite3_step(stmt) == SQLITE_ROW else {
            return nil
        }

        return metadataFromRow(stmt)
    }

    /// Get all children of a parent
    func childItems(parentOcId: String) -> [ItemMetadata] {
        let sql = "SELECT * FROM items WHERE parent_oc_id = ? ORDER BY is_directory DESC, filename ASC"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            logger.error("Failed to prepare SELECT statement")
            return []
        }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_text(stmt, 1, parentOcId, -1, SQLITE_TRANSIENT)

        var items: [ItemMetadata] = []
        while sqlite3_step(stmt) == SQLITE_ROW {
            if let metadata = metadataFromRow(stmt) {
                items.append(metadata)
            }
        }

        return items
    }

    /// Store metadata and keep cached descendant paths valid after a directory move.
    func addItemMetadata(_ metadata: ItemMetadata) throws {
        let existing = itemMetadata(ocId: metadata.ocId)
        if var existing = existing {
            existing.syncTime = metadata.syncTime
            if existing == metadata { return }
        }
        guard let existing = existing, existing.isDirectory,
              existing.remotePath != metadata.remotePath else {
            try storeItemMetadata(metadata)
            return
        }

        let ownsTransaction = sqlite3_get_autocommit(db) != 0
        if ownsTransaction { try executeUpdate("BEGIN IMMEDIATE") }
        do {
            try storeItemMetadata(metadata)
            let oldPrefix = existing.remotePath.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            let newPrefix = metadata.remotePath.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            let oldPath = "/" + oldPrefix + "/"
            let newPath = "/" + newPrefix + "/"
            // substr avoids interpreting '%' and '_' in filenames as SQL wildcards.
            let sql = "UPDATE items SET remote_path = ? || substr(remote_path, ?) WHERE substr(remote_path, 1, ?) = ?"
            var stmt: OpaquePointer?
            guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
                throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db)))
            }
            defer { sqlite3_finalize(stmt) }
            sqlite3_bind_text(stmt, 1, newPath, -1, SQLITE_TRANSIENT)
            sqlite3_bind_int(stmt, 2, Int32(oldPath.unicodeScalars.count + 1))
            sqlite3_bind_int(stmt, 3, Int32(oldPath.unicodeScalars.count))
            sqlite3_bind_text(stmt, 4, oldPath, -1, SQLITE_TRANSIENT)
            guard sqlite3_step(stmt) == SQLITE_DONE else {
                throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db)))
            }
            // Descendant identity paths must follow a directory even if the cache
            // disappears before any child is accessed again.
            try executeBoundUpdate("UPDATE identities.item_identities SET remote_path = ? || substr(remote_path, length(?) + 1) WHERE substr(remote_path, 1, length(?)) = ?", values: [newPath, oldPath, oldPath, oldPath])
            if ownsTransaction { try executeUpdate("COMMIT") }
        } catch {
            if ownsTransaction { try? executeUpdate("ROLLBACK") }
            throw error
        }
    }

    /// Merge server fields inside the actor so a concurrent download cannot lose its state.
    func mergeServerMetadata(_ metadata: ItemMetadata, fetchedAfter: Date? = nil, preservingIdentifier: String? = nil) throws -> ItemMetadata {
        var merged = metadata
        let atPath = itemMetadata(remotePath: Self.normalizedPath(metadata.remotePath))
            ?? itemMetadata(remotePath: Self.normalizedPath(metadata.remotePath) + "/")
        // SQLite stores epoch doubles. Compare in that same representation and
        // preserve cached changes on equal ticks, whose ordering is ambiguous.
        if let atPath = atPath, let fetchedAfter = fetchedAfter, atPath.syncTime.timeIntervalSince1970 >= fetchedAfter.timeIntervalSince1970 {
            return atPath
        }
        if let preservingIdentifier = preservingIdentifier {
            merged.ocId = preservingIdentifier
        } else if let mapped = try providerIdentifier(serverIdentifier: metadata.ocId) {
            merged.ocId = mapped
        } else if metadata.ocId.hasPrefix("path:") {
            if let existing = itemMetadata(remotePath: Self.normalizedPath(metadata.remotePath))
                        ?? itemMetadata(remotePath: Self.normalizedPath(metadata.remotePath) + "/") {
                merged.ocId = existing.ocId
            } else if let existing = try identity(remotePath: metadata.remotePath), WebDAVItem.path(fromIdentifier: existing.serverIdentifier) != nil {
                merged.ocId = existing.identifier
            } else {
                merged.ocId = "local:" + UUID().uuidString.lowercased()
            }
        }
        if let existing = itemMetadata(ocId: merged.ocId) {
            if let fetchedAfter = fetchedAfter, existing.syncTime.timeIntervalSince1970 >= fetchedAfter.timeIntervalSince1970 { return existing }
            merged.isDownloaded = existing.isDownloaded
            merged.isDownloading = existing.isDownloading
            merged.isUploaded = existing.isUploaded
            merged.isUploading = existing.isUploading
            merged.status = existing.status
            merged.statusError = existing.statusError
        }
        try transaction {
            merged.finderMetadata = try localMetadata(ocId: merged.ocId)
            let clearsModificationOverride = itemMetadata(ocId: merged.ocId).map { $0.etag != merged.etag } == true
                && merged.finderMetadata.contentModificationDate != nil
            if clearsModificationOverride { merged.finderMetadata.contentModificationDate = nil }
            if clearsModificationOverride {
                _ = try updateLocalMetadata(ocId: merged.ocId, metadata: merged.finderMetadata, fields: [.contentModificationDate])
            }
            if let replaced = itemMetadata(remotePath: Self.normalizedPath(metadata.remotePath))
                ?? itemMetadata(remotePath: Self.normalizedPath(metadata.remotePath) + "/"), replaced.ocId != merged.ocId {
                // A different stable server identifier at this path is an observed
                // replacement, not a rename of the old resource.
                if replaced.isDirectory { try deleteDirectoryAndSubdirectories(ocId: replaced.ocId) }
                else { try deleteItemMetadata(ocId: replaced.ocId) }
            }
            if let previous = try identity(remotePath: metadata.remotePath), previous.identifier != merged.ocId {
                try executeBoundUpdate("DELETE FROM identities.recycle_keys WHERE provider_id = ?", values: [previous.identifier])
                try executeBoundUpdate("DELETE FROM identities.finder_metadata WHERE provider_id = ?", values: [previous.identifier])
                try executeBoundUpdate("DELETE FROM identities.item_identities WHERE provider_id = ?", values: [previous.identifier])
            }
            try rememberIdentity(merged, serverIdentifier: metadata.ocId)
            try addItemMetadata(merged)
        }
        return merged
    }

    /// Search results need their actual parent, including ancestors missing from the cache.
    func mergeSearchResult(_ remote: WebDAVItem, webdav: WebDAVClient, fetchedAfter: Date? = nil) async throws -> ItemMetadata {
        try Task.checkCancellation()
        let identifier = try providerIdentifier(serverIdentifier: remote.ocId) ?? remote.ocId
        if let fetchedAfter, let current = itemMetadata(ocId: identifier), current.syncTime.timeIntervalSince1970 >= fetchedAfter.timeIntervalSince1970 { return current }
        let parent = try await resolveParent(path: remote.parentPath, webdav: webdav)
        return try mergeServerMetadata(ItemMetadata(from: remote, parentOcId: parent), fetchedAfter: fetchedAfter)
    }

    func finderMetadata(ocId: String) throws -> FinderMetadata { try localMetadata(ocId: ocId) }

    private func localMetadata(ocId: String) throws -> FinderMetadata {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, "SELECT metadata FROM identities.finder_metadata WHERE provider_id = ?", -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        sqlite3_bind_text(stmt, 1, ocId, -1, SQLITE_TRANSIENT)
        let result = sqlite3_step(stmt)
        if result == SQLITE_DONE { return FinderMetadata() }
        guard result == SQLITE_ROW else { throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db))) }
        return try JSONDecoder().decode(FinderMetadata.self, from: Data(columnString(stmt, 0).utf8))
    }

    /// Apply only acknowledged Finder fields. An included nil explicitly clears that field.
    /// The transaction prevents independent extension processes from losing each other's edits.
    func updateLocalMetadata(ocId: String, metadata: FinderMetadata, fields: Set<FinderMetadata.Field>) throws -> ItemMetadata? {
        try transaction {
            guard var item = itemMetadata(ocId: ocId) else { return nil }
            var local = try localMetadata(ocId: ocId)
            let previous = local
            local.apply(metadata, fields: fields)
            guard local != previous else { return item }
            let encoded = try JSONEncoder().encode(local)
            try executeBoundUpdate("INSERT OR REPLACE INTO identities.finder_metadata(provider_id, metadata) VALUES (?, ?)",
                                   values: [ocId, String(decoding: encoded, as: UTF8.self)])
            try executeBoundUpdate("INSERT INTO item_changes(oc_id, parent_oc_id) VALUES (?, ?)", values: [ocId, item.parentOcId])
            item.finderMetadata = local
            if fields.contains(.trash) { try rememberRecycleKey(item) }
            return item
        }
    }

    private func providerIdentifier(serverIdentifier: String) throws -> String? {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, "SELECT provider_id FROM identities.item_identities WHERE server_id = ? LIMIT 1", -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        sqlite3_bind_text(stmt, 1, serverIdentifier, -1, SQLITE_TRANSIENT)
        return sqlite3_step(stmt) == SQLITE_ROW ? columnString(stmt, 0) : nil
    }

    /// Locate a recycle entry using its durable opaque key, never its filename.
    func identifierForTrashKey(_ key: String) throws -> String? {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, "SELECT provider_id FROM identities.recycle_keys WHERE recycle_key = ? LIMIT 1", -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT)
        return sqlite3_step(stmt) == SQLITE_ROW ? columnString(stmt, 0) : nil
    }

    private func rememberRecycleKey(_ metadata: ItemMetadata) throws {
        let key: String
        if let trash = metadata.finderMetadata.trash { key = trash.key }
        else {
            let raw = metadata.fileId.isEmpty ? (try identity(identifier: metadata.ocId))?.serverIdentifier ?? metadata.ocId : metadata.fileId
            guard !raw.hasPrefix("path:"), !raw.hasPrefix("local:"), !raw.hasPrefix("trash:") else { return }
            let server = raw.hasPrefix("fileid:") ? String(raw.dropFirst(7)) : raw
            key = server.split(separator: "!").last.map(String.init) ?? server
        }
        try executeBoundUpdate("INSERT OR REPLACE INTO identities.recycle_keys(provider_id, recycle_key) VALUES (?, ?)", values: [metadata.ocId, key])
    }

    /// Commit a server trash snapshot only if no later restore/purge won the race.
    func mergeTrashSnapshot(_ metadata: ItemMetadata, trash: TrashMetadata, fetchedAfter: Date, previousIdentifier: String?) throws -> ItemMetadata? {
        try transaction {
            if let previousIdentifier, itemMetadata(ocId: previousIdentifier) == nil { return nil }
            if let current = itemMetadata(ocId: metadata.ocId), current.syncTime.timeIntervalSince1970 >= fetchedAfter.timeIntervalSince1970 {
                return current.parentOcId == metadata.parentOcId ? current : nil
            }
            return try storeTrashItem(metadata, trash: trash)
        }
    }

    /// Keep trash state and parent transition atomic for working-set observers.
    func storeTrashItem(_ metadata: ItemMetadata, trash: TrashMetadata?) throws -> ItemMetadata {
        try transaction {
            var value = metadata
            value.finderMetadata = try localMetadata(ocId: value.ocId)
            value.finderMetadata.trash = trash
            // Preserve the original server ID for stable reconciliation after restore.
            if try identity(identifier: value.ocId) == nil { try rememberIdentity(value, serverIdentifier: value.ocId) }
            try addItemMetadata(value)
            let stored = try updateLocalMetadata(ocId: value.ocId, metadata: value.finderMetadata, fields: [.trash]) ?? value
            if stored.isDirectory { try setDescendantTrashState(of: stored, trash: trash) }
            return stored
        }
    }

    func setDescendantTrashState(of directory: ItemMetadata, trash: TrashMetadata?) throws {
        try transaction {
            var pending = childItems(parentOcId: directory.ocId)
            while let child = pending.popLast() {
                if child.isDirectory { pending.append(contentsOf: childItems(parentOcId: child.ocId)) }
                var local = child.finderMetadata
                if let trash = trash {
                    let prefix = directory.remotePath.hasSuffix("/") ? directory.remotePath : directory.remotePath + "/"
                    guard child.remotePath.hasPrefix(prefix) else { continue }
                    let suffix = String(child.remotePath.dropFirst(prefix.count))
                    local.trash = TrashMetadata(key: trash.key + "/" + suffix,
                        originalPath: trash.originalPath + "/" + suffix, originalParentOcId: child.parentOcId,
                        deletionDate: trash.deletionDate)
                } else { local.trash = nil }
                _ = try updateLocalMetadata(ocId: child.ocId, metadata: local, fields: [.trash])
            }
        }
    }

    private static func normalizedPath(_ path: String) -> String {
        path == "/" ? path : "/" + path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
    }

    private func identity(identifier: String? = nil, remotePath: String? = nil) throws -> Identity? {
        var stmt: OpaquePointer?
        let sql = identifier != nil
            ? "SELECT provider_id, remote_path, server_id FROM identities.item_identities WHERE provider_id = ?"
            : "SELECT provider_id, remote_path, server_id FROM identities.item_identities WHERE remote_path = ?"
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        sqlite3_bind_text(stmt, 1, identifier ?? Self.normalizedPath(remotePath!), -1, SQLITE_TRANSIENT)
        let result = sqlite3_step(stmt)
        if result == SQLITE_DONE { return nil }
        guard result == SQLITE_ROW else { throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db))) }
        return Identity(identifier: columnString(stmt, 0), path: columnString(stmt, 1), serverIdentifier: columnString(stmt, 2))
    }

    private func rememberIdentity(_ metadata: ItemMetadata, serverIdentifier: String) throws {
        try rememberRecycleKey(metadata)
        let existing = try identity(identifier: metadata.ocId)
        if existing?.path == Self.normalizedPath(metadata.remotePath), existing?.serverIdentifier == serverIdentifier { return }
        try executeBoundUpdate("INSERT OR REPLACE INTO identities.item_identities(provider_id, remote_path, server_id) VALUES (?, ?, ?)",
                               values: [metadata.ocId, Self.normalizedPath(metadata.remotePath), serverIdentifier])
    }

    private func executeBoundUpdate(_ sql: String, values: [String]) throws {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        for (index, value) in values.enumerated() { sqlite3_bind_text(stmt, Int32(index + 1), value, -1, SQLITE_TRANSIENT) }
        guard sqlite3_step(stmt) == SQLITE_DONE else { throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db))) }
    }

    private func transaction<T>(_ action: () throws -> T) throws -> T {
        let ownsTransaction = sqlite3_get_autocommit(db) != 0
        if ownsTransaction { try executeUpdate("BEGIN IMMEDIATE") }
        do {
            let value = try action()
            if ownsTransaction { try executeUpdate("COMMIT") }
            return value
        } catch {
            if ownsTransaction { try? executeUpdate("ROLLBACK") }
            throw error
        }
    }

    /// Resolve retained Finder identifiers without treating a missing cache row as deletion.
    /// Stable server IDs can be rediscovered; id-less resources use their retained path map.
    func resolveItem(identifier: String, webdav: WebDAVClient) async throws -> ItemMetadata? {
        if let cached = itemMetadata(ocId: identifier) { return cached }
        let identity = try identity(identifier: identifier)
        let knownPath = identity?.path ?? WebDAVItem.path(fromIdentifier: identifier)
        if let path = knownPath {
            do {
                try Task.checkCancellation()
                if let remote = try await webdav.listDirectory(path: path).first {
                    let sameResource = remote.ocId == identifier
                        || remote.ocId == identity?.serverIdentifier
                        || (remote.ocId.hasPrefix("path:") && identity.flatMap({ WebDAVItem.path(fromIdentifier: $0.serverIdentifier) }) != nil)
                        || (identity == nil && WebDAVItem.path(fromIdentifier: identifier) != nil && remote.ocId.hasPrefix("path:"))
                    if sameResource {
                        let parent = try await resolveParent(path: remote.parentPath, webdav: webdav)
                        return try mergeServerMetadata(ItemMetadata(from: remote, parentOcId: parent), preservingIdentifier: identifier)
                    }
                }
            } catch WebDAVError.fileNotFound {
                // A stable-ID resource may have moved while the extension was stopped.
            }
        }
        // Without a retained map there is no safe way to associate a local UUID
        // with another path. In particular, equal etags are not item identity.
        if identifier.hasPrefix("local:") { return nil }
        var pending: [(path: String, parent: String)] = [("/", Self.rootContainerId)]
        var visited = Set<String>()
        var index = 0
        while index < pending.count {
            try Task.checkCancellation()
            let folder = pending[index]
            index += 1
            guard visited.insert(folder.path).inserted else { continue }
            let listing: [WebDAVItem]
            do {
                listing = try await webdav.listDirectory(path: folder.path)
            } catch WebDAVError.fileNotFound { continue }
            for remote in listing.dropFirst() {
                try Task.checkCancellation()
                let metadata = try mergeServerMetadata(ItemMetadata(from: remote, parentOcId: folder.parent))
                if remote.ocId == identifier || metadata.ocId == identifier { return metadata }
                if metadata.isDirectory { pending.append((metadata.remotePath, metadata.ocId)) }
            }
        }
        return nil
    }

    private func resolveParent(path: String, webdav: WebDAVClient) async throws -> String {
        try Task.checkCancellation()
        if await webdav.isRootPath(path) { return Self.rootContainerId }
        if let cached = itemMetadata(remotePath: path) ?? itemMetadata(remotePath: path + "/") {
            return cached.ocId
        }
        guard let remote = try await webdav.listDirectory(path: path).first,
              remote.isDirectory, remote.parentPath != path else { throw WebDAVError.fileNotFound }
        let parent = try await resolveParent(path: remote.parentPath, webdav: webdav)
        return try mergeServerMetadata(ItemMetadata(from: remote, parentOcId: parent)).ocId
    }

    /// Keyset pages keep memory and observer batches bounded without OFFSET scans.
    func itemsPage(parentOcId: String?, after identifier: String? = nil, limit: Int = 500, directoriesOnly: Bool = false) throws -> (items: [ItemMetadata], next: String?) {
        var stmt: OpaquePointer?
        let sql = "SELECT * FROM items WHERE (? IS NULL OR parent_oc_id = ?) AND (? IS NULL OR oc_id > ?) AND (? = 0 OR is_directory = 1) ORDER BY oc_id LIMIT ?"
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        for index: Int32 in [1, 2] {
            if let parentOcId = parentOcId { sqlite3_bind_text(stmt, index, parentOcId, -1, SQLITE_TRANSIENT) }
            else { sqlite3_bind_null(stmt, index) }
        }
        for index: Int32 in [3, 4] {
            if let identifier = identifier { sqlite3_bind_text(stmt, index, identifier, -1, SQLITE_TRANSIENT) }
            else { sqlite3_bind_null(stmt, index) }
        }
        sqlite3_bind_int(stmt, 5, directoriesOnly ? 1 : 0)
        sqlite3_bind_int64(stmt, 6, Int64(max(1, limit) + 1))
        var items: [ItemMetadata] = []
        var result = sqlite3_step(stmt)
        while result == SQLITE_ROW {
            if let metadata = metadataFromRow(stmt) { items.append(metadata) }
            result = sqlite3_step(stmt)
        }
        guard result == SQLITE_DONE else { throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db))) }
        let more = items.count > max(1, limit)
        if more { items.removeLast() }
        return (items, more ? items.last?.ocId : nil)
    }

    private func executeUpdate(_ sql: String) throws {
        guard sqlite3_exec(db, sql, nil, nil, nil) == SQLITE_OK else {
            throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db)))
        }
    }

    private func storeItemMetadata(_ metadata: ItemMetadata) throws {
        let sql = """
            INSERT OR REPLACE INTO items (
                oc_id, file_id, parent_oc_id, remote_path, filename, etag,
                content_type, size, last_modified, creation_date, is_directory,
                permissions, owner_id, owner_display_name, is_downloaded,
                is_downloading, is_uploaded, is_uploading, status, status_error, sync_time
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            let error = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.insertFailed(error)
        }
        defer { sqlite3_finalize(stmt) }

        var col: Int32 = 1
        sqlite3_bind_text(stmt, col, metadata.ocId, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.fileId, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.parentOcId, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.remotePath, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.filename, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.etag, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.contentType, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_int64(stmt, col, metadata.size); col += 1

        if let lastModified = metadata.lastModified {
            sqlite3_bind_double(stmt, col, lastModified.timeIntervalSince1970)
        } else {
            sqlite3_bind_null(stmt, col)
        }
        col += 1

        if let creationDate = metadata.creationDate {
            sqlite3_bind_double(stmt, col, creationDate.timeIntervalSince1970)
        } else {
            sqlite3_bind_null(stmt, col)
        }
        col += 1

        sqlite3_bind_int(stmt, col, metadata.isDirectory ? 1 : 0); col += 1
        sqlite3_bind_text(stmt, col, metadata.permissions, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.ownerId, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_text(stmt, col, metadata.ownerDisplayName, -1, SQLITE_TRANSIENT); col += 1
        sqlite3_bind_int(stmt, col, metadata.isDownloaded ? 1 : 0); col += 1
        sqlite3_bind_int(stmt, col, metadata.isDownloading ? 1 : 0); col += 1
        sqlite3_bind_int(stmt, col, metadata.isUploaded ? 1 : 0); col += 1
        sqlite3_bind_int(stmt, col, metadata.isUploading ? 1 : 0); col += 1
        sqlite3_bind_int(stmt, col, Int32(metadata.status.rawValue)); col += 1

        if let statusError = metadata.statusError {
            sqlite3_bind_text(stmt, col, statusError, -1, SQLITE_TRANSIENT)
        } else {
            sqlite3_bind_null(stmt, col)
        }
        col += 1

        sqlite3_bind_double(stmt, col, metadata.syncTime.timeIntervalSince1970)

        if sqlite3_step(stmt) != SQLITE_DONE {
            let error = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.insertFailed(error)
        }
    }

    /// Ignore a stale directory response if another operation updated or moved the item.
    func deleteIfUnchanged(_ metadata: ItemMetadata) throws {
        guard itemMetadata(ocId: metadata.ocId) == metadata else { return }
        if metadata.isDirectory {
            try deleteDirectoryAndSubdirectories(ocId: metadata.ocId)
        } else {
            try deleteItemMetadata(ocId: metadata.ocId)
        }
    }

    /// Delete item metadata by ocId
    func deleteItemMetadata(ocId: String) throws {
        try transaction {
            try executeBoundUpdate("DELETE FROM identities.recycle_keys WHERE provider_id = ?", values: [ocId])
            try executeBoundUpdate("DELETE FROM identities.finder_metadata WHERE provider_id = ?", values: [ocId])
            try executeBoundUpdate("DELETE FROM identities.item_identities WHERE provider_id = ?", values: [ocId])
            try executeBoundUpdate("DELETE FROM items WHERE oc_id = ?", values: [ocId])
        }
    }

    /// Retire identity and cache rows together, including descendants of a deleted folder.
    func deleteDirectoryAndSubdirectories(ocId: String) throws {
        try transaction {
            if let directory = itemMetadata(ocId: ocId) {
                let prefix = directory.remotePath.hasSuffix("/") ? directory.remotePath : directory.remotePath + "/"
                try executeBoundUpdate("DELETE FROM identities.finder_metadata WHERE provider_id IN (SELECT provider_id FROM identities.item_identities WHERE provider_id = ? OR substr(remote_path, 1, length(?)) = ?)", values: [ocId, prefix, prefix])
                try executeBoundUpdate("DELETE FROM identities.item_identities WHERE provider_id = ? OR substr(remote_path, 1, length(?)) = ?", values: [ocId, prefix, prefix])
            }
            let sql = """
                WITH RECURSIVE descendants(oc_id) AS (
                    SELECT oc_id FROM items WHERE oc_id = ?
                    UNION
                    SELECT i.oc_id FROM items i INNER JOIN descendants d ON i.parent_oc_id = d.oc_id
                )
                DELETE FROM items WHERE oc_id IN (SELECT oc_id FROM descendants)
                """
            try executeBoundUpdate(sql, values: [ocId])
            try executeUpdate("DELETE FROM identities.recycle_keys WHERE provider_id NOT IN (SELECT provider_id FROM identities.item_identities) AND provider_id NOT IN (SELECT oc_id FROM items)")
            try executeUpdate("DELETE FROM identities.finder_metadata WHERE provider_id NOT IN (SELECT provider_id FROM identities.item_identities) AND provider_id NOT IN (SELECT oc_id FROM items)")
        }
    }

    /// Update download state
    func setDownloaded(ocId: String, downloaded: Bool) throws {
        let sql = "UPDATE items SET is_downloaded = ?, is_downloading = 0, status = 0 WHERE oc_id = ?"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            let error = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.updateFailed(error)
        }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_int(stmt, 1, downloaded ? 1 : 0)
        sqlite3_bind_text(stmt, 2, ocId, -1, SQLITE_TRANSIENT)

        if sqlite3_step(stmt) != SQLITE_DONE {
            let error = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.updateFailed(error)
        }
    }

    /// Completion of an older GET must not overwrite metadata already refreshed
    /// to a newer server version. Keep concurrent upload state intact as well.
    func finishDownload(ocId: String, matchingETag: String, size: Int64) throws {
        let sql = """
            UPDATE items SET is_downloaded = 1, is_downloading = 0, size = ?,
                status = CASE WHEN status IN (1, 3) THEN 0 ELSE status END,
                status_error = CASE WHEN status IN (1, 3) THEN NULL ELSE status_error END
            WHERE oc_id = ? AND etag = ?
            """
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        sqlite3_bind_int64(stmt, 1, size)
        sqlite3_bind_text(stmt, 2, ocId, -1, SQLITE_TRANSIENT)
        sqlite3_bind_text(stmt, 3, matchingETag, -1, SQLITE_TRANSIENT)
        guard sqlite3_step(stmt) == SQLITE_DONE else {
            throw DatabaseError.updateFailed(String(cString: sqlite3_errmsg(db)))
        }
    }

    /// Update file size after download
    func updateSize(ocId: String, size: Int64) throws {
        let sql = "UPDATE items SET size = ? WHERE oc_id = ?"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            let error = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.updateFailed(error)
        }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_int64(stmt, 1, size)
        sqlite3_bind_text(stmt, 2, ocId, -1, SQLITE_TRANSIENT)

        if sqlite3_step(stmt) != SQLITE_DONE {
            let error = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.updateFailed(error)
        }
    }

    /// Update status
    func setStatus(ocId: String, status: ItemStatus, error: String? = nil) throws {
        let sql = "UPDATE items SET status = ?, status_error = ?, is_downloading = ?, is_uploading = ? WHERE oc_id = ?"

        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            let err = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.updateFailed(err)
        }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_int(stmt, 1, Int32(status.rawValue))

        if let error = error {
            sqlite3_bind_text(stmt, 2, error, -1, SQLITE_TRANSIENT)
        } else {
            sqlite3_bind_null(stmt, 2)
        }

        sqlite3_bind_int(stmt, 3, status == .downloading ? 1 : 0)
        sqlite3_bind_int(stmt, 4, status == .uploading ? 1 : 0)
        sqlite3_bind_text(stmt, 5, ocId, -1, SQLITE_TRANSIENT)

        if sqlite3_step(stmt) != SQLITE_DONE {
            let err = String(cString: sqlite3_errmsg(db))
            throw DatabaseError.updateFailed(err)
        }
    }

    /// Get all downloaded (materialized) items
    func downloadedItems() -> [ItemMetadata] {
        let sql = "SELECT * FROM items WHERE is_downloaded = 1 ORDER BY is_directory DESC, filename ASC"
        var stmt: OpaquePointer?

        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            return []
        }
        defer { sqlite3_finalize(stmt) }

        var items: [ItemMetadata] = []
        while sqlite3_step(stmt) == SQLITE_ROW {
            if let metadata = metadataFromRow(stmt) {
                items.append(metadata)
            }
        }
        return items
    }

    /// Clear all items (for re-enumeration)
    func clearAll() throws {
        try transaction {
            try executeUpdate("DELETE FROM identities.recycle_keys")
            try executeUpdate("DELETE FROM identities.finder_metadata")
            try executeUpdate("DELETE FROM identities.item_identities")
            try executeUpdate("DELETE FROM items")
        }
    }

    /// A persisted epoch rejects anchors from a replaced database or the old timestamp format.
    func currentSyncAnchor() throws -> Data {
        let state = try syncState()
        return Data("\(state.epoch):\(state.sequence)".utf8)
    }

    private func syncState() throws -> (epoch: String, sequence: Int64, minimum: Int64) {
        var stmt: OpaquePointer?
        let sql = "SELECT epoch, COALESCE((SELECT MAX(sequence) FROM item_changes), 0), (SELECT minimum_sequence FROM sync_retention) FROM sync_state"
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        guard sqlite3_step(stmt) == SQLITE_ROW else {
            throw DatabaseError.readFailed("Missing sync state")
        }
        return (columnString(stmt, 0), sqlite3_column_int64(stmt, 1), sqlite3_column_int64(stmt, 2))
    }

    func validateSyncAnchor(_ anchor: Data) throws {
        let state = try syncState()
        let parts = String(data: anchor, encoding: .utf8)?.split(separator: ":") ?? []
        guard parts.count == 2, parts[0] == state.epoch,
              let sequence = Int64(parts[1]), sequence >= state.minimum, sequence <= state.sequence else {
            throw DatabaseError.expiredAnchor
        }
    }

    /// Read the delta and its upper anchor in one actor turn, including durable tombstones.
    /// nil parent denotes the working set, which includes all known items.
    func changes(since anchor: Data, parentOcId: String?, limit: Int = 500) throws -> (updated: [ItemMetadata], deleted: [String], anchor: Data, moreComing: Bool) {
        // Another extension instance may compact the same journal. Keep validation
        // and the bounded page in one SQLite transaction, not just one actor turn.
        try transaction { try readChanges(since: anchor, parentOcId: parentOcId, limit: limit) }
    }

    private func readChanges(since anchor: Data, parentOcId: String?, limit: Int) throws -> (updated: [ItemMetadata], deleted: [String], anchor: Data, moreComing: Bool) {
        let state = try syncState()
        let parts = String(data: anchor, encoding: .utf8)?.split(separator: ":") ?? []
        guard parts.count == 2, parts[0] == state.epoch,
              let sequence = Int64(parts[1]), sequence >= state.minimum, sequence <= state.sequence else {
            throw DatabaseError.expiredAnchor
        }
        var stmt: OpaquePointer?
        let sql = "SELECT oc_id, MAX(sequence) AS last_sequence FROM item_changes WHERE sequence > ? AND (? IS NULL OR parent_oc_id = ?) AND sequence <= ? GROUP BY oc_id ORDER BY last_sequence LIMIT ?"
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        sqlite3_bind_int64(stmt, 1, sequence)
        if let parentOcId = parentOcId {
            sqlite3_bind_text(stmt, 2, parentOcId, -1, SQLITE_TRANSIENT)
            sqlite3_bind_text(stmt, 3, parentOcId, -1, SQLITE_TRANSIENT)
        } else {
            sqlite3_bind_null(stmt, 2)
            sqlite3_bind_null(stmt, 3)
        }
        sqlite3_bind_int64(stmt, 4, state.sequence)
        sqlite3_bind_int64(stmt, 5, Int64(max(1, limit) + 1))
        var updated: [ItemMetadata] = []
        var deleted: [String] = []
        var lastSequence = sequence
        var count = 0
        var moreComing = false
        var result = sqlite3_step(stmt)
        while result == SQLITE_ROW {
            if count == max(1, limit) { moreComing = true; break }
            count += 1
            lastSequence = sqlite3_column_int64(stmt, 1)
            let identifier = columnString(stmt, 0)
            if let metadata = itemMetadata(ocId: identifier),
               parentOcId == nil || metadata.parentOcId == parentOcId {
                updated.append(metadata)
            } else {
                deleted.append(identifier)
            }
            result = sqlite3_step(stmt)
        }
        guard result == SQLITE_DONE || moreComing else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        let upper = moreComing ? lastSequence : state.sequence
        return (updated, deleted, Data("\(state.epoch):\(upper)".utf8), moreComing)
    }

    func allItems() throws -> [ItemMetadata] {
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, "SELECT * FROM items ORDER BY remote_path", -1, &stmt, nil) == SQLITE_OK else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        defer { sqlite3_finalize(stmt) }
        var items: [ItemMetadata] = []
        var result = sqlite3_step(stmt)
        while result == SQLITE_ROW {
            if let metadata = metadataFromRow(stmt) { items.append(metadata) }
            result = sqlite3_step(stmt)
        }
        guard result == SQLITE_DONE else {
            throw DatabaseError.readFailed(String(cString: sqlite3_errmsg(db)))
        }
        return items
    }

    // MARK: - Helpers

    /// Safe helper: returns empty string when sqlite3_column_text returns NULL
    private func columnString(_ stmt: OpaquePointer?, _ col: Int32) -> String {
        guard let ptr = sqlite3_column_text(stmt, col) else { return "" }
        return String(cString: ptr)
    }

    private func metadataFromRow(_ stmt: OpaquePointer?) -> ItemMetadata? {
        guard let stmt = stmt else { return nil }

        var col: Int32 = 0

        let ocId = columnString(stmt, col); col += 1
        let fileId = columnString(stmt, col); col += 1
        let parentOcId = columnString(stmt, col); col += 1
        let remotePath = columnString(stmt, col); col += 1
        let filename = columnString(stmt, col); col += 1
        let etag = columnString(stmt, col); col += 1
        let contentType = columnString(stmt, col); col += 1
        let size = sqlite3_column_int64(stmt, col); col += 1

        let lastModified: Date?
        if sqlite3_column_type(stmt, col) != SQLITE_NULL {
            lastModified = Date(timeIntervalSince1970: sqlite3_column_double(stmt, col))
        } else {
            lastModified = nil
        }
        col += 1

        let creationDate: Date?
        if sqlite3_column_type(stmt, col) != SQLITE_NULL {
            creationDate = Date(timeIntervalSince1970: sqlite3_column_double(stmt, col))
        } else {
            creationDate = nil
        }
        col += 1

        let isDirectory = sqlite3_column_int(stmt, col) != 0; col += 1
        let permissions = columnString(stmt, col); col += 1
        let ownerId = columnString(stmt, col); col += 1
        let ownerDisplayName = columnString(stmt, col); col += 1
        let isDownloaded = sqlite3_column_int(stmt, col) != 0; col += 1
        let isDownloading = sqlite3_column_int(stmt, col) != 0; col += 1
        let isUploaded = sqlite3_column_int(stmt, col) != 0; col += 1
        let isUploading = sqlite3_column_int(stmt, col) != 0; col += 1
        let status = ItemStatus(rawValue: Int(sqlite3_column_int(stmt, col))) ?? .normal; col += 1

        let statusError: String?
        if sqlite3_column_type(stmt, col) != SQLITE_NULL {
            statusError = String(cString: sqlite3_column_text(stmt, col))
        } else {
            statusError = nil
        }
        col += 1

        let syncTime = Date(timeIntervalSince1970: sqlite3_column_double(stmt, col))

        return ItemMetadata(
            ocId: ocId,
            fileId: fileId,
            parentOcId: parentOcId,
            remotePath: remotePath,
            filename: filename,
            etag: etag,
            contentType: contentType,
            size: size,
            lastModified: lastModified,
            creationDate: creationDate,
            isDirectory: isDirectory,
            permissions: permissions,
            ownerId: ownerId,
            ownerDisplayName: ownerDisplayName,
            isDownloaded: isDownloaded,
            isDownloading: isDownloading,
            isUploaded: isUploaded,
            isUploading: isUploading,
            status: status,
            statusError: statusError,
            syncTime: syncTime,
            finderMetadata: (try? localMetadata(ocId: ocId)) ?? FinderMetadata()
        )
    }
}

// MARK: - Errors

enum DatabaseError: Error, LocalizedError {
    case readFailed(String)
    case expiredAnchor
    case openFailed(String)
    case createTableFailed(String)
    case insertFailed(String)
    case updateFailed(String)
    case deleteFailed(String)

    var errorDescription: String? {
        switch self {
        case .readFailed(let msg): return "Failed to read: \(msg)"
        case .expiredAnchor: return "The sync anchor has expired"
        case .openFailed(let msg): return "Failed to open database: \(msg)"
        case .createTableFailed(let msg): return "Failed to create table: \(msg)"
        case .insertFailed(let msg): return "Failed to insert: \(msg)"
        case .updateFailed(let msg): return "Failed to update: \(msg)"
        case .deleteFailed(let msg): return "Failed to delete: \(msg)"
        }
    }
}

// SQLite constant for binding
private let SQLITE_TRANSIENT = unsafeBitCast(-1, to: sqlite3_destructor_type.self)
