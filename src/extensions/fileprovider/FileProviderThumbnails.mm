// FileProviderThumbnails -- Thumbnail fetching and caching for the File Provider extension.
// Two-tier cache: NSCache (in-memory) + disk cache in the app group container.

#import "FileProviderThumbnails.h"
#import "FileProviderXPCService.h"

#import <os/log.h>

static os_log_t thumbnailLog(void) {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("eu.opencloud.desktop.fileprovider", "thumbnails");
    });
    return log;
}

/// Cache TTL: 24 hours in seconds.
static const NSTimeInterval kThumbnailCacheTTL = 24.0 * 60.0 * 60.0;

/// Maximum number of thumbnails kept in the in-memory cache.
static const NSUInteger kMemoryCacheCountLimit = 200;

#pragma mark - FileProviderThumbnails

API_AVAILABLE(macos(12.0))
@implementation FileProviderThumbnails {
    FileProviderXPCService *_xpcService;

    /// In-memory cache keyed by "fileId-WxH".
    NSCache<NSString *, NSData *> *_memoryCache;

    /// Serial queue protecting disk cache reads/writes.
    dispatch_queue_t _cacheQueue;

    /// Root directory for the on-disk thumbnail cache inside the app group container.
    NSURL *_diskCacheURL;
}

- (instancetype)initWithXPCService:(FileProviderXPCService *)xpcService {
    self = [super init];
    if (self) {
        _xpcService = xpcService;

        _memoryCache = [[NSCache alloc] init];
        _memoryCache.countLimit = kMemoryCacheCountLimit;

        _cacheQueue = dispatch_queue_create("eu.opencloud.desktop.fileprovider.thumbnailcache",
                                            DISPATCH_QUEUE_SERIAL);

        // Use the app group container for shared disk cache.
        NSURL *groupContainer = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:@"group.eu.opencloud.desktop"];
        if (groupContainer) {
            _diskCacheURL = [groupContainer URLByAppendingPathComponent:@"ThumbnailCache"
                                                           isDirectory:YES];
        } else {
            // Fallback to temporary directory if app group is unavailable.
            os_log_error(thumbnailLog(), "App group container unavailable, using temp dir for thumbnail cache");
            _diskCacheURL = [NSURL fileURLWithPath:[NSTemporaryDirectory()
                stringByAppendingPathComponent:@"OpenCloudThumbnailCache"]
                                     isDirectory:YES];
        }

        // Ensure the cache directory exists.
        [[NSFileManager defaultManager] createDirectoryAtURL:_diskCacheURL
                                 withIntermediateDirectories:YES
                                                  attributes:nil
                                                       error:nil];
    }
    return self;
}

#pragma mark - Public

- (void)fetchThumbnail:(NSString *)fileId
                  size:(CGSize)size
     completionHandler:(void (^)(NSData * _Nullable, NSError * _Nullable))handler {

    NSString *cacheKey = [self _cacheKeyForFileId:fileId size:size];

    // 1. Check in-memory cache.
    NSData *memoryCached = [_memoryCache objectForKey:cacheKey];
    if (memoryCached) {
        os_log_debug(thumbnailLog(), "Thumbnail cache hit (memory) for %{public}@", fileId);
        handler(memoryCached, nil);
        return;
    }

    // 2. Check disk cache (off main thread).
    dispatch_async(_cacheQueue, ^{
        NSData *diskCached = [self _readDiskCacheForKey:cacheKey];
        if (diskCached) {
            os_log_debug(thumbnailLog(), "Thumbnail cache hit (disk) for %{public}@", fileId);
            [self->_memoryCache setObject:diskCached forKey:cacheKey];
            handler(diskCached, nil);
            return;
        }

        // 3. Fetch via XPC from the main app.
        os_log_info(thumbnailLog(), "Fetching thumbnail via XPC for %{public}@ size=%.0fx%.0f",
                    fileId, size.width, size.height);

        id<OpenCloudXPCServiceProtocol> proxy = self->_xpcService.remoteObjectProxy;
        if (!proxy) {
            os_log_error(thumbnailLog(), "No XPC proxy for thumbnail fetch of %{public}@", fileId);
            handler(nil, nil);
            return;
        }

        [proxy fetchThumbnail:fileId size:size completionHandler:^(NSData *imageData, NSError *error) {
            if (error) {
                os_log_error(thumbnailLog(), "XPC thumbnail error for %{public}@: %{public}@",
                             fileId, error.localizedDescription);
                handler(nil, error);
                return;
            }

            if (!imageData || imageData.length == 0) {
                // No thumbnail available -- graceful degradation.
                os_log_debug(thumbnailLog(), "No thumbnail available for %{public}@", fileId);
                handler(nil, nil);
                return;
            }

            // Cache the result.
            [self->_memoryCache setObject:imageData forKey:cacheKey];
            dispatch_async(self->_cacheQueue, ^{
                [self _writeDiskCache:imageData forKey:cacheKey];
            });

            os_log_info(thumbnailLog(), "Thumbnail fetched and cached for %{public}@ (%lu bytes)",
                        fileId, (unsigned long)imageData.length);
            handler(imageData, nil);
        }];
    });
}

- (void)clearCache {
    [_memoryCache removeAllObjects];

    dispatch_async(_cacheQueue, ^{
        NSError *error = nil;
        [[NSFileManager defaultManager] removeItemAtURL:self->_diskCacheURL error:&error];
        if (error) {
            os_log_error(thumbnailLog(), "Failed to clear disk cache: %{public}@",
                         error.localizedDescription);
        }
        [[NSFileManager defaultManager] createDirectoryAtURL:self->_diskCacheURL
                                 withIntermediateDirectories:YES
                                                  attributes:nil
                                                       error:nil];
        os_log_info(thumbnailLog(), "Thumbnail cache cleared");
    });
}

#pragma mark - Private: Cache Key

- (NSString *)_cacheKeyForFileId:(NSString *)fileId size:(CGSize)size {
    return [NSString stringWithFormat:@"%@-%.0fx%.0f", fileId, size.width, size.height];
}

#pragma mark - Private: Disk Cache

/// Returns the file URL for a given cache key inside the disk cache directory.
- (NSURL *)_diskCacheFileURLForKey:(NSString *)key {
    // Use a simple hash to avoid filesystem-unfriendly characters.
    NSString *safeKey = [[key dataUsingEncoding:NSUTF8StringEncoding] base64EncodedStringWithOptions:0];
    return [_diskCacheURL URLByAppendingPathComponent:safeKey];
}

/// Reads data from disk cache if it exists and has not expired (24h TTL).
/// Must be called on _cacheQueue.
- (NSData * _Nullable)_readDiskCacheForKey:(NSString *)key {
    NSURL *fileURL = [self _diskCacheFileURLForKey:key];

    NSError *error = nil;
    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:fileURL.path error:&error];
    if (!attrs) {
        return nil;
    }

    // Check TTL.
    NSDate *modDate = attrs[NSFileModificationDate];
    if (modDate && [[NSDate date] timeIntervalSinceDate:modDate] > kThumbnailCacheTTL) {
        // Expired -- remove stale entry.
        [[NSFileManager defaultManager] removeItemAtURL:fileURL error:nil];
        return nil;
    }

    return [NSData dataWithContentsOfURL:fileURL options:0 error:&error];
}

/// Writes data to disk cache. Must be called on _cacheQueue.
- (void)_writeDiskCache:(NSData *)data forKey:(NSString *)key {
    NSURL *fileURL = [self _diskCacheFileURLForKey:key];
    NSError *error = nil;
    if (![data writeToURL:fileURL options:NSDataWritingAtomic error:&error]) {
        os_log_error(thumbnailLog(), "Failed to write thumbnail to disk cache: %{public}@",
                     error.localizedDescription);
    }
}

@end
