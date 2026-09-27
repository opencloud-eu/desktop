import AppKit
import FileProvider

extension FileProviderExtension: NSFileProviderCustomAction {
    func performAction(identifier: NSFileProviderExtensionActionIdentifier, onItemsWithIdentifiers identifiers: [NSFileProviderItemIdentifier],
                       completionHandler: @escaping (Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: 1)
        let task = Task {
            do {
                try Task.checkCancellation()
                guard let action = ProviderBrowserAction(rawValue: identifier.rawValue), identifiers.count == 1 else {
                    throw CocoaError(.featureUnsupported)
                }
                guard let webdav = webdavClient, let database else { throw NSFileProviderError(.notAuthenticated) }
                guard let metadata = try await database.resolveItem(identifier: identifiers[0].rawValue, webdav: webdav), !metadata.isTrashed else {
                    throw NSFileProviderError(.noSuchItem)
                }
                if action == .share && !metadata.permissions.uppercased().contains("R") { throw WebDAVError.permissionDenied }
                if action == .versions && metadata.isDirectory { throw CocoaError(.featureUnsupported) }
                let link = try await webdav.privateLink(path: metadata.remotePath)
                let destination = try action.destination(privateLink: link)
                try Task.checkCancellation()
                try await MainActor.run {
                    guard !progress.isCancelled else { throw CocoaError(.userCancelled) }
                    guard self.webdavClient === webdav else { throw NSFileProviderError(.notAuthenticated) }
                    if action == .copyLink {
                        NSPasteboard.general.clearContents()
                        guard NSPasteboard.general.setString(destination.absoluteString, forType: .string) else { throw CocoaError(.fileWriteUnknown) }
                    } else if !NSWorkspace.shared.open(destination) {
                        throw CocoaError(.fileReadUnknown)
                    }
                }
                progress.completedUnitCount = 1
                completionHandler(nil)
            } catch {
                completionHandler(fileProviderError(error))
            }
        }
        progress.cancellationHandler = { task.cancel() }
        if progress.isCancelled { task.cancel() }
        return progress
    }
}
