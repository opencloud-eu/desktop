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
import OSLog

/// Enumerates server directories and replays persistent changes from the requested anchor.
class FileProviderEnumerator: NSObject, NSFileProviderEnumerator {
    private let enumeratedItemIdentifier: NSFileProviderItemIdentifier
    private weak var fpExtension: FileProviderExtension?
    private let operationLock = NSLock()
    private var operations: [UUID: Task<Void, Never>] = [:]
    private var invalidated = false
    private static let pageSize = 500
    private struct Page: Codable {
        let container: String
        let epoch: String
        let after: String
    }
    private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "eu.opencloud.desktop.FileProviderExt", category: "FileProviderEnumerator")

    init(enumeratedItemIdentifier: NSFileProviderItemIdentifier, domain: NSFileProviderDomain, fpExtension: FileProviderExtension) {
        self.enumeratedItemIdentifier = enumeratedItemIdentifier
        self.fpExtension = fpExtension
        super.init()
    }

    func invalidate() {
        operationLock.lock()
        invalidated = true
        let tasks = Array(operations.values)
        operationLock.unlock()
        for task in tasks { task.cancel() }
    }

    private func startOperation(_ operation: @escaping () async -> Void) {
        let identifier = UUID()
        operationLock.lock()
        let task = Task {
            await operation()
            finishOperation(identifier)
        }
        operations[identifier] = task
        if invalidated { task.cancel() }
        operationLock.unlock()
    }

    private func finishOperation(_ identifier: UUID) {
        operationLock.lock()
        operations.removeValue(forKey: identifier)
        operationLock.unlock()
    }

    private func providerError(_ error: Error) -> Error {
        if case DatabaseError.expiredAnchor = error {
            return NSFileProviderError(.syncAnchorExpired)
        }
        return fileProviderError(error)
    }

    private func item(_ metadata: ItemMetadata) -> NSFileProviderItem {
        let parent = metadata.parentOcId == ItemDatabase.rootContainerId
            ? NSFileProviderItemIdentifier.rootContainer
            : NSFileProviderItemIdentifier(metadata.parentOcId)
        return FileProviderItem(metadata: metadata, parentItemIdentifier: parent, supportsTrash: fpExtension?.supportsTrash ?? false)
    }

    private func directory(ext: FileProviderExtension, webdav: WebDAVClient, database: ItemDatabase) async throws -> (path: String, parent: String) {
        if enumeratedItemIdentifier == .rootContainer {
            return ("/", ItemDatabase.rootContainerId)
        }
        var metadata = await database.itemMetadata(ocId: enumeratedItemIdentifier.rawValue)
        if metadata == nil {
            metadata = try await ext.resolveItemFromServer(identifier: enumeratedItemIdentifier, webdav: webdav, database: database)
        }
        guard let metadata = metadata, metadata.isDirectory else {
            throw NSFileProviderError(.noSuchItem)
        }
        return (metadata.remotePath, metadata.ocId)
    }

    private func loadDirectory(path: String, parent: String, webdav: WebDAVClient, database: ItemDatabase) async throws -> (items: [ItemMetadata], missing: [ItemMetadata]) {
        // Capture before the network request so concurrent local writes are not deleted.
        try Task.checkCancellation()
        let previous = await database.childItems(parentOcId: parent)
        let started = Date()
        let response = try await webdav.listDirectory(path: path)
        var metadata: [ItemMetadata] = []
        for remote in response.dropFirst() {
            try Task.checkCancellation()
            let merged = try await database.mergeServerMetadata(ItemMetadata(from: remote, parentOcId: parent), fetchedAfter: started)
            metadata.append(merged)
        }
        let identifiers = Set(metadata.map { $0.ocId })
        return (metadata, previous.filter { !identifiers.contains($0.ocId) })
    }

    private func refreshDirectory(path: String, parent: String, webdav: WebDAVClient, database: ItemDatabase) async throws -> [ItemMetadata] {
        if let metadata = await database.itemMetadata(ocId: parent), metadata.isTrashed {
            return try await FileProviderTrash.refresh(webdav: webdav, database: database, parent: metadata)
        }
        let snapshot = try await loadDirectory(path: path, parent: parent, webdav: webdav, database: database)
        if !snapshot.missing.isEmpty {
            // A child missing from one parent may have moved to another. Reconcile
            // known folders before discarding its identity and materialization state.
            try await refreshWorkingSet(webdav: webdav, database: database)
        }
        for missing in snapshot.missing {
            try Task.checkCancellation()
            let retirement = await fpExtension?.syncStatus.removalScope(metadata: missing, database: database)
            try await database.deleteIfUnchanged(missing)
            if let retirement { await fpExtension?.syncStatus.retireRemovedItems(retirement, database: database) }
        }
        return await database.childItems(parentOcId: parent)
    }

    private func refreshWorkingSet(webdav: WebDAVClient, database: ItemDatabase) async throws {
        // Refresh known folders, including parents of materialized nested files.
        // Opening another Finder window must not be required to discover their changes.
        if await webdav.supportsTrash {
            do { _ = try await FileProviderTrash.refresh(webdav: webdav, database: database) }
            catch WebDAVError.fileNotFound { /* The server may not offer an optional trash endpoint. */ }
            catch let error as CocoaError where error.code == .featureUnsupported {}
        }
        let root = try await loadDirectory(path: "/", parent: ItemDatabase.rootContainerId, webdav: webdav, database: database)
        var missing = root.missing
        var cursor: String?
        repeat {
            let page = try await database.itemsPage(parentOcId: nil, after: cursor, limit: Self.pageSize, directoriesOnly: true)
            for directory in page.items {
                try Task.checkCancellation()
                guard let current = await database.itemMetadata(ocId: directory.ocId), !current.isTrashed else { continue }
                do {
                    let snapshot = try await loadDirectory(path: current.remotePath, parent: current.ocId, webdav: webdav, database: database)
                    missing.append(contentsOf: snapshot.missing)
                } catch WebDAVError.fileNotFound {
                    missing.append(current)
                }
            }
            cursor = page.next
        } while cursor != nil
        // Apply all moves before deletions, so moving a cached folder between known
        // parents keeps its descendants and their local state.
        for item in missing {
            try Task.checkCancellation()
            let retirement = await fpExtension?.syncStatus.removalScope(metadata: item, database: database)
            try await database.deleteIfUnchanged(item)
            if let retirement { await fpExtension?.syncStatus.retireRemovedItems(retirement, database: database) }
        }
    }

    func enumerateItems(for observer: NSFileProviderEnumerationObserver, startingAt page: NSFileProviderPage) {
        guard let ext = fpExtension else {
            observer.finishEnumeratingWithError(NSFileProviderError(.notAuthenticated))
            return
        }
        startOperation {
            let activity = ext.syncStatus.begin(.metadata, key: "enumerate:" + self.enumeratedItemIdentifier.rawValue)
            do {
                try Task.checkCancellation()
                guard ext.isAuthenticated, let webdav = ext.webdavClient, let database = ext.database else {
                    throw NSFileProviderError(.notAuthenticated)
                }
                let anchor = try await database.currentSyncAnchor()
                let epoch = String(data: anchor, encoding: .utf8)!.components(separatedBy: ":")[0]
                let initial = page.rawValue == NSFileProviderPage.initialPageSortedByName as Data || page.rawValue == NSFileProviderPage.initialPageSortedByDate as Data
                let cursor: String?
                if initial {
                    cursor = nil
                } else {
                    guard let token = try? JSONDecoder().decode(Page.self, from: page.rawValue),
                          token.container == self.enumeratedItemIdentifier.rawValue, token.epoch == epoch else {
                        throw NSFileProviderError(.pageExpired)
                    }
                    cursor = token.after
                }
                let parent: String?
                switch self.enumeratedItemIdentifier {
                case .trashContainer:
                    if initial, await webdav.supportsTrash { _ = try await FileProviderTrash.refresh(webdav: webdav, database: database) }
                    parent = NSFileProviderItemIdentifier.trashContainer.rawValue
                case .workingSet:
                    if initial { try await self.refreshWorkingSet(webdav: webdav, database: database) }
                    parent = nil
                default:
                    let folder = try await self.directory(ext: ext, webdav: webdav, database: database)
                    if initial { _ = try await self.refreshDirectory(path: folder.path, parent: folder.parent, webdav: webdav, database: database) }
                    parent = folder.parent
                }
                let result = try await database.itemsPage(parentOcId: parent, after: cursor, limit: Self.pageSize)
                try Task.checkCancellation()
                let next = try result.next.map {
                    NSFileProviderPage(try JSONEncoder().encode(Page(container: self.enumeratedItemIdentifier.rawValue, epoch: epoch, after: $0)))
                }
                observer.didEnumerate(result.items.map(self.item))
                if initial && self.enumeratedItemIdentifier != .trashContainer { ext.remoteCheckSucceeded() }
                ext.syncStatus.finish(activity, error: nil, checkedRemote: initial && self.enumeratedItemIdentifier == .workingSet)
                observer.finishEnumerating(upTo: next)
            } catch {
                ext.syncStatus.finish(activity, error: self.providerError(error))
                let container = self.enumeratedItemIdentifier == .rootContainer ? "root" : self.enumeratedItemIdentifier == .workingSet ? "working-set" : self.enumeratedItemIdentifier == .trashContainer ? "trash" : "directory"
                self.logger.error("Enumeration failed [\(container, privacy: .public)]: \(fileProviderErrorDiagnostic(error), privacy: .public)")
                observer.finishEnumeratingWithError(self.providerError(error))
            }
        }
    }

    func enumerateChanges(for observer: NSFileProviderChangeObserver, from anchor: NSFileProviderSyncAnchor) {
        guard let ext = fpExtension else {
            observer.finishEnumeratingWithError(NSFileProviderError(.notAuthenticated))
            return
        }
        startOperation {
            let activity = ext.syncStatus.begin(.metadata, key: "enumerate:" + self.enumeratedItemIdentifier.rawValue)
            do {
                try Task.checkCancellation()
                guard ext.isAuthenticated, let webdav = ext.webdavClient, let database = ext.database else {
                    throw NSFileProviderError(.notAuthenticated)
                }
                try await database.validateSyncAnchor(anchor.rawValue)
                let parent: String?
                switch self.enumeratedItemIdentifier {
                case .trashContainer:
                    if await webdav.supportsTrash { _ = try await FileProviderTrash.refresh(webdav: webdav, database: database) }
                    parent = NSFileProviderItemIdentifier.trashContainer.rawValue
                case .workingSet:
                    try await self.refreshWorkingSet(webdav: webdav, database: database)
                    parent = nil
                default:
                    let folder = try await self.directory(ext: ext, webdav: webdav, database: database)
                    _ = try await self.refreshDirectory(path: folder.path, parent: folder.parent, webdav: webdav, database: database)
                    parent = folder.parent
                }
                let changes = try await database.changes(since: anchor.rawValue, parentOcId: parent)
                try Task.checkCancellation()
                observer.didUpdate(changes.updated.map(self.item))
                observer.didDeleteItems(withIdentifiers: changes.deleted.map { NSFileProviderItemIdentifier($0) })
                ext.remoteCheckSucceeded()
                ext.syncStatus.finish(activity, error: nil, checkedRemote: self.enumeratedItemIdentifier == .workingSet)
                observer.finishEnumeratingChanges(upTo: NSFileProviderSyncAnchor(changes.anchor), moreComing: changes.moreComing)
            } catch {
                ext.syncStatus.finish(activity, error: self.providerError(error))
                self.logger.error("Change enumeration failed: \(fileProviderErrorDiagnostic(error), privacy: .public)")
                observer.finishEnumeratingWithError(self.providerError(error))
            }
        }
    }

    func currentSyncAnchor(completionHandler: @escaping (NSFileProviderSyncAnchor?) -> Void) {
        guard let database = fpExtension?.database else {
            completionHandler(nil)
            return
        }
        startOperation {
            do {
                try Task.checkCancellation()
                completionHandler(NSFileProviderSyncAnchor(try await database.currentSyncAnchor()))
            } catch {
                completionHandler(nil)
            }
        }
    }
}
