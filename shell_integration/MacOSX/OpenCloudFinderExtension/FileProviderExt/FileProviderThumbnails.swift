import FileProvider

extension FileProviderExtension: NSFileProviderThumbnailing {
    func fetchThumbnails(for itemIdentifiers: [NSFileProviderItemIdentifier], requestedSize size: CGSize,
                         perThumbnailCompletionHandler: @escaping (NSFileProviderItemIdentifier, Data?, Error?) -> Void,
                         completionHandler: @escaping (Error?) -> Void) -> Progress {
        let progress = Progress(totalUnitCount: Int64(itemIdentifiers.count))
        let task = Task {
            guard let client = webdavClient, let database else {
                completionHandler(NSFileProviderError(.notAuthenticated))
                return
            }
            // Bounds also reject non-finite values before conversion to Int.
            let width = size.width.isFinite ? Int(min(max(size.width, 1), 1024)) : 256
            let height = size.height.isFinite ? Int(min(max(size.height, 1), 1024)) : 256
            do {
                for identifier in itemIdentifiers {
                    try Task.checkCancellation()
                    do {
                        guard let metadata = try await database.resolveItem(identifier: identifier.rawValue, webdav: client) else {
                            throw NSFileProviderError(.noSuchItem)
                        }
                        let data = metadata.isDirectory ? nil : try await client.thumbnail(remotePath: metadata.remotePath,
                                                                                          etag: metadata.etag, width: width, height: height)
                        try Task.checkCancellation()
                        perThumbnailCompletionHandler(identifier, data, nil)
                    } catch {
                        try Task.checkCancellation()
                        perThumbnailCompletionHandler(identifier, nil, fileProviderError(error))
                    }
                    progress.completedUnitCount += 1
                }
                completionHandler(nil)
            } catch {
                completionHandler(fileProviderError(error))
            }
        }
        progress.cancellationHandler = { task.cancel() }
        return progress
    }
}
