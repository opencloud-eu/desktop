// FileProviderEnumerator -- serves directory listings to Finder by querying the
// server live (PROPFIND Depth:1). The extension is the source of truth; results
// are cached for itemForIdentifier, change detection and offline display.

#import "FileProviderEnumerator.h"

#import "FileProviderItem.h"
#import "FileProviderItemCache.h"
#import "FileProviderWebDAV.h"
#import "FileProviderConfig.h"
#import "FileProviderWorkingSetDelta.h"
#import "FileProviderXPCService.h" // kOpenCloudAppGroupIdentifier

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

/// Reads the shared metadata plist for a domain (written by the main app's sync
/// engine after each sync). Used to drive the working-set change channel so that
/// items newly discovered on the server propagate to Finder. Returns nil if absent.
static NSArray<NSDictionary *> *readSharedMetadata(NSFileProviderDomain *domain) {
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) return nil;
    NSString *perDomain = [NSString stringWithFormat:@"fileprovider_items_%@.plist", domain.identifier];
    NSURL *url = [containerURL URLByAppendingPathComponent:perDomain];
    if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
        url = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
    }
    NSData *data = [NSData dataWithContentsOfURL:url];
    if (!data) return nil;
    NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                              options:NSPropertyListImmutable
                                                               format:nil error:nil];
    if (![items isKindOfClass:[NSArray class]]) return nil;

    // Filter out oCIS-internal items that the live PROPFIND enumeration does NOT
    // return (e.g. the ".space" space-metadata marker). Reporting them in the
    // working set while the per-folder PROPFIND enumeration omits them makes
    // fileproviderd see an inconsistency → endless reconciliation (createItem
    // ".space" loop) that blocks new creates and change propagation.
    NSMutableArray<NSDictionary *> *filtered = [NSMutableArray arrayWithCapacity:items.count];
    for (NSDictionary *dict in items) {
        NSString *name = dict[@"filename"] ?: dict[@"name"] ?: @"";
        if ([name isEqualToString:@".space"]) continue;
        [filtered addObject:dict];
    }
    return filtered;
}

/// Cache key (not a real path) for the per-domain working-set snapshot.
static NSString *const kWorkingSetCacheKey = @"::workingset::";

/// oCIS-internal items that must NOT be shown to the user and that the main app's
/// sync does NOT put in the plist. The live PROPFIND returns ".space" at a space
/// root; if the folder enumeration shows it but the working set/plist omits it,
/// fileproviderd loops reconciliation (createItem ".space"). Filter it from BOTH
/// the PROPFIND folder enumeration and the plist so the two sources agree.
static BOOL isHiddenOcEntry(NSString *name) {
    return [name isEqualToString:@".space"];
}

/// Content signature of the shared metadata, used as the working-set sync anchor
/// so fileproviderd re-checks when the main app's sync changes the plist.
/// Uses FPItemSetSignature (hash of every item's fileId|path|etag) rather than
/// count+max(modtime): a server-side rename keeps both count and modtime, so the
/// old signature collided and renames never re-enumerated.
static NSString *workingSetSignature(NSArray<NSDictionary *> *allItems) {
    return FPItemSetSignature(allItems);
}

/// Content signature of a single folder's direct children FROM THE SHARED PLIST
/// (refreshed by the main app's sync). Used as the per-folder sync anchor so the
/// anchor CHANGES when the server adds/removes/renames a child — which is what
/// makes fileproviderd call enumerateChanges for that folder. (A cached PROPFIND
/// etag would never change on its own, so folders were never re-enumerated.)
static NSString *folderChildrenSignature(NSFileProviderDomain *domain, NSString *relPath) {
    NSArray<NSDictionary *> *all = readSharedMetadata(domain) ?: @[];
    NSString *target = relPath ?: @"";
    NSMutableArray<NSDictionary *> *children = [NSMutableArray array];
    for (NSDictionary *d in all) {
        NSString *pp = d[@"parentPath"] ?: @"";
        if ([pp isEqualToString:target]) [children addObject:d];
    }
    return FPItemSetSignature(children);
}

