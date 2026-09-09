// FileProviderThumbnails -- Helper class for fetching and caching thumbnails
// served to the macOS File Provider framework via NSFileProviderThumbnailing.
#pragma once

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

@class FileProviderXPCService;

NS_ASSUME_NONNULL_BEGIN

/// Fetches and caches file thumbnails for the File Provider extension.
/// Uses XPC to request thumbnail data from the main app, and maintains
/// a two-tier cache (NSCache in-memory + disk in the app group container)
/// with a 24-hour TTL.
API_AVAILABLE(macos(12.0))
@interface FileProviderThumbnails : NSObject

/// Designated initializer.
/// @param xpcService  The XPC service used to request thumbnails from the main app.
- (instancetype)initWithXPCService:(FileProviderXPCService *)xpcService;

/// Fetch a thumbnail for a given file identifier.
/// @param fileId   The server-side file identifier.
/// @param size     The requested thumbnail dimensions.
/// @param handler  Called with thumbnail image data (PNG) or nil if unavailable.
///                 Error is non-nil only on infrastructure failures, not missing thumbnails.
- (void)fetchThumbnail:(NSString *)fileId size:(CGSize)size completionHandler:(void (^)(NSData *_Nullable imageData, NSError *_Nullable error))handler;

/// Remove all cached thumbnails (both in-memory and on-disk).
- (void)clearCache;

@end

NS_ASSUME_NONNULL_END
