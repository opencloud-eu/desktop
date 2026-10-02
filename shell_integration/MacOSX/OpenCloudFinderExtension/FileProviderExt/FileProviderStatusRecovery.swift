import Foundation

extension FileProviderSyncStatus {
    /// Capture only items with tracked activity/errors, not an entire large tree.
    func removalScope(metadata: ItemMetadata, database: ItemDatabase) async -> [String: String] {
        var scope = [metadata.ocId: "create:" + metadata.parentOcId + "/" + metadata.filename]
        guard metadata.isDirectory else { return scope }
        let prefix = metadata.remotePath.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/"
        for identifier in trackedItemIdentifiers() {
            if let child = await database.itemMetadata(ocId: identifier),
               child.remotePath.trimmingCharacters(in: CharacterSet(charactersIn: "/")).hasPrefix(prefix) {
                scope[identifier] = "create:" + child.parentOcId + "/" + child.filename
            }
        }
        return scope
    }

    /// Recheck after the DB transaction: a child concurrently moved elsewhere
    /// still exists, so its failure must not be hidden by a parent's deletion.
    func retireRemovedItems(_ scope: [String: String], database: ItemDatabase, includingTrashed: Bool = false, preserving operation: Operation? = nil) async {
        var removed: Set<String> = []
        for identifier in scope.keys {
            let item = await database.itemMetadata(ocId: identifier)
            if item == nil || (includingTrashed && item?.isTrashed == true) { removed.insert(identifier) }
        }
        guard !removed.isEmpty else { return }
        retireItems(identifiers: removed, creationKeys: Set(removed.compactMap { scope[$0] }), preserving: operation)
    }
}