typedef NS_ENUM(NSInteger, FPContainerKind) {
    FPContainerKindRoot,
    FPContainerKindWorkingSet,
    FPContainerKindTrash,
    FPContainerKindFolder,
    FPContainerKindUnknown,
};

/// Maps a raw PROPFIND/network error into a user-friendly NSFileProviderError that
/// Finder surfaces nicely (instead of e.g. "PROPFIND HTTP 401"). The common case is
/// that the OpenCloud app is closed, so its access token is no longer being refreshed.
static NSError *friendlyEnumerationError(NSError *err) {
    BOOL isHTTP = [err.domain isEqualToString:@"FileProviderWebDAV"];
    NSInteger code = err.code;

    if (isHTTP && (code == 401 || code == 403)) {
        return [NSError errorWithDomain:NSFileProviderErrorDomain
                                   code:NSFileProviderErrorNotAuthenticated
                               userInfo:@{NSLocalizedDescriptionKey:
            NSLocalizedString(@"Bitte öffne die OpenCloud App und melde dich an, um auf deine Dateien zuzugreifen.",
                              @"FileProvider enumerate 401")}];
    }
    if (isHTTP && code == 404) {
        return [NSError errorWithDomain:NSFileProviderErrorDomain
                                   code:NSFileProviderErrorNoSuchItem
                               userInfo:@{NSLocalizedDescriptionKey:
            NSLocalizedString(@"Dieser Ordner ist auf dem Server nicht mehr vorhanden.",
                              @"FileProvider enumerate 404")}];
    }
    // Network failure / server not reachable / app not running.
    return [NSError errorWithDomain:NSFileProviderErrorDomain
                               code:NSFileProviderErrorServerUnreachable
                           userInfo:@{NSLocalizedDescriptionKey:
        NSLocalizedString(@"OpenCloud ist gerade nicht erreichbar. Bitte öffne die OpenCloud App und versuche es erneut.",
                          @"FileProvider enumerate unreachable")}];
}

#pragma mark - FileProviderEnumerator

API_AVAILABLE(macos(12.0))
@implementation FileProviderEnumerator {
    NSFileProviderItemIdentifier _containerId;
    NSFileProviderDomain *_domain;
    FileProviderItemCache *_cache;
    BOOL _invalidated;
}

- (instancetype)initWithContainerIdentifier:(NSFileProviderItemIdentifier)containerId
                                     domain:(NSFileProviderDomain *)domain
                                      cache:(FileProviderItemCache *)cache {
    self = [super init];
    if (self) {
        _containerId = [containerId copy];
        _domain = domain;
        _cache = cache;
        _invalidated = NO;
        os_log_debug(enumeratorLog(), "Enumerator CREATED for container: %{public}@", containerId);
    }
    return self;
}

#pragma mark - Container resolution

