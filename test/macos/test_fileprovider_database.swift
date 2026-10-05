import Foundation
import SQLite3

@main
struct DatabaseTests {
    static func metadata(_ id: String, parent: String = ItemDatabase.rootContainerId,
                         path: String? = nil, etag: String = "v1", directory: Bool = false,
                         size: Int64 = 100) -> ItemMetadata {
        ItemMetadata(from: WebDAVItem(ocId: id, fileId: id, remotePath: path ?? "/\(id)",
                                     filename: id, etag: etag, contentType: "", size: size,
                                     lastModified: nil, creationDate: nil, isDirectory: directory,
                                     permissions: "", ownerId: "", ownerDisplayName: ""), parentOcId: parent)
    }

    static func require(_ condition: Bool, _ message: String) {
        precondition(condition, message)
    }

    static func main() async throws {
        let container = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: container) }
        let database = try ItemDatabase(containerURL: container, domainIdentifier: "test")
        let initial = try await database.currentSyncAnchor()
        try await database.addItemMetadata(metadata("file"))
        let first = try await database.changes(since: initial, parentOcId: ItemDatabase.rootContainerId)
        require(first.updated.map(\.ocId) == ["file"], "Insert must be replayed")
        require(first.anchor != initial, "Anchor must advance for insert")
        // Reads and later enumeration cannot consume another observer's changes.
        _ = await database.itemMetadata(ocId: "file")
        _ = try await database.mergeServerMetadata(metadata("file"))
        let replay = try await database.changes(since: initial, parentOcId: ItemDatabase.rootContainerId)
        require(replay.updated.map(\.ocId) == ["file"], "Changes must replay from the supplied anchor")
        require(replay.anchor == first.anchor, "Identical server metadata must not create changes")
        let reopened = try ItemDatabase(containerURL: container, domainIdentifier: "test")
        let persisted = try await reopened.currentSyncAnchor()
        require(persisted == first.anchor, "Anchor must survive extension restart")
        try await database.setDownloaded(ocId: "file", downloaded: true)
        let localChange = try await database.changes(since: first.anchor, parentOcId: nil)
        require(localChange.updated.first?.isDownloaded == true, "Local state changes belong in working-set deltas")
        let merged = try await database.mergeServerMetadata(metadata("file", etag: "v2", size: 0))
        require(merged.isDownloaded && merged.size == 0, "Preserve local state and accept remote truncation to zero")
        try await database.setStatus(ocId: "file", status: .downloading)
        try await database.finishDownload(ocId: "file", matchingETag: "v1", size: 100)
        require(await database.itemMetadata(ocId: "file")?.size == 0, "Older GET completion must not corrupt newer remote size")
        try await database.finishDownload(ocId: "file", matchingETag: "v2", size: 0)
        require(await database.itemMetadata(ocId: "file")?.status == .normal, "Matching GET completion must clear download status")
        try await database.setStatus(ocId: "file", status: .uploading)
        try await database.finishDownload(ocId: "file", matchingETag: "v2", size: 0)
        require(await database.itemMetadata(ocId: "file")?.isUploading == true, "Download completion must preserve concurrent upload state")
        try await database.setStatus(ocId: "file", status: .normal)
        // A stale response cannot overwrite or delete a newer local operation.
        let stale = merged
        let started = Date.distantPast
        _ = try await database.mergeServerMetadata(metadata("file", etag: "stale"), fetchedAfter: started)
        require(await database.itemMetadata(ocId: "file")?.etag == "v2", "Ignore stale server responses")
        let sameTick = await database.itemMetadata(ocId: "file")!.syncTime
        let samePath = try await database.mergeServerMetadata(metadata("file", etag: "same-tick-stale"), fetchedAfter: sameTick)
        require(samePath.etag == "v2", "Equal persisted timestamps must preserve the cached item at its path")
        let staleMove = try await database.mergeServerMetadata(metadata("file", path: "/stale-move", etag: "same-tick-stale"), fetchedAfter: sameTick)
        require(staleMove.etag == "v2" && staleMove.remotePath == samePath.remotePath,
                "Equal persisted timestamps must preserve a cached identifier at a different path")
        try await database.addItemMetadata(metadata("file", parent: "new-parent", etag: "v3"))
        try await database.deleteIfUnchanged(stale)
        require(await database.itemMetadata(ocId: "file") != nil, "Stale directory response must not delete a moved item")
        let oldParent = try await database.changes(since: localChange.anchor, parentOcId: ItemDatabase.rootContainerId)
        require(oldParent.deleted == ["file"], "Moving must remove the item from its old parent")
        let newParent = try await database.changes(since: localChange.anchor, parentOcId: "new-parent")
        require(newParent.updated.map(\.ocId) == ["file"], "Moving must add the item to its new parent")

        // Literal SQL wildcard characters and composed Unicode must not corrupt siblings.
        let oldPath = "/cafe\u{301}%_"
        try await database.addItemMetadata(metadata("folder", path: oldPath, directory: true))
        try await database.addItemMetadata(metadata("child", parent: "folder", path: oldPath + "/child"))
        try await database.addItemMetadata(metadata("sibling", path: oldPath + "other/child"))
        let moveAnchor = try await database.currentSyncAnchor()
        try await database.addItemMetadata(metadata("folder", path: "/renamed", directory: true))
        require(await database.itemMetadata(ocId: "child")?.remotePath == "/renamed/child", "Directory move must update descendant paths")
        require(await database.itemMetadata(ocId: "sibling")?.remotePath == oldPath + "other/child", "Move must not rewrite siblings")
        let moved = try await database.changes(since: moveAnchor, parentOcId: nil)
        require(Set(moved.updated.map(\.ocId)) == ["folder", "child"], "Directory move must report descendant metadata changes")
        try await database.deleteDirectoryAndSubdirectories(ocId: "folder")
        let deletion = try await database.changes(since: moved.anchor, parentOcId: nil)
        require(Set(deletion.deleted) == ["folder", "child"], "Recursive delete must journal descendant tombstones")
        let again = try await database.changes(since: moved.anchor, parentOcId: nil)
        require(Set(again.deleted) == Set(deletion.deleted), "Deleted items must remain replayable")
        do {
            _ = try await database.changes(since: Data("2026-01-01T00:00:00Z".utf8), parentOcId: nil)
            preconditionFailure("Old timestamp anchors must expire")
        } catch DatabaseError.expiredAnchor {}
        let other = try ItemDatabase(containerURL: container, domainIdentifier: "other")
        do {
            _ = try await other.changes(since: deletion.anchor, parentOcId: nil)
            preconditionFailure("Anchors from another database must expire")
        } catch DatabaseError.expiredAnchor {}
        // Migrate an existing items-only database without losing its cached metadata.
        let legacy = try ItemDatabase(containerURL: container, domainIdentifier: "legacy")
        try await legacy.addItemMetadata(metadata("preserved"))
        var handle: OpaquePointer?
        let legacyPath = container.appendingPathComponent("FileProvider/items-legacy.sqlite").path
        require(sqlite3_open(legacyPath, &handle) == SQLITE_OK, "Open migration fixture")
        let removeJournal = """
            DROP TRIGGER items_before_replace;
            DROP TRIGGER items_insert;
            DROP TRIGGER items_update;
            DROP TRIGGER items_delete;
            DROP TABLE item_changes;
            DROP TABLE sync_state;
            """
        require(sqlite3_exec(handle, removeJournal, nil, nil, nil) == SQLITE_OK, "Create old-schema fixture")
        sqlite3_close(handle)
        let migrated = try ItemDatabase(containerURL: container, domainIdentifier: "legacy")
        require(await migrated.itemMetadata(ocId: "preserved") != nil, "Migration must preserve cached items")
        let migratedAnchor = try await migrated.currentSyncAnchor()
        try await migrated.deleteItemMetadata(ocId: "preserved")
        let migratedDelete = try await migrated.changes(since: migratedAnchor, parentOcId: nil)
        require(migratedDelete.deleted == ["preserved"], "Migration must install working delete journal")
        let retained = try ItemDatabase(containerURL: container, domainIdentifier: "retention", changeRetentionLimit: 20)
        let prunedAnchor = try await retained.currentSyncAnchor()
        for index in 0..<60 { try await retained.addItemMetadata(metadata("entry-\(index)")) }
        do {
            _ = try await retained.changes(since: prunedAnchor, parentOcId: nil)
            preconditionFailure("Pruned history must expire its old anchor")
        } catch DatabaseError.expiredAnchor {}
        let recentAnchor = try await retained.currentSyncAnchor()
        for index in 60..<70 { try await retained.addItemMetadata(metadata("entry-\(index)")) }
        var changeCursor = recentAnchor
        var changed = Set<String>()
        var more = true
        while more {
            let page = try await retained.changes(since: changeCursor, parentOcId: nil, limit: 3)
            require(page.updated.count + page.deleted.count <= 3, "Change pages must stay bounded")
            changed.formUnion(page.updated.map(\.ocId))
            changeCursor = page.anchor
            more = page.moreComing
        }
        require(changed.count == 10, "Paged change delivery must not lose identifiers")
        require(changeCursor == (try await retained.currentSyncAnchor()), "Final page must reach current anchor")
        var retentionHandle: OpaquePointer?
        require(sqlite3_open(container.appendingPathComponent("FileProvider/items-retention.sqlite").path, &retentionHandle) == SQLITE_OK, "Open retention inspection")
        var statement: OpaquePointer?
        require(sqlite3_prepare_v2(retentionHandle, "SELECT COUNT(*) FROM item_changes", -1, &statement, nil) == SQLITE_OK, "Inspect bounded journal")
        require(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) <= 21, "Journal must stay within retention plus pruning batch")
        sqlite3_finalize(statement)
        sqlite3_close(retentionHandle)

        let large = try ItemDatabase(containerURL: container, domainIdentifier: "large")
        for index in 0..<1203 { try await large.addItemMetadata(metadata("item-\(index)")) }
        var cursor: String?
        var pagedIDs = Set<String>()
        var pages = 0
        repeat {
            let page = try await large.itemsPage(parentOcId: nil, after: cursor, limit: 500)
            require(page.items.count <= 500, "Item pages must stay bounded")
            pagedIDs.formUnion(page.items.map(\.ocId))
            cursor = page.next
            pages += 1
        } while cursor != nil
        require(pages == 3 && pagedIDs.count == 1203, "Large item enumeration must deliver every item once")
        // Finder metadata has its own durable, account-private store.
        let finder = try ItemDatabase(containerURL: container, domainIdentifier: "finder")
        _ = try await finder.mergeServerMetadata(metadata("finder-file"))
        let finderAnchor = try await finder.currentSyncAnchor()
        var local = FinderMetadata()
        local.tagData = Data([0, 255, 7])
        local.lastUsedDate = Date(timeIntervalSince1970: 1234)
        local.creationDate = Date(timeIntervalSince1970: 12)
        local.contentModificationDate = Date(timeIntervalSince1970: 34)
        local.contentPolicy = 3
        local.fileSystemFlags = 17
        local.extendedAttributes = ["com.example.finder": Data([255, 0])]
        let finderFields: Set<FinderMetadata.Field> = [.tagData, .lastUsedDate, .contentPolicy, .fileSystemFlags, .extendedAttributes, .creationDate, .contentModificationDate]
        let updatedFinder = try await finder.updateLocalMetadata(ocId: "finder-file", metadata: local, fields: finderFields)
        require(updatedFinder?.finderMetadata == local, "Finder fields must be returned after persistence")
        let finderChanges = try await finder.changes(since: finderAnchor, parentOcId: nil)
        require(finderChanges.updated.first?.finderMetadata == local, "Finder metadata changes must be journaled")
        _ = try await finder.updateLocalMetadata(ocId: "finder-file", metadata: local, fields: finderFields)
        require(try await finder.currentSyncAnchor() == finderChanges.anchor, "Identical Finder metadata must not advance the anchor")
        let refreshedFinder = try await finder.mergeServerMetadata(metadata("finder-file", path: "/renamed-finder", etag: "v2"))
        local.contentModificationDate = nil
        require(refreshedFinder.finderMetadata == local, "Server refresh and move must preserve private Finder metadata")
        let reopenedFinder = try ItemDatabase(containerURL: container, domainIdentifier: "finder")
        _ = try await reopenedFinder.updateLocalMetadata(ocId: "finder-file", metadata: FinderMetadata(), fields: [.tagData])
        local.tagData = nil
        require(await finder.itemMetadata(ocId: "finder-file")?.finderMetadata == local, "Explicit clears from another instance must preserve independent fields")
        // Simulate loss of the replaceable cache while retaining the durable identity database.
        let finderCache = container.appendingPathComponent("FileProvider/items-finder.sqlite")
        var cacheHandle: OpaquePointer?
        require(sqlite3_open(finderCache.path, &cacheHandle) == SQLITE_OK, "Open cache for loss simulation")
        require(sqlite3_exec(cacheHandle, "DELETE FROM items", nil, nil, nil) == SQLITE_OK, "Discard cache rows")
        sqlite3_close(cacheHandle)
        let recoveredFinder = try await reopenedFinder.mergeServerMetadata(metadata("finder-file", path: "/renamed-finder"))
        require(recoveredFinder.finderMetadata == local, "Cache recovery must preserve private Finder metadata")
        _ = try await reopenedFinder.mergeServerMetadata(metadata("replacement", path: "/renamed-finder"))
        _ = try await reopenedFinder.mergeServerMetadata(metadata("finder-file", path: "/other"))
        require(await reopenedFinder.itemMetadata(ocId: "finder-file")?.finderMetadata == FinderMetadata(), "Replacement must retire old private metadata")
        let isolatedFinder = try ItemDatabase(containerURL: container, domainIdentifier: "finder-other")
        let isolatedItem = try await isolatedFinder.mergeServerMetadata(metadata("finder-file"))
        require(isolatedItem.finderMetadata == FinderMetadata(), "Finder metadata must stay private to its domain")
        print("FileProvider database regression tests passed")
    }
}
