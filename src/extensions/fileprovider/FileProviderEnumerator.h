// FileProviderEnumerator -- NSFileProviderEnumerator implementation that serves
// directory listings to Finder by querying the server live (PROPFIND Depth:1)
// and caching the result. The extension is the source of truth.
#pragma once

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>

@class FileProviderItemCache;

NS_ASSUME_NONNULL_BEGIN

/// Enumerates items within a given container (directory) for the File Provider
/// framework. Resolves the container's server path from the shared cache, does a
/// live PROPFIND for its children, and updates the cache so deeper containers and
/// itemForIdentifier can resolve.
API_AVAILABLE(macos(12.0))
@interface FileProviderEnumerator : NSObject <NSFileProviderEnumerator>

/// Designated initializer.
/// @param containerId The container to enumerate (root, working set, or a folder's fileId).
/// @param domain      The File Provider domain (its identifier selects the config plist).
/// @param cache       Shared per-domain metadata cache (id->path, container snapshots).
- (instancetype)initWithContainerIdentifier:(NSFileProviderItemIdentifier)containerId
                                     domain:(NSFileProviderDomain *)domain
                                      cache:(FileProviderItemCache *)cache;

@end

NS_ASSUME_NONNULL_END
