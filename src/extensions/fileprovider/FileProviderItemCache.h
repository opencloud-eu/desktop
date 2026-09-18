// FileProviderItemCache -- persistent metadata cache for the File Provider
// extension's authoritative (server-driven) enumeration.
//
// Stores:
//   * fileId -> space-relative path        (resolve a container id to a path)
//   * containerPath -> { etag, childIds }  (per-folder change detection + offline)
//
// Backed by a plist at an injectable file URL so it can be unit-tested with a
// temp directory (see tests/test_fileprovider_item_cache.mm). Thread-safe.
#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface FileProviderItemCache : NSObject

/// Loads any existing cache from `url`. Mutations are kept in memory until
/// -save is called.
- (instancetype)initWithFileURL:(NSURL *)url;

#pragma mark id <-> path

- (nullable NSString *)pathForFileId:(NSString *)fileId;
- (void)setPath:(NSString *)path forFileId:(NSString *)fileId;

#pragma mark per-item metadata (for itemForIdentifier / offline)

/// Full metadata dict for an item (keys as consumed by FileProviderItem:
/// fileId, filename, path, parentId, isDirectory, size, modtime, etag,
/// isDownloaded, davUrl). Returns nil if unknown.
- (nullable NSDictionary *)metadataForFileId:(NSString *)fileId;
/// Stores item metadata and (for convenience) its fileId->path mapping.
- (void)setMetadata:(NSDictionary *)metadata forFileId:(NSString *)fileId;

#pragma mark container snapshot (change detection / offline)

- (nullable NSString *)etagForContainerPath:(NSString *)containerPath;
- (nullable NSArray<NSString *> *)childFileIdsForContainerPath:(NSString *)containerPath;
- (void)setContainerPath:(NSString *)containerPath etag:(NSString *)etag childFileIds:(NSArray<NSString *> *)childFileIds;

#pragma mark persistence

/// Atomically writes the in-memory state to the file URL. Returns NO on failure.
- (BOOL)save;
/// Discards in-memory state and re-reads from the file URL.
- (void)reload;

@end

NS_ASSUME_NONNULL_END