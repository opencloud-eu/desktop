// FileProviderEnumerator -- NSFileProviderEnumerator implementation.
// Serves directory listings to Finder by reading file metadata from the
// App Group shared container (written by the main app's sync engine).

#import "FileProviderEnumerator.h"

#import "FileProviderItem.h"
#import "FileProviderXPCService.h"

#import <os/log.h>

static os_log_t enumeratorLog(void) {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("eu.opencloud.desktop.fileprovider", "enumerator");
    });
    return log;
}

/// Appends a trace line to the debug log file in the App Group container.
static void appendTrace(NSString *line) {
    NSURL *container = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!container) return;
    NSString *path = [[container URLByAppendingPathComponent:@"fp_debug.log"] path];
    NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!fh) {
        [[NSFileManager defaultManager] createFileAtPath:path contents:nil attributes:nil];
        fh = [NSFileHandle fileHandleForWritingAtPath:path];
    }
    [fh seekToEndOfFile];
    [fh writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
    [fh closeFile];
}

/// Reads the shared metadata plist from the App Group container.
/// Uses per-domain file if available, falls back to legacy global file.
static NSArray<NSDictionary *> *readSharedMetadata(NSFileProviderDomain *domain) {
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) {
        os_log_error(enumeratorLog(), "Cannot access App Group container");
        return nil;
    }

    // Try per-domain file first, fall back to legacy.
    NSString *perDomainName = [NSString stringWithFormat:@"fileprovider_items_%@.plist", domain.identifier];
    NSURL *perDomainURL = [containerURL URLByAppendingPathComponent:perDomainName];
    NSURL *metadataURL = [[NSFileManager defaultManager] fileExistsAtPath:perDomainURL.path]
        ? perDomainURL
        : [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
    NSData *data = [NSData dataWithContentsOfURL:metadataURL];
    if (!data) {
        os_log_error(enumeratorLog(), "Shared metadata file not found at: %{public}@", metadataURL.path);
        return nil;
    }

    NSError *readError = nil;
    NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                              options:NSPropertyListImmutable
                                                               format:nil
                                                                error:&readError];
    if (!items || readError) {
        os_log_error(enumeratorLog(), "Failed to read shared metadata: %{public}@",
                     readError.localizedDescription);
        return nil;
    }

    return items;
}

#pragma mark - FileProviderEnumerator

API_AVAILABLE(macos(12.0))
@implementation FileProviderEnumerator {
    NSFileProviderItemIdentifier _containerId;
    FileProviderXPCService *_xpcService;
    NSFileProviderDomain *_domain;
    BOOL _invalidated;
}

- (instancetype)initWithContainerIdentifier:(NSFileProviderItemIdentifier)containerId
                                 xpcService:(FileProviderXPCService *)service
                                     domain:(NSFileProviderDomain *)domain {
    self = [super init];
    if (self) {
        _containerId = [containerId copy];
        _xpcService = service;
        _domain = domain;
        _invalidated = NO;

        os_log_debug(enumeratorLog(), "Enumerator CREATED for container: %{public}@", containerId);
    }
    return self;
}

#pragma mark - NSFileProviderEnumerator

- (void)enumerateItemsForObserver:(id<NSFileProviderEnumerationObserver>)observer
                   startingAtPage:(NSFileProviderPage)page {
    // Use fault-level logging to ensure it's always persisted
    os_log_debug(enumeratorLog(), "enumerateItems CALLED container=%{public}@ invalidated=%d", _containerId, _invalidated);

    appendTrace([NSString stringWithFormat:@"[%@] enumerateItems container=%@ invalidated=%d\n",
        [NSDate date], _containerId, _invalidated]);

    if (_invalidated) {
        [observer finishEnumeratingWithError:
            [NSError errorWithDomain:NSFileProviderErrorDomain
                                code:NSFileProviderErrorServerUnreachable
                            userInfo:@{NSLocalizedDescriptionKey: @"Enumerator has been invalidated"}]];
        return;
    }

    os_log_info(enumeratorLog(), "enumerateItems container=%{public}@", _containerId);

    // Read file metadata from the App Group shared container.
    NSArray<NSDictionary *> *allItems = readSharedMetadata(_domain);
    if (!allItems) {
        os_log_error(enumeratorLog(), "No shared metadata available — main app may not be running");
        [observer didEnumerateItems:@[]];
        [observer finishEnumeratingWithError:nil];
        return;
    }

    // Determine which items belong to this container.
    NSString *targetParentPath = nil;

    if ([_containerId isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
        // Root container: items whose parentPath is empty.
        targetParentPath = @"";
    } else if ([_containerId isEqualToString:NSFileProviderWorkingSetContainerItemIdentifier]) {
        // Working set: return all items.
        targetParentPath = nil; // nil means "all items"
    } else if ([_containerId isEqualToString:NSFileProviderTrashContainerItemIdentifier]) {
        // Trash: return empty (no trashed items tracked).
        os_log_info(enumeratorLog(), "Enumerated 0 items for trash container");
        [observer didEnumerateItems:@[]];
        [observer finishEnumeratingWithError:nil];
        return;
    } else {
        // Specific folder: find the folder's path by its fileId,
        // then list items whose parentPath matches that path.
        for (NSDictionary *item in allItems) {
            if ([item[@"fileId"] isEqualToString:_containerId]) {
                targetParentPath = item[@"path"];
                break;
            }
        }
        if (!targetParentPath) {
            os_log_error(enumeratorLog(), "Container not found in metadata: %{public}@", _containerId);
            [observer didEnumerateItems:@[]];
            [observer finishEnumeratingWithError:nil];
            return;
        }
    }

    // Filter items by parent path.
    NSMutableArray<FileProviderItem *> *providerItems = [NSMutableArray array];
    for (NSDictionary *dict in allItems) {
        if (targetParentPath == nil) {
            // Working set: include all items.
        } else if (![dict[@"parentPath"] isEqualToString:targetParentPath]) {
            continue;
        }

        FileProviderItem *item = [[FileProviderItem alloc] initWithDictionary:dict];
        [providerItems addObject:item];
    }

    os_log_info(enumeratorLog(), "Enumerated %lu items for container %{public}@",
                (unsigned long)providerItems.count, _containerId);

    appendTrace([NSString stringWithFormat:@"[%@] enumerateItems RESULT container=%@ items=%lu allItems=%lu\n",
        [NSDate date], _containerId, (unsigned long)providerItems.count, (unsigned long)allItems.count]);

    [observer didEnumerateItems:providerItems];
    [observer finishEnumeratingWithError:nil];
}

- (void)enumerateChangesForObserver:(id<NSFileProviderChangeObserver>)observer
                     fromSyncAnchor:(NSFileProviderSyncAnchor)anchor {
    os_log_debug(enumeratorLog(), "enumerateChanges CALLED container=%{public}@", _containerId);

    {
        NSString *inAnchorStr = anchor ? [[NSString alloc] initWithData:anchor encoding:NSUTF8StringEncoding] : @"(nil)";
        appendTrace([NSString stringWithFormat:@"[%@] enumerateChanges container=%@ anchor=%@\n",
            [NSDate date], _containerId, inAnchorStr]);
    }

    os_log_info(enumeratorLog(), "enumerateChanges container=%{public}@", _containerId);

    // Read current metadata to build a content-based sync anchor.
    NSArray<NSDictionary *> *allItems = readSharedMetadata(_domain);
    NSString *currentAnchorString = @"empty";
    if (allItems) {
        // Use item count + latest modtime as a simple content anchor.
        int64_t latestModtime = 0;
        for (NSDictionary *dict in allItems) {
            int64_t mt = [dict[@"modtime"] longLongValue];
            if (mt > latestModtime) latestModtime = mt;
        }
        currentAnchorString = [NSString stringWithFormat:@"%lu-%lld",
                               (unsigned long)allItems.count, latestModtime];
    }
    NSData *currentAnchor = [currentAnchorString dataUsingEncoding:NSUTF8StringEncoding];

    // Compare with incoming anchor. If different (or first call), report all items as updates.
    NSString *incomingAnchorString = anchor ? [[NSString alloc] initWithData:anchor encoding:NSUTF8StringEncoding] : @"";

    // Always report all items as updates. The system may have a cached anchor
    // from a previous run where items were enumerated but not successfully stored
    // (e.g. due to a crash). Reporting all items is idempotent — fileproviderd
    // will reconcile against its database.
    os_log_info(enumeratorLog(), "enumerateChanges: reporting all items (incoming=%{public}@ current=%{public}@)",
                incomingAnchorString, currentAnchorString);

    if (allItems) {
        // Filter items for this container and report them as updates.
        NSMutableArray<FileProviderItem *> *updatedItems = [NSMutableArray array];
        NSMutableSet<NSString *> *currentFileIds = [NSMutableSet set];
        NSString *targetParentPath = nil;

        if ([_containerId isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
            targetParentPath = @"";
        } else if ([_containerId isEqualToString:NSFileProviderWorkingSetContainerItemIdentifier]) {
            targetParentPath = nil;
        } else if ([_containerId isEqualToString:NSFileProviderTrashContainerItemIdentifier]) {
            // Trash: no changes.
            [observer finishEnumeratingChangesUpToSyncAnchor:currentAnchor moreComing:NO];
            return;
        } else {
            for (NSDictionary *item in allItems) {
                if ([item[@"fileId"] isEqualToString:_containerId]) {
                    targetParentPath = item[@"path"];
                    break;
                }
            }
            if (!targetParentPath) {
                os_log_error(enumeratorLog(), "enumerateChanges: container %{public}@ not found in metadata — skipping", _containerId);
                [observer finishEnumeratingChangesUpToSyncAnchor:currentAnchor moreComing:NO];
                return;
            }
        }

        for (NSDictionary *dict in allItems) {
            if (targetParentPath != nil && ![dict[@"parentPath"] isEqualToString:targetParentPath]) {
                continue;
            }
            FileProviderItem *item = [[FileProviderItem alloc] initWithDictionary:dict];
            [updatedItems addObject:item];
            [currentFileIds addObject:dict[@"fileId"] ?: @""];
        }

        os_log_info(enumeratorLog(), "enumerateChanges: reporting %lu updated items for %{public}@",
                    (unsigned long)updatedItems.count, _containerId);

        if (updatedItems.count > 0) {
            [observer didUpdateItems:updatedItems];
        }

        // Detect deleted items by comparing current fileIds with the set from the
        // previous enumerateChanges call. Report deletions so fileproviderd removes
        // them from Finder.
        // Cache key must include the domain identifier so that multiple
        // domains (spaces/accounts) don't overwrite each other's caches.
        NSString *cacheKey = [NSString stringWithFormat:@"prevFileIds_%@_%@",
            _domain.identifier,
            [_containerId stringByReplacingOccurrencesOfString:@"/" withString:@"_"]];
        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (containerURL) {
            NSURL *cacheURL = [containerURL URLByAppendingPathComponent:
                [NSString stringWithFormat:@"%@.plist", cacheKey]];
            NSArray *previousIds = [NSArray arrayWithContentsOfURL:cacheURL];
            if (previousIds) {
                NSMutableSet *previousSet = [NSMutableSet setWithArray:previousIds];
                [previousSet minusSet:currentFileIds];
                if (previousSet.count > 0) {
                    NSArray<NSFileProviderItemIdentifier> *deletedIds = [previousSet allObjects];
                    os_log_info(enumeratorLog(), "enumerateChanges: reporting %lu deleted items for %{public}@",
                                (unsigned long)deletedIds.count, _containerId);
                    [observer didDeleteItemsWithIdentifiers:deletedIds];
                }
            }
            // Save current set for next comparison.
            [[currentFileIds allObjects] writeToURL:cacheURL atomically:YES];
        }
    }

    [observer finishEnumeratingChangesUpToSyncAnchor:currentAnchor moreComing:NO];
}

- (void)currentSyncAnchorWithCompletionHandler:(void (^)(NSFileProviderSyncAnchor _Nullable))completionHandler {
    // Build a content-based anchor from the shared metadata.
    NSArray<NSDictionary *> *allItems = readSharedMetadata(_domain);
    NSString *anchorString = @"empty";
    if (allItems) {
        int64_t latestModtime = 0;
        for (NSDictionary *dict in allItems) {
            int64_t mt = [dict[@"modtime"] longLongValue];
            if (mt > latestModtime) latestModtime = mt;
        }
        anchorString = [NSString stringWithFormat:@"%lu-%lld",
                        (unsigned long)allItems.count, latestModtime];
    }
    NSData *anchorData = [anchorString dataUsingEncoding:NSUTF8StringEncoding];

    os_log_info(enumeratorLog(), "currentSyncAnchor: %{public}@", anchorString);

    completionHandler(anchorData);
}

- (void)invalidate {
    os_log_info(enumeratorLog(), "Enumerator invalidated for container: %{public}@", _containerId);
    _invalidated = YES;
    _xpcService = nil;
}

@end