/// Classifies the container and, for folders, resolves its space-relative path
/// (via the cache) and the identifier to report as children's parent.
- (FPContainerKind)kindForContainerRelPath:(NSString **)outRelPath
                            parentItemFileId:(NSString **)outFileId {
    if ([_containerId isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
        if (outRelPath) *outRelPath = @"";
        if (outFileId) *outFileId = NSFileProviderRootContainerItemIdentifier;
        return FPContainerKindRoot;
    }
    if ([_containerId isEqualToString:NSFileProviderWorkingSetContainerItemIdentifier]) {
        return FPContainerKindWorkingSet;
    }
    if ([_containerId isEqualToString:NSFileProviderTrashContainerItemIdentifier]) {
        return FPContainerKindTrash;
    }
    NSString *path = [_cache pathForFileId:_containerId];
    if (path == nil) {
        return FPContainerKindUnknown;
    }
    if (outRelPath) *outRelPath = path;
    if (outFileId) *outFileId = _containerId;
    return FPContainerKindFolder;
}

/// Builds the metadata dict consumed by FileProviderItem from a parsed entry.
static NSDictionary *itemDictFromEntry(FileProviderWebDAVEntry *e,
                                       NSString *parentFileId,
                                       NSString *davBase) {
    return @{
        @"fileId": e.fileId ?: @"",
        @"filename": e.name ?: @"",
        @"path": e.relativePath ?: @"",
        @"parentId": parentFileId ?: NSFileProviderRootContainerItemIdentifier,
        @"isDirectory": @(e.isDirectory),
        @"size": @(e.size),
        @"modtime": @(e.modtime),
        @"etag": e.etag ?: @"",
        @"isDownloaded": @(e.isDirectory ? YES : NO),
        @"davUrl": davBase ?: @"",
    };
}

#pragma mark - NSFileProviderEnumerator

- (void)enumerateItemsForObserver:(id<NSFileProviderEnumerationObserver>)observer
                   startingAtPage:(NSFileProviderPage)page {
    if (_invalidated) {
        [observer finishEnumeratingWithError:
            [NSError errorWithDomain:NSFileProviderErrorDomain
                                code:NSFileProviderErrorServerUnreachable
                            userInfo:@{NSLocalizedDescriptionKey: @"Enumerator invalidated"}]];
        return;
    }

    NSString *relPath = nil;
    NSString *parentFileId = nil;
    FPContainerKind kind = [self kindForContainerRelPath:&relPath parentItemFileId:&parentFileId];

    appendTrace([NSString stringWithFormat:@"[%@] enumerateItems container=%@ kind=%ld relPath=%@\n",
        [NSDate date], _containerId, (long)kind, relPath ?: @"(nil)"]);

    if (kind == FPContainerKindTrash) {
        [observer didEnumerateItems:@[]];
        [observer finishEnumeratingUpToPage:nil];
        return;
    }
    // Working set: seed from the shared plist (kept current by the main app's
    // sync). This is the global item set fileproviderd tracks; the per-folder
    // enumerators drive the live, browsable tree.
    if (kind == FPContainerKindWorkingSet) {
        NSArray<NSDictionary *> *allItems = readSharedMetadata(_domain) ?: @[];
        NSMutableArray<FileProviderItem *> *items = [NSMutableArray array];
        for (NSDictionary *dict in allItems) {
            [items addObject:[[FileProviderItem alloc] initWithDictionary:dict]];
            [_cache setMetadata:dict forFileId:dict[@"fileId"]];
        }
        appendTrace([NSString stringWithFormat:@"[%@] enumerateItems WORKINGSET items=%lu\n",
            [NSDate date], (unsigned long)items.count]);
        [observer didEnumerateItems:items];
        [observer finishEnumeratingUpToPage:nil];
        return;
    }
    if (kind == FPContainerKindUnknown) {
        os_log_error(enumeratorLog(), "enumerateItems: container %{public}@ not in cache", _containerId);
        // Transient so fileproviderd retries after the parent is enumerated.
        [observer finishEnumeratingWithError:
            [NSError errorWithDomain:NSFileProviderErrorDomain
                                code:NSFileProviderErrorServerUnreachable
                            userInfo:@{NSLocalizedDescriptionKey: @"Container not yet resolved"}]];
        return;
    }

    NSString *domainId = _domain.identifier;
    NSString *davBase = [FileProviderConfig davBaseForDomainIdentifier:domainId];
    NSString *token = [FileProviderConfig accessTokenForDomainIdentifier:domainId];

    if (davBase.length == 0 || token.length == 0) {
        // Not signed in yet: serve cached children if we have them, else error.
        if (![self serveCachedChildrenForRelPath:relPath toObserver:observer]) {
            [observer finishEnumeratingWithError:
                [NSError errorWithDomain:NSFileProviderErrorDomain
                                    code:NSFileProviderErrorNotAuthenticated
                                userInfo:@{NSLocalizedDescriptionKey:
                    @"Bitte in der OpenCloud App anmelden."}]];
        }
        return;
    }

    // Cache-first: if we already have a live snapshot of this folder, show it
    // instantly and refresh in the background, so Finder doesn't block on the
    // network for folders that were opened before. The first visit is still
    // served live (authoritative), so nothing goes missing.
    NSArray<NSString *> *cachedChildIds = [_cache childFileIdsForContainerPath:relPath];
    NSString *prevEtag = [_cache etagForContainerPath:relPath] ?: @"";
    BOOL alreadyServed = NO;
    if (cachedChildIds.count > 0) {
        [self serveCachedChildrenForRelPath:relPath toObserver:observer];
        alreadyServed = YES;
    }

    __block FileProviderEnumerator *strongSelf = self;
    [FileProviderWebDAV propfindChildrenAtDavBase:davBase
                                     relativePath:relPath
                                            token:token
                                       completion:^(NSArray<FileProviderWebDAVEntry *> *entries,
                                                    NSError *error) {
        if (error || entries == nil) {
            os_log_error(enumeratorLog(), "enumerateItems PROPFIND failed for %{public}@: %{public}@",
                         relPath, error.localizedDescription);
            if (alreadyServed) {
                // Cached contents were already shown; ignore the transient error.
                strongSelf = nil;
                return;
            }
            // Offline / transient: fall back to cached children.
            if (![strongSelf serveCachedChildrenForRelPath:relPath toObserver:observer]) {
                [observer finishEnumeratingWithError:friendlyEnumerationError(error)];
            }
            strongSelf = nil;
            return;
        }

        // Cache-first PROBE: when we already served from cache, do NOT mutate the
        // cache here. Just check the folder's etag. If it changed (oCIS bumps the
        // folder etag on ANY child add/remove/modify), ask Finder to re-enumerate
        // — enumerateChanges then runs the authoritative diff, which needs the OLD
        // cached child list as its baseline to detect DELETIONS. Overwriting the
        // cache here would erase that baseline and a deleted file would linger.
        if (alreadyServed) {
            NSString *newEtag = @"";
            for (FileProviderWebDAVEntry *e in entries) {
                if ([e.relativePath isEqualToString:relPath]) { newEtag = e.etag ?: @""; break; }
            }
            BOOL changed = ![newEtag isEqualToString:prevEtag];
            appendTrace([NSString stringWithFormat:@"[%@] enumerateItems PROBE container=%@ changed=%d\n",
                [NSDate date], strongSelf->_containerId, changed]);
            if (changed) {
                NSFileProviderManager *mgr = [NSFileProviderManager managerForDomain:strongSelf->_domain];
                [mgr signalEnumeratorForContainerItemIdentifier:strongSelf->_containerId
                                              completionHandler:^(NSError *e) {}];
            }
            strongSelf = nil;
            return;
        }

        // First visit: build the listing live, populate the cache, serve it.
        NSMutableArray<FileProviderItem *> *items = [NSMutableArray array];
        NSMutableArray<NSString *> *childIds = [NSMutableArray array];
        NSString *selfEtag = @"";

        for (FileProviderWebDAVEntry *e in entries) {
            // The folder itself (self): record its etag, do not list it as a child.
            if ([e.relativePath isEqualToString:relPath]) {
                selfEtag = e.etag ?: @"";
                continue;
            }
            if (isHiddenOcEntry(e.name)) continue; // keep consistent with the plist
            NSDictionary *dict = itemDictFromEntry(e, parentFileId, davBase);
            [items addObject:[[FileProviderItem alloc] initWithDictionary:dict]];
            [childIds addObject:e.fileId ?: @""];
            [strongSelf->_cache setMetadata:dict forFileId:e.fileId];
        }

        [strongSelf->_cache setContainerPath:relPath etag:selfEtag childFileIds:childIds];
        [strongSelf->_cache save];

        os_log_info(enumeratorLog(), "enumerateItems: %lu items for %{public}@",
                    (unsigned long)items.count, relPath);
        appendTrace([NSString stringWithFormat:@"[%@] enumerateItems RESULT container=%@ items=%lu\n",
            [NSDate date], strongSelf->_containerId, (unsigned long)items.count]);

        [observer didEnumerateItems:items];
        [observer finishEnumeratingUpToPage:nil];
        strongSelf = nil;
    }];
}

/// Serves the last-known children of a folder from the cache (offline path).
/// Returns NO if nothing is cached.
- (BOOL)serveCachedChildrenForRelPath:(NSString *)relPath
                           toObserver:(id<NSFileProviderEnumerationObserver>)observer {
    NSArray<NSString *> *childIds = [_cache childFileIdsForContainerPath:relPath];
    if (childIds.count == 0) return NO;

    NSMutableArray<FileProviderItem *> *items = [NSMutableArray array];
    for (NSString *fid in childIds) {
        NSDictionary *md = [_cache metadataForFileId:fid];
        if (md) [items addObject:[[FileProviderItem alloc] initWithDictionary:md]];
    }
    [observer didEnumerateItems:items];
    [observer finishEnumeratingUpToPage:nil];
    os_log_info(enumeratorLog(), "enumerateItems: served %lu cached items for %{public}@",
                (unsigned long)items.count, relPath);
    return YES;
}

- (void)enumerateChangesForObserver:(id<NSFileProviderChangeObserver>)observer
                     fromSyncAnchor:(NSFileProviderSyncAnchor)anchor {
    if (_invalidated) {
        [observer finishEnumeratingWithError:
            [NSError errorWithDomain:NSFileProviderErrorDomain
                                code:NSFileProviderErrorServerUnreachable
                            userInfo:@{NSLocalizedDescriptionKey: @"Enumerator invalidated"}]];
        return;
    }

    NSString *relPath = nil;
    NSString *parentFileId = nil;
    FPContainerKind kind = [self kindForContainerRelPath:&relPath parentItemFileId:&parentFileId];

    // Working set: the global change channel. Diff the shared plist (refreshed by
    // the main app's sync) against the last snapshot so server-side additions and
    // deletions propagate to Finder.
    if (kind == FPContainerKindWorkingSet) {
        NSArray<NSDictionary *> *allItems = readSharedMetadata(_domain) ?: @[];

        // Diff against the previous snapshot so only genuinely new/changed items
        // are reported. Reporting every item on every pass made fileproviderd
        // re-index the whole tree every ~30s (huge CPU + constant Finder view
        // churn). Build the previous etag map from the cache BEFORE overwriting.
        NSArray<NSString *> *prevIds = [_cache childFileIdsForContainerPath:kWorkingSetCacheKey] ?: @[];
        NSMutableArray<NSDictionary *> *previousItems =
            [NSMutableArray arrayWithCapacity:prevIds.count];
        for (NSString *fid in prevIds) {
            NSDictionary *md = [_cache metadataForFileId:fid];
            if (md) [previousItems addObject:md];
        }

        FPWorkingSetDelta *delta = FPComputeWorkingSetDelta(allItems, previousItems);

        // Refresh the cache for all current items (path/parent lookups stay current).
        for (NSDictionary *dict in allItems) {
            [_cache setMetadata:dict forFileId:dict[@"fileId"]];
        }

        if (delta.deletedFileIds.count > 0) {
            [observer didDeleteItemsWithIdentifiers:delta.deletedFileIds];
        }
        if (delta.changedItems.count > 0) {
            NSMutableArray<FileProviderItem *> *updated =
                [NSMutableArray arrayWithCapacity:delta.changedItems.count];
            for (NSDictionary *dict in delta.changedItems) {
                [updated addObject:[[FileProviderItem alloc] initWithDictionary:dict]];
            }
            [observer didUpdateItems:updated];
        }

        NSString *sig = workingSetSignature(allItems);
        [_cache setContainerPath:kWorkingSetCacheKey etag:sig childFileIds:delta.currentFileIds];
        [_cache save];
        appendTrace([NSString stringWithFormat:@"[%@] enumerateChanges WORKINGSET upd=%lu del=%lu (of %lu)\n",
            [NSDate date], (unsigned long)delta.changedItems.count,
            (unsigned long)delta.deletedFileIds.count, (unsigned long)allItems.count]);
        [observer finishEnumeratingChangesUpToSyncAnchor:[self anchorData:sig] moreComing:NO];
        return;
    }
    // Trash / unknown: report no changes.
    if (kind != FPContainerKindRoot && kind != FPContainerKindFolder) {
        [observer finishEnumeratingChangesUpToSyncAnchor:[self anchorData:@""] moreComing:NO];
        return;
    }

    NSString *domainId = _domain.identifier;
    NSString *davBase = [FileProviderConfig davBaseForDomainIdentifier:domainId];
    NSString *token = [FileProviderConfig accessTokenForDomainIdentifier:domainId];
    if (davBase.length == 0 || token.length == 0) {
        [observer finishEnumeratingChangesUpToSyncAnchor:
            [self anchorData:folderChildrenSignature(_domain, relPath)] moreComing:NO];
        return;
    }

    NSArray<NSString *> *previousChildIds = [_cache childFileIdsForContainerPath:relPath] ?: @[];

    __block FileProviderEnumerator *strongSelf = self;
    [FileProviderWebDAV propfindChildrenAtDavBase:davBase
                                     relativePath:relPath
                                            token:token
                                       completion:^(NSArray<FileProviderWebDAVEntry *> *entries,
                                                    NSError *error) {
        if (error || entries == nil) {
            // Keep the existing anchor; try again on the next signal.
            [observer finishEnumeratingChangesUpToSyncAnchor:
                [strongSelf anchorData:folderChildrenSignature(strongSelf->_domain, relPath)] moreComing:NO];
            strongSelf = nil;
            return;
        }

        NSMutableArray<FileProviderItem *> *updated = [NSMutableArray array];
        NSMutableArray<NSString *> *currentIds = [NSMutableArray array];
        NSString *selfEtag = @"";

        for (FileProviderWebDAVEntry *e in entries) {
            if ([e.relativePath isEqualToString:relPath]) { selfEtag = e.etag ?: @""; continue; }
            if (isHiddenOcEntry(e.name)) continue; // keep consistent with the plist
            NSDictionary *dict = itemDictFromEntry(e, parentFileId, davBase);
            [updated addObject:[[FileProviderItem alloc] initWithDictionary:dict]];
            [currentIds addObject:e.fileId ?: @""];
            [strongSelf->_cache setMetadata:dict forFileId:e.fileId];
        }

        // Deletions: previously-known children no longer present.
        NSMutableSet<NSString *> *deleted = [NSMutableSet setWithArray:previousChildIds];
        [deleted minusSet:[NSSet setWithArray:currentIds]];
        if (deleted.count > 0) {
            [observer didDeleteItemsWithIdentifiers:[deleted allObjects]];
        }
        if (updated.count > 0) {
            [observer didUpdateItems:updated];
        }

        [strongSelf->_cache setContainerPath:relPath etag:selfEtag childFileIds:currentIds];
        [strongSelf->_cache save];

        NSString *folderAnchor = folderChildrenSignature(strongSelf->_domain, relPath);
        appendTrace([NSString stringWithFormat:@"[%@] enumerateChanges RESULT container=%@ upd=%lu del=%lu anchor=%@\n",
            [NSDate date], strongSelf->_containerId,
            (unsigned long)updated.count, (unsigned long)deleted.count, folderAnchor]);

        [observer finishEnumeratingChangesUpToSyncAnchor:[strongSelf anchorData:folderAnchor] moreComing:NO];
        strongSelf = nil;
    }];
}

- (NSData *)anchorData:(NSString *)s {
    return [(s ?: @"") dataUsingEncoding:NSUTF8StringEncoding];
}

- (void)currentSyncAnchorWithCompletionHandler:(void (^)(NSFileProviderSyncAnchor _Nullable))completionHandler {
    NSString *relPath = nil;
    FPContainerKind kind = [self kindForContainerRelPath:&relPath parentItemFileId:nil];
    NSString *anchor;
    if (kind == FPContainerKindWorkingSet) {
        // Content signature of the shared plist so the anchor changes whenever the
        // main app's sync adds/removes items → fileproviderd calls enumerateChanges.
        anchor = workingSetSignature(readSharedMetadata(_domain) ?: @[]);
    } else if (kind == FPContainerKindRoot || kind == FPContainerKindFolder) {
        // Per-folder child signature from the plist: changes when a child is added
        // or removed on the server → fileproviderd re-enumerates this folder.
        anchor = folderChildrenSignature(_domain, relPath);
    } else {
        anchor = @"";
    }
    completionHandler([self anchorData:anchor]);
}

- (void)invalidate {
    os_log_info(enumeratorLog(), "Enumerator invalidated for container: %{public}@", _containerId);
    _invalidated = YES;
}

@end
