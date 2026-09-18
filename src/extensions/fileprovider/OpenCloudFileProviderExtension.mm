// OpenCloudFileProviderExtension -- NSFileProviderReplicatedExtension implementation.
// Runs as a separate process managed by the macOS File Provider framework.

#import "OpenCloudFileProviderExtension.h"

#import "FileProviderEnumerator.h"
#import "FileProviderItem.h"
#import "FileProviderItemCache.h"
#import "FileProviderThumbnails.h"
#import "FileProviderXPCService.h"

static os_log_t extensionLog(void) {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("eu.opencloud.desktop.fileprovider", "extension");
    });
    return log;
}

/// Appends a trace line to the debug log file in the App Group container.
static NSString *traceLogPath(void) {
    // Try App Group container first
    NSURL *container = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (container) {
        return [[container URLByAppendingPathComponent:@"fp_debug.log"] path];
    }
    // Fallback to sandbox temp dir
    return [NSTemporaryDirectory() stringByAppendingPathComponent:@"fp_debug.log"];
}

static void appendTrace(NSString *line) {
    NSString *path = traceLogPath();
    NSFileHandle *fh = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!fh) {
        [[NSFileManager defaultManager] createFileAtPath:path contents:nil attributes:nil];
        fh = [NSFileHandle fileHandleForWritingAtPath:path];
    }
    if (fh) {
        [fh seekToEndOfFile];
        [fh writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
        [fh closeFile];
    }
}

/// Creates an NSError in the file provider extension domain for "not implemented" stubs.
static NSError *notImplementedError(void) {
    return [NSError errorWithDomain:NSFileProviderErrorDomain
                               code:NSFileProviderErrorServerUnreachable
                           userInfo:@{NSLocalizedDescriptionKey: @"Not yet implemented"}];
}

/// Creates an NSError indicating the user is not authenticated.
static NSError *notAuthenticatedError(void) {
    return [NSError errorWithDomain:NSFileProviderErrorDomain
                               code:NSFileProviderErrorNotAuthenticated
                           userInfo:@{NSLocalizedDescriptionKey:
        NSLocalizedString(@"Bitte melde dich in der OpenCloud App an, um auf deine Dateien zugreifen zu können.",
                          @"FileProvider auth error")}];
}

/// Creates a user-visible error for configuration/connectivity issues.
static NSError *configUnavailableError(NSString *detail) {
    NSString *message = [NSString stringWithFormat:
        NSLocalizedString(@"Die OpenCloud App muss gestartet und angemeldet sein, um Dateien herunterladen zu können. (%@)",
                          @"FileProvider config error"), detail];
    return [NSError errorWithDomain:NSFileProviderErrorDomain
                               code:NSFileProviderErrorServerUnreachable
                           userInfo:@{NSLocalizedDescriptionKey: message}];
}

// Keep old name as alias for backward compatibility in non-download code paths.
static NSError *xpcUnavailableError(void) {
    return configUnavailableError(@"Hintergrund-Synchronisation nicht verfügbar");
}

/// Returns the per-domain config plist filename, falling back to the legacy
/// global filename if the per-domain file does not exist yet.
static NSString *configPlistName(NSFileProviderDomain *domain, NSURL *containerURL) {
    NSString *perDomain = [NSString stringWithFormat:@"fileprovider_config_%@.plist", domain.identifier];
    if ([[NSFileManager defaultManager] fileExistsAtPath:
         [[containerURL URLByAppendingPathComponent:perDomain] path]]) {
        return perDomain;
    }
    return @"fileprovider_config.plist"; // legacy fallback
}

/// Returns the per-domain items plist filename, falling back to the legacy
/// global filename if the per-domain file does not exist yet.
static NSString *itemsPlistName(NSFileProviderDomain *domain, NSURL *containerURL) {
    NSString *perDomain = [NSString stringWithFormat:@"fileprovider_items_%@.plist", domain.identifier];
    if ([[NSFileManager defaultManager] fileExistsAtPath:
         [[containerURL URLByAppendingPathComponent:perDomain] path]]) {
        return perDomain;
    }
    return @"fileprovider_items.plist"; // legacy fallback
}

#pragma mark - Default Item Capabilities

/// Returns default NSFileProviderItemCapabilities for items served by this extension.
static NSFileProviderItemCapabilities defaultItemCapabilities(void) {
    return NSFileProviderItemCapabilitiesAllowsReading
         | NSFileProviderItemCapabilitiesAllowsWriting
         | NSFileProviderItemCapabilitiesAllowsRenaming
         | NSFileProviderItemCapabilitiesAllowsReparenting
         | NSFileProviderItemCapabilitiesAllowsTrashing
         | NSFileProviderItemCapabilitiesAllowsDeleting;
}

#pragma mark - OpenCloudFileProviderExtension

API_AVAILABLE(macos(12.0))
@implementation OpenCloudFileProviderExtension {
    NSFileProviderDomain *_domain;
    FileProviderXPCService *_xpcService;
    FileProviderThumbnails *_thumbnails;

    /// Serialisation queue for hydration coalescing state.
    dispatch_queue_t _hydrationQueue;

    /// Maps file identifiers to arrays of pending completion handlers for in-flight
    /// hydration requests. When a hydration is already in progress for a given fileId,
    /// subsequent requests queue their handlers here instead of issuing a second XPC call.
    NSMutableDictionary<NSString *, NSMutableArray *> *_pendingHydrations;

    /// Shared per-domain metadata cache populated by the enumerator's live
    /// PROPFIND results. Source of truth for itemForIdentifier and hydration paths.
    FileProviderItemCache *_itemCache;
}

#pragma mark - Lifecycle

- (instancetype)initWithDomain:(NSFileProviderDomain *)domain {
    self = [super init];
    if (self) {
        _domain = domain;
        // Multiple trace mechanisms to diagnose
        NSLog(@">>> EXTENSION INIT domain=%@", domain.identifier);
        os_log_debug(extensionLog(), "EXTENSION INIT domain=%{public}@", domain.identifier);

        // Try writing to a KNOWN writable location
        NSString *homeDir = NSHomeDirectory();
        NSString *tracePath = [homeDir stringByAppendingPathComponent:@"fp_debug.log"];
        NSString *initLine = [NSString stringWithFormat:@"INIT domain=%@ home=%@\n", domain.identifier, homeDir];
        [initLine writeToFile:tracePath atomically:YES encoding:NSUTF8StringEncoding error:nil];

        appendTrace([NSString stringWithFormat:@"[%@] EXTENSION INIT domain=%@\n",
            [NSDate date], domain.identifier]);
        _xpcService = [[FileProviderXPCService alloc] init];
        _hydrationQueue = dispatch_queue_create("eu.opencloud.desktop.fileprovider.hydration",
                                                DISPATCH_QUEUE_SERIAL);
        _pendingHydrations = [[NSMutableDictionary alloc] init];

        // Per-domain metadata cache backing server-driven enumeration.
        NSURL *groupURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (groupURL) {
            NSURL *cacheURL = [groupURL URLByAppendingPathComponent:
                [NSString stringWithFormat:@"fileprovider_idcache_%@.plist", domain.identifier]];
            _itemCache = [[FileProviderItemCache alloc] initWithFileURL:cacheURL];
        }

        _thumbnails = [[FileProviderThumbnails alloc] initWithXPCService:_xpcService];
        os_log_info(extensionLog(), "Extension initialized for domain: %{public}@", domain.identifier);
    }
    return self;
}

- (void)invalidate {
    os_log_info(extensionLog(), "Extension invalidated for domain: %{public}@", _domain.identifier);
    [_xpcService invalidate];
    _xpcService = nil;
}

#pragma mark - NSFileProviderReplicatedExtension (Item Lookup)

- (NSProgress *)itemForIdentifier:(NSFileProviderItemIdentifier)identifier
                          request:(NSFileProviderRequest *)request
                completionHandler:(void (^)(NSFileProviderItem _Nullable, NSError * _Nullable))completionHandler {
    os_log_info(extensionLog(), "itemForIdentifier: %{public}@", identifier);

    // Root container and trash are always resolvable without data.
    if ([identifier isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
        completionHandler([FileProviderItem rootContainerItem], nil);
        return [NSProgress discreteProgressWithTotalUnitCount:0];
    }
    if ([identifier isEqualToString:NSFileProviderTrashContainerItemIdentifier]) {
        FileProviderItem *trashItem = [[FileProviderItem alloc]
            initWithIdentifier:NSFileProviderTrashContainerItemIdentifier
                      filename:@".Trash"
              parentIdentifier:NSFileProviderRootContainerItemIdentifier
                   isDirectory:YES
                          size:0
                       modDate:nil];
        completionHandler(trashItem, nil);
        return [NSProgress discreteProgressWithTotalUnitCount:0];
    }

    // Cache first: populated by the enumerator's live PROPFIND results.
    if (_itemCache) {
        NSDictionary *md = [_itemCache metadataForFileId:identifier];
        if (md) {
            os_log_info(extensionLog(), "itemForIdentifier: found %{public}@ in cache", identifier);
            completionHandler([[FileProviderItem alloc] initWithDictionary:md], nil);
            return [NSProgress discreteProgressWithTotalUnitCount:0];
        }
    }

    // Fallback: look up the item from the shared metadata plist in the App Group
    // container (legacy path while the main app still writes it).
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (containerURL) {
        NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
        NSData *data = [NSData dataWithContentsOfURL:metadataURL];
        if (data) {
            NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                                      options:NSPropertyListImmutable
                                                                       format:nil
                                                                        error:nil];
            for (NSDictionary *dict in items) {
                if ([dict[@"fileId"] isEqualToString:identifier]) {
                    FileProviderItem *item = [[FileProviderItem alloc] initWithDictionary:dict];
                    os_log_info(extensionLog(), "itemForIdentifier: found %{public}@ in shared metadata", identifier);
                    completionHandler(item, nil);
                    return [NSProgress discreteProgressWithTotalUnitCount:0];
                }
            }
        }
    }

    os_log_error(extensionLog(), "itemForIdentifier: %{public}@ not found in shared metadata", identifier);
    completionHandler(nil, [NSError errorWithDomain:NSFileProviderErrorDomain
                                               code:NSFileProviderErrorNoSuchItem
                                           userInfo:@{NSLocalizedDescriptionKey: @"Item not found"}]);
    return [NSProgress discreteProgressWithTotalUnitCount:0];
}

#pragma mark - NSFileProviderReplicatedExtension (Content Fetch)

- (NSProgress *)fetchContentsForItemWithIdentifier:(NSFileProviderItemIdentifier)itemIdentifier
                                           version:(NSFileProviderItemVersion *)requestedVersion
                                           request:(NSFileProviderRequest *)request
                                 completionHandler:(void (^)(NSURL * _Nullable, NSFileProviderItem _Nullable, NSError * _Nullable))completionHandler {
    os_log_info(extensionLog(), "fetchContents: hydration requested for %{public}@", itemIdentifier);

    NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:100];

    // Note: NSFileProviderRequest.isCancelled not available pre-macOS 15; skip check.

    NSString *fileId = [itemIdentifier copy];

    // Coalesce concurrent hydration requests for the same identifier.
    dispatch_async(_hydrationQueue, ^{
        NSMutableArray *existingHandlers = self->_pendingHydrations[fileId];
        if (existingHandlers != nil) {
            // A hydration for this fileId is already in flight — queue up.
            os_log_info(extensionLog(), "fetchContents: coalescing hydration for %{public}@", fileId);
            [existingHandlers addObject:[completionHandler copy]];
            return;
        }

        // First request for this fileId — start the hydration.
        self->_pendingHydrations[fileId] = [NSMutableArray arrayWithObject:[completionHandler copy]];

        // --- Direct download: read config from App Group container ---
        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (!containerURL) {
            os_log_error(extensionLog(), "fetchContents: cannot access App Group container");
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:configUnavailableError(@"App-Container nicht verfügbar")];
            return;
        }

        // Read server config (davUrl + accessToken).
        NSURL *configURL = [containerURL URLByAppendingPathComponent:configPlistName(self->_domain, containerURL)];
        NSData *configData = [NSData dataWithContentsOfURL:configURL];
        if (!configData) {
            os_log_error(extensionLog(), "fetchContents: config plist not found at %{public}@", configURL.path);
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:configUnavailableError(@"Server-Konfiguration nicht gefunden")];
            return;
        }
        NSDictionary *config = [NSPropertyListSerialization propertyListWithData:configData
                                                                         options:NSPropertyListImmutable
                                                                          format:nil error:nil];
        NSString *davUrl = config[@"davUrl"]; // fallback
        NSString *accessToken = config[@"accessToken"];
        if (!accessToken || accessToken.length == 0) {
            os_log_error(extensionLog(), "fetchContents: no access token — app may still be starting");
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:configUnavailableError(@"Anmeldung wird vorbereitet — bitte kurz warten")];
            return;
        }

        // Resolve the file path. Cache first (server-driven enumeration), then
        // fall back to the legacy items plist.
        NSString *filePath = nil;
        NSDictionary *itemDict = nil;
        if (self->_itemCache) {
            NSDictionary *md = [self->_itemCache metadataForFileId:fileId];
            if (md[@"path"]) { filePath = md[@"path"]; itemDict = md; }
        }
        NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
        NSData *metaData = filePath ? nil : [NSData dataWithContentsOfURL:metadataURL];
        if (metaData) {
            NSArray *items = [NSPropertyListSerialization propertyListWithData:metaData
                                                                       options:NSPropertyListImmutable
                                                                        format:nil error:nil];
            for (NSDictionary *item in items) {
                if ([item[@"fileId"] isEqualToString:fileId]) {
                    filePath = item[@"path"];
                    itemDict = item;
                    break;
                }
            }
        }
        if (!filePath) {
            os_log_error(extensionLog(), "fetchContents: fileId %{public}@ not found in items plist", fileId);
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:[NSError errorWithDomain:NSFileProviderErrorDomain
                                          code:NSFileProviderErrorNoSuchItem
                                      userInfo:@{NSLocalizedDescriptionKey:
                    NSLocalizedString(@"Diese Datei wurde nicht gefunden. Möglicherweise wurde sie verschoben oder gelöscht.",
                                      @"FileProvider item not found")}]];
            return;
        }

        // Use per-item davUrl if available (correct space), fall back to global config.
        if (itemDict[@"davUrl"]) {
            davUrl = itemDict[@"davUrl"];
        }
        if (!davUrl || davUrl.length == 0) {
            os_log_error(extensionLog(), "fetchContents: no davUrl for %{public}@", fileId);
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:configUnavailableError(@"Server-URL nicht konfiguriert")];
            return;
        }

        // Build the WebDAV download URL: davUrl + "/" + filePath
        NSString *davBase = [davUrl hasSuffix:@"/"] ? [davUrl substringToIndex:davUrl.length - 1] : davUrl;
        NSString *encodedPath = [filePath stringByAddingPercentEncodingWithAllowedCharacters:
                                 [NSCharacterSet URLPathAllowedCharacterSet]];
        NSString *downloadURLString = [NSString stringWithFormat:@"%@/%@", davBase, encodedPath];
        NSURL *downloadURL = [NSURL URLWithString:downloadURLString];

        os_log_info(extensionLog(), "fetchContents: downloading %{public}@ from %{public}@",
                    fileId, downloadURLString);

        // Create temp file URL.
        NSString *tempDir = NSTemporaryDirectory();
        NSString *tempFilename = [NSString stringWithFormat:@"hydration-%@", [[NSUUID UUID] UUIDString]];
        NSURL *tempURL = [NSURL fileURLWithPath:[tempDir stringByAppendingPathComponent:tempFilename]];

        // Use NSURLSession to download the file directly.
        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:downloadURL];
        [req setValue:[NSString stringWithFormat:@"Bearer %@", accessToken]
   forHTTPHeaderField:@"Authorization"];

        NSURLSessionConfiguration *sessionConfig = [NSURLSessionConfiguration defaultSessionConfiguration];
        NSURLSession *session = [NSURLSession sessionWithConfiguration:sessionConfig];

        NSDictionary *capturedItemDict = itemDict;

        // Use the file size from metadata to drive the progress indicator.
        int64_t expectedSize = [itemDict[@"size"] longLongValue];
        if (expectedSize > 0) {
            progress.totalUnitCount = expectedSize;
        }

        NSURLSessionDownloadTask *downloadTask = [session downloadTaskWithRequest:req completionHandler:^(NSURL *location, NSURLResponse *response, NSError *error) {
            if (error) {
                os_log_error(extensionLog(), "fetchContents: download failed for %{public}@: %{public}@",
                             fileId, error.localizedDescription);
                [self _completeHydrationForFileId:fileId url:nil item:nil error:error];
                return;
            }

            NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
            if (http.statusCode < 200 || http.statusCode >= 300) {
                os_log_error(extensionLog(), "fetchContents: HTTP %ld for %{public}@", (long)http.statusCode, fileId);
                NSFileProviderErrorCode fpCode;
                NSString *userMessage;
                if (http.statusCode == 401 || http.statusCode == 403) {
                    fpCode = NSFileProviderErrorNotAuthenticated;
                    userMessage = NSLocalizedString(
                        @"Die Anmeldung ist abgelaufen. Bitte melde dich in der OpenCloud App erneut an.",
                        @"FileProvider HTTP 401/403");
                } else if (http.statusCode == 404) {
                    // Use a transient error so fileproviderd retries later
                    // instead of permanently removing the item from Finder.
                    // The item will be cleaned up by the next sync cycle if
                    // it was truly deleted on the server.
                    fpCode = NSFileProviderErrorServerUnreachable;
                    userMessage = NSLocalizedString(
                        @"Diese Datei wurde auf dem Server nicht gefunden. Sie wurde möglicherweise gelöscht oder verschoben.",
                        @"FileProvider HTTP 404");
                } else if (http.statusCode >= 500) {
                    fpCode = NSFileProviderErrorServerUnreachable;
                    userMessage = [NSString stringWithFormat:
                        NSLocalizedString(@"Der Server hat einen Fehler gemeldet (HTTP %ld). Bitte versuche es später erneut.",
                                          @"FileProvider HTTP 5xx"), (long)http.statusCode];
                } else {
                    fpCode = NSFileProviderErrorServerUnreachable;
                    userMessage = [NSString stringWithFormat:
                        NSLocalizedString(@"Die Datei konnte nicht heruntergeladen werden (HTTP %ld).",
                                          @"FileProvider HTTP error"), (long)http.statusCode];
                }
                NSError *httpError = [NSError errorWithDomain:NSFileProviderErrorDomain
                                                         code:fpCode
                                                     userInfo:@{NSLocalizedDescriptionKey: userMessage}];
                [self _completeHydrationForFileId:fileId url:nil item:nil error:httpError];
                return;
            }

            // Move downloaded file to our temp path.
            NSError *moveError = nil;
            [[NSFileManager defaultManager] moveItemAtURL:location toURL:tempURL error:&moveError];
            if (moveError) {
                os_log_error(extensionLog(), "fetchContents: move failed: %{public}@", moveError.localizedDescription);
                [self _completeHydrationForFileId:fileId url:nil item:nil error:moveError];
                return;
            }

            os_log_info(extensionLog(), "fetchContents: download succeeded for %{public}@", fileId);
            progress.completedUnitCount = progress.totalUnitCount;

            // Build the item from the shared plist metadata, then mark it as
            // downloaded so fileproviderd knows the content is now available
            // locally and does not retry or show an error badge.
            NSMutableDictionary *itemDict = capturedItemDict
                ? [capturedItemDict mutableCopy]
                : nil;
            if (itemDict) {
                itemDict[@"isDownloaded"] = @YES;
            }
            FileProviderItem *item = itemDict
                ? [[FileProviderItem alloc] initWithDictionary:itemDict]
                : nil;
            [self _completeHydrationForFileId:fileId url:tempURL item:item error:nil];
        }];

        // Add the download task's built-in progress as a child of the progress
        // we return to fileproviderd, so Finder shows a real download indicator.
        [progress addChild:downloadTask.progress withPendingUnitCount:progress.totalUnitCount];

        [downloadTask resume];
    });

    return progress;
}

/// Dispatches all queued completion handlers for a given fileId and removes the
/// entry from the pending-hydrations map. Must be called on any queue -- it
/// internally hops to _hydrationQueue for thread safety.
- (void)_completeHydrationForFileId:(NSString *)fileId
                                url:(NSURL *)url
                               item:(NSFileProviderItem)item
                              error:(NSError *)error {
    dispatch_async(_hydrationQueue, ^{
        NSArray *handlers = [self->_pendingHydrations[fileId] copy];
        [self->_pendingHydrations removeObjectForKey:fileId];

        // Call handlers outside the queue to avoid blocking it.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
            for (void (^handler)(NSURL *, NSFileProviderItem, NSError *) in handlers) {
                handler(url, item, error);
            }
        });
    });
}

/// Removes a stale item (HTTP 404) from the shared fileprovider_items.plist
/// so subsequent enumerations no longer surface it to Finder.
- (void)_removeStaleItemFromPlist:(NSString *)fileId {
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) return;

    NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
    NSData *data = [NSData dataWithContentsOfURL:metadataURL];
    if (!data) return;

    NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                              options:NSPropertyListMutableContainers
                                                               format:nil
                                                                error:nil];
    if (![items isKindOfClass:[NSArray class]]) return;

    NSMutableArray *mutableItems = [items mutableCopy];
    NSUInteger indexToRemove = NSNotFound;
    for (NSUInteger i = 0; i < mutableItems.count; i++) {
        NSDictionary *item = mutableItems[i];
        if ([item[@"fileId"] isEqualToString:fileId]) {
            indexToRemove = i;
            break;
        }
    }

    if (indexToRemove != NSNotFound) {
        NSString *path = mutableItems[indexToRemove][@"path"];
        [mutableItems removeObjectAtIndex:indexToRemove];

        NSData *newData = [NSPropertyListSerialization dataWithPropertyList:mutableItems
                                                                    format:NSPropertyListBinaryFormat_v1_0
                                                                   options:0
                                                                     error:nil];
        if (newData) {
            [newData writeToURL:metadataURL atomically:YES];
            os_log_info(extensionLog(), "Removed stale item %{public}@ (%{public}@) from shared plist", fileId, path);
        }
    }
}

/// Updates an existing item's path and parent in the plist after a MOVE.
/// Does NOT set extensionCreated, so the item behaves like a journal-sourced
/// entry and gets cleaned up normally when deleted on the server.
- (void)_updateItemPathInPlist:(NSString *)fileId
                       newPath:(NSString *)newPath
                   newParentId:(NSString *)newParentId
                 newParentPath:(NSString *)newParentPath
                        davUrl:(NSString *)davUrl {
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) return;

    NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
    NSData *data = [NSData dataWithContentsOfURL:metadataURL];
    if (!data) return;

    NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                              options:NSPropertyListImmutable
                                                               format:nil error:nil];
    if (![items isKindOfClass:[NSArray class]]) return;

    NSMutableArray *mutableItems = [items mutableCopy];
    for (NSUInteger i = 0; i < mutableItems.count; i++) {
        NSDictionary *item = mutableItems[i];
        if ([item[@"fileId"] isEqualToString:fileId]) {
            NSMutableDictionary *updated = [item mutableCopy];
            updated[@"path"] = newPath;
            updated[@"parentId"] = newParentId;
            updated[@"parentPath"] = newParentPath;
            if (davUrl) updated[@"davUrl"] = davUrl;
            // Remove extensionCreated but add movedAt timestamp.
            // The merge logic preserves moved items briefly so the
            // enumerator can register them in prevFileIds. Without
            // this, server-side deletions of moved files are never
            // detected because the item was never in prevFileIds
            // for the new parent container.
            [updated removeObjectForKey:@"extensionCreated"];
            [updated removeObjectForKey:@"extensionCreatedAt"];
            updated[@"movedAt"] = @((int64_t)[[NSDate date] timeIntervalSince1970]);
            [mutableItems replaceObjectAtIndex:i withObject:updated];
            break;
        }
    }

    NSData *newData = [NSPropertyListSerialization dataWithPropertyList:mutableItems
                                                                format:NSPropertyListBinaryFormat_v1_0
                                                               options:0 error:nil];
    if (newData) {
        [newData writeToURL:metadataURL atomically:YES];
        os_log_info(extensionLog(), "Updated item %{public}@ path to %{public}@ in plist", fileId, newPath);
    }
}

/// Appends a newly created item to the shared fileprovider_items.plist so
/// subsequent enumerations include it. Without this, items created via
/// createItemBasedOnTemplate would disappear from Finder on the next
/// enumeration cycle because the enumerator only reports plist contents.
- (void)_appendItemToPlist:(NSDictionary *)itemDict {
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) return;

    NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
    NSMutableArray *items = nil;
    NSData *data = [NSData dataWithContentsOfURL:metadataURL];
    if (data) {
        NSArray *existing = [NSPropertyListSerialization propertyListWithData:data
                                                                     options:NSPropertyListImmutable
                                                                      format:nil error:nil];
        items = existing ? [existing mutableCopy] : [NSMutableArray array];
    } else {
        items = [NSMutableArray array];
    }

    // Mark the item as extension-created so syncMetadataToSharedContainer
    // in the main app preserves it until the sync engine discovers it.
    // Items without this flag are treated as journal-sourced and will be
    // removed when they disappear from the journal (e.g. server-side delete).
    NSMutableDictionary *markedItem = [itemDict mutableCopy];
    markedItem[@"extensionCreated"] = @YES;
    markedItem[@"extensionCreatedAt"] = @((int64_t)[[NSDate date] timeIntervalSince1970]);

    // Replace any existing entry with the same fileId to avoid duplicates.
    NSString *newFileId = markedItem[@"fileId"];
    NSUInteger existingIndex = NSNotFound;
    for (NSUInteger i = 0; i < items.count; i++) {
        if ([items[i][@"fileId"] isEqualToString:newFileId]) {
            existingIndex = i;
            break;
        }
    }
    if (existingIndex != NSNotFound) {
        [items replaceObjectAtIndex:existingIndex withObject:markedItem];
    } else {
        [items addObject:markedItem];
    }

    NSData *newData = [NSPropertyListSerialization dataWithPropertyList:items
                                                                format:NSPropertyListBinaryFormat_v1_0
                                                               options:0 error:nil];
    if (newData) {
        [newData writeToURL:metadataURL atomically:YES];
        os_log_info(extensionLog(), "Appended item %{public}@ to shared plist (total: %lu)",
                    newFileId, (unsigned long)items.count);
    }
}

#pragma mark - NSFileProviderReplicatedExtension (Create)

- (NSProgress *)createItemBasedOnTemplate:(id<NSFileProviderItem>)itemTemplate
                                   fields:(NSFileProviderItemFields)fields
                                 contents:(NSURL *)url
                                  options:(NSFileProviderCreateItemOptions)options
                                  request:(NSFileProviderRequest *)request
                        completionHandler:(void (^)(NSFileProviderItem _Nullable,
                                                    NSFileProviderItemFields,
                                                    BOOL,
                                                    NSError * _Nullable))completionHandler {
    os_log_info(extensionLog(), "createItem: %{public}@ parent=%{public}@",
                itemTemplate.filename, itemTemplate.parentItemIdentifier);

    NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:100];

    // When fileproviderd imports items from disk (e.g. after a DB reset), it calls
    // createItem for directories/files it found on FPFS. Look up the item in the
    // shared plist — if found, return it directly without needing XPC.
    {
        NSString *templateName = itemTemplate.filename;
        NSString *templateParent = itemTemplate.parentItemIdentifier;

        appendTrace([NSString stringWithFormat:@"[%@] createItem: name=%@ parent=%@ options=%lu url=%@\n",
            [NSDate date], templateName, templateParent, (unsigned long)options, url.path ?: @"(nil)"]);

        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (containerURL) {
            NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
            NSData *data = [NSData dataWithContentsOfURL:metadataURL];
            if (data) {
                NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                                          options:NSPropertyListImmutable
                                                                           format:nil
                                                                            error:nil];
                for (NSDictionary *dict in items) {
                    NSString *parentId = dict[@"parentId"] ?: NSFileProviderRootContainerItemIdentifier;
                    NSString *filename = dict[@"filename"] ?: dict[@"name"] ?: @"";
                    if ([filename isEqualToString:templateName]
                        && [parentId isEqualToString:templateParent]) {
                        FileProviderItem *item = [[FileProviderItem alloc] initWithDictionary:dict];
                        os_log_info(extensionLog(), "createItem: PLIST MATCH %{public}@ id=%{public}@",
                                    filename, item.itemIdentifier);
                        completionHandler(item, NSFileProviderItemFields(0), NO, nil);
                        return progress;
                    }
                }

                appendTrace([NSString stringWithFormat:
                    @"[%@] createItem: NO MATCH name=%@ parent=%@ plistCount=%lu options=%lu\n",
                    [NSDate date], templateName, templateParent, (unsigned long)items.count, (unsigned long)options]);

                // For reconciliation imports (MayAlreadyExist), the item is stale FPFS
                // data from a previous session that's no longer in our metadata.
                // Return NSFileProviderErrorNoSuchItem so fileproviderd removes it
                // from FPFS and continues reconciliation without a server-unreachable stall.
                if (options & NSFileProviderCreateItemMayAlreadyExist) {
                    completionHandler(nil, 0, NO,
                        [NSError errorWithDomain:NSFileProviderErrorDomain
                                           code:NSFileProviderErrorNoSuchItem
                                       userInfo:@{NSLocalizedDescriptionKey: @"Item not in local metadata"}]);
                    return progress;
                }
            }
        }
    }

    NSString *parentId = [itemTemplate.parentItemIdentifier copy];

    // --- Direct WebDAV upload (no XPC needed) ---
    {
        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (!containerURL) {
            completionHandler(nil, 0, NO, configUnavailableError(@"App-Container nicht verfügbar"));
            return progress;
        }

        // Read access token from global config.
        NSURL *configURL = [containerURL URLByAppendingPathComponent:configPlistName(self->_domain, containerURL)];
        NSData *configData = [NSData dataWithContentsOfURL:configURL];
        NSDictionary *config = configData
            ? [NSPropertyListSerialization propertyListWithData:configData options:NSPropertyListImmutable format:nil error:nil]
            : nil;
        NSString *accessToken = config[@"accessToken"];

        if (!accessToken || accessToken.length == 0) {
            completionHandler(nil, 0, NO, configUnavailableError(@"Anmeldung fehlt"));
            return progress;
        }

        // Resolve parent path and davUrl from items plist.
        // Each item carries its own davUrl so we use the correct space.
        NSString *parentPath = @"";
        NSString *davUrl = config[@"davUrl"]; // fallback to global config
        NSURL *metaURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
        NSData *metaData = [NSData dataWithContentsOfURL:metaURL];
        if (metaData) {
            NSArray *items = [NSPropertyListSerialization propertyListWithData:metaData
                                                                      options:NSPropertyListImmutable format:nil error:nil];
            for (NSDictionary *item in items) {
                if (![parentId isEqualToString:NSFileProviderRootContainerItemIdentifier]
                    && [item[@"fileId"] isEqualToString:parentId]) {
                    parentPath = item[@"path"] ?: @"";
                    if (item[@"davUrl"]) davUrl = item[@"davUrl"];
                    break;
                }
                // For root-level items, grab davUrl from any item in the plist.
                if ([parentId isEqualToString:NSFileProviderRootContainerItemIdentifier]
                    && item[@"davUrl"] && !davUrl) {
                    davUrl = item[@"davUrl"];
                }
            }
        }

        if (!davUrl || davUrl.length == 0) {
            completionHandler(nil, 0, NO, configUnavailableError(@"Server-URL nicht konfiguriert"));
            return progress;
        }

        NSString *filename = itemTemplate.filename;
        NSString *davBase = [davUrl hasSuffix:@"/"] ? [davUrl substringToIndex:davUrl.length - 1] : davUrl;

        if (url == nil) {
            // --- Directory creation via MKCOL ---
            NSString *dirPath = parentPath.length > 0
                ? [NSString stringWithFormat:@"%@/%@", parentPath, filename]
                : filename;
            NSString *encodedPath = [dirPath stringByAddingPercentEncodingWithAllowedCharacters:
                                     [NSCharacterSet URLPathAllowedCharacterSet]];
            NSString *mkcolURLString = [NSString stringWithFormat:@"%@/%@", davBase, encodedPath];
            NSURL *mkcolURL = [NSURL URLWithString:mkcolURLString];

            os_log_info(extensionLog(), "createItem: MKCOL %{public}@", mkcolURLString);

            NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:mkcolURL];
            req.HTTPMethod = @"MKCOL";
            [req setValue:[NSString stringWithFormat:@"Bearer %@", accessToken] forHTTPHeaderField:@"Authorization"];

            NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
            [[session dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                if (error) {
                    os_log_error(extensionLog(), "createItem: MKCOL failed: %{public}@", error.localizedDescription);
                    completionHandler(nil, 0, NO, error);
                    return;
                }
                NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
                if (http.statusCode < 200 || http.statusCode >= 300) {
                    os_log_error(extensionLog(), "createItem: MKCOL HTTP %ld", (long)http.statusCode);
                    completionHandler(nil, 0, NO, [NSError errorWithDomain:NSFileProviderErrorDomain
                        code:NSFileProviderErrorServerUnreachable
                        userInfo:@{NSLocalizedDescriptionKey:
                            [NSString stringWithFormat:@"Ordner konnte nicht erstellt werden (HTTP %ld)", (long)http.statusCode]}]);
                    return;
                }

                NSString *dirPath = parentPath.length > 0
                    ? [NSString stringWithFormat:@"%@/%@", parentPath, filename]
                    : filename;

                // Block that finalises the created-item and calls completionHandler.
                // Extracted so it can be called from both the PROPFIND path and the
                // direct fallback path below.
                void (^finish)(NSString *, NSString *, int64_t) =
                    ^(NSString *canonicalFileId, NSString *etag, int64_t modtime) {
                    NSDictionary *dirDict = @{
                        @"fileId": canonicalFileId,
                        @"filename": filename,
                        @"path": dirPath,
                        @"parentId": parentId,
                        @"parentPath": parentPath,
                        @"isDirectory": @YES,
                        @"size": @0,
                        @"modtime": @(modtime),
                        @"etag": etag ?: @"",
                        @"isVirtualFile": @NO,
                        @"isDownloaded": @YES,
                        @"davUrl": davUrl,
                    };
                    FileProviderItem *createdItem = [[FileProviderItem alloc] initWithDictionary:dirDict];
                    os_log_info(extensionLog(), "createItem: directory done id=%{public}@", canonicalFileId);
                    [self _appendItemToPlist:dirDict];
                    progress.completedUnitCount = 100;
                    completionHandler(createdItem, NSFileProviderItemFields(0), NO, nil);
                };

                // Do a PROPFIND (Depth: 0) on the newly created folder to retrieve
                // the canonical fileId (oc:id) that the sync engine will later store
                // in its journal. Using the MKCOL OC-FileId header alone is not
                // reliable — the header may use a different format (e.g. bare numeric)
                // than the journal (e.g. "spaceId!numeric"). A mismatch causes
                // NSFileProvider to see two items with the same name → "abc 2" duplicate.
                NSMutableURLRequest *pfReq = [NSMutableURLRequest requestWithURL:mkcolURL];
                pfReq.HTTPMethod = @"PROPFIND";
                [pfReq setValue:[NSString stringWithFormat:@"Bearer %@", accessToken]
                     forHTTPHeaderField:@"Authorization"];
                [pfReq setValue:@"0" forHTTPHeaderField:@"Depth"];
                [pfReq setValue:@"application/xml; charset=utf-8" forHTTPHeaderField:@"Content-Type"];
                pfReq.HTTPBody = [@"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                    "<d:propfind xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\">"
                    "<d:prop><oc:id/><d:getetag/><d:getlastmodified/></d:prop>"
                    "</d:propfind>" dataUsingEncoding:NSUTF8StringEncoding];

                NSURLSession *pfSession = [NSURLSession sessionWithConfiguration:
                    [NSURLSessionConfiguration defaultSessionConfiguration]];
                [[pfSession dataTaskWithRequest:pfReq
                             completionHandler:^(NSData *pfData, NSURLResponse *pfResp, NSError *pfErr) {
                    NSString *canonicalId = nil;
                    NSString *etag        = nil;
                    int64_t  modtime      = (int64_t)[[NSDate date] timeIntervalSince1970];

                    if (!pfErr && pfData) {
                        NSString *xml = [[NSString alloc] initWithData:pfData encoding:NSUTF8StringEncoding];
                        // Extract oc:id — the canonical fileId the sync journal uses.
                        NSRegularExpression *idRe = [NSRegularExpression
                            regularExpressionWithPattern:@"<[^:>]+:id[^>]*>([^<]+)</[^:>]+:id>"
                                                 options:0 error:nil];
                        NSTextCheckingResult *m = [idRe firstMatchInString:xml options:0
                                                                      range:NSMakeRange(0, xml.length)];
                        if (m && m.numberOfRanges > 1) {
                            canonicalId = [xml substringWithRange:[m rangeAtIndex:1]];
                        }
                        // Extract etag.
                        NSRegularExpression *etagRe = [NSRegularExpression
                            regularExpressionWithPattern:@"<[^:>]+:getetag[^>]*>\"?([^<\"]+)\"?</[^:>]+:getetag>"
                                                 options:0 error:nil];
                        NSTextCheckingResult *em = [etagRe firstMatchInString:xml options:0
                                                                         range:NSMakeRange(0, xml.length)];
                        if (em && em.numberOfRanges > 1) {
                            etag = [xml substringWithRange:[em rangeAtIndex:1]];
                        }
                    }

                    // Fall back to OC-FileId header if PROPFIND failed or returned no id.
                    if (!canonicalId || canonicalId.length == 0) {
                        canonicalId = [http.allHeaderFields[@"OC-FileId"] copy]
                            ?: [NSString stringWithFormat:@"ext!%@", [[NSUUID UUID] UUIDString]];
                        os_log_info(extensionLog(),
                            "createItem: PROPFIND id not found, using fallback: %{public}@", canonicalId);
                        appendTrace([NSString stringWithFormat:
                            @"[%@] createItem PROPFIND-FALLBACK name=%@ id=%@ pfErr=%@ pfStatus=%ld\n",
                            [NSDate date], filename, canonicalId, pfErr.localizedDescription ?: @"nil",
                            (long)((NSHTTPURLResponse *)pfResp).statusCode]);
                    } else {
                        os_log_info(extensionLog(),
                            "createItem: PROPFIND canonical id=%{public}@", canonicalId);
                        appendTrace([NSString stringWithFormat:
                            @"[%@] createItem PROPFIND-OK name=%@ id=%@\n",
                            [NSDate date], filename, canonicalId]);
                    }

                    finish(canonicalId, etag, modtime);
                }] resume];
            }] resume];

        } else {
            // --- File upload via PUT ---

            // Stage the content to a temporary file before the async upload.
            // The system-provided content URL may become invalid after this
            // method returns, so we must copy it synchronously.
            NSString *stagingDir = NSTemporaryDirectory();
            NSString *stagingFilename = [NSString stringWithFormat:@"upload-%@-%@",
                                         filename, [[NSUUID UUID] UUIDString]];
            NSURL *stagingURL = [NSURL fileURLWithPath:[stagingDir stringByAppendingPathComponent:stagingFilename]];

            NSError *copyError = nil;
            [[NSFileManager defaultManager] copyItemAtURL:url toURL:stagingURL error:&copyError];
            if (copyError) {
                os_log_error(extensionLog(), "createItem: failed to stage content: %{public}@",
                             copyError.localizedDescription);
                completionHandler(nil, 0, NO, copyError);
                return progress;
            }

            NSString *filePath = parentPath.length > 0
                ? [NSString stringWithFormat:@"%@/%@", parentPath, filename]
                : filename;
            NSString *encodedPath = [filePath stringByAddingPercentEncodingWithAllowedCharacters:
                                     [NSCharacterSet URLPathAllowedCharacterSet]];
            NSString *putURLString = [NSString stringWithFormat:@"%@/%@", davBase, encodedPath];
            NSURL *putURL = [NSURL URLWithString:putURLString];

            os_log_info(extensionLog(), "createItem: PUT %{public}@", putURLString);

            NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:putURL];
            req.HTTPMethod = @"PUT";
            [req setValue:[NSString stringWithFormat:@"Bearer %@", accessToken] forHTTPHeaderField:@"Authorization"];

            NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
            // Get file size before the async upload (staging file is deleted in the handler).
            int64_t stagedFileSize = 0;
            {
                NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:stagingURL.path error:nil];
                if (attrs) stagedFileSize = [attrs[NSFileSize] longLongValue];
            }

            [[session uploadTaskWithRequest:req fromFile:stagingURL completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                // Clean up staging file.
                [[NSFileManager defaultManager] removeItemAtURL:stagingURL error:nil];

                if (error) {
                    os_log_error(extensionLog(), "createItem: PUT failed: %{public}@", error.localizedDescription);
                    completionHandler(nil, 0, NO, error);
                    return;
                }
                NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
                if (http.statusCode < 200 || http.statusCode >= 300) {
                    os_log_error(extensionLog(), "createItem: PUT HTTP %ld", (long)http.statusCode);
                    completionHandler(nil, 0, NO, [NSError errorWithDomain:NSFileProviderErrorDomain
                        code:NSFileProviderErrorServerUnreachable
                        userInfo:@{NSLocalizedDescriptionKey:
                            [NSString stringWithFormat:@"Datei konnte nicht hochgeladen werden (HTTP %ld)", (long)http.statusCode]}]);
                    return;
                }

                NSString *putEtag = [http.allHeaderFields[@"ETag"] copy] ?: @"";
                int64_t fileSize = stagedFileSize;

                // Helper block to finish item creation with a confirmed fileId.
                void (^finishFile)(NSString *, NSString *, int64_t) =
                    ^(NSString *fileId, NSString *etag, int64_t modtime) {
                    NSMutableDictionary *itemDict = [@{
                        @"fileId": fileId,
                        @"filename": filename,
                        @"path": filePath,
                        @"parentId": parentId,
                        @"parentPath": parentPath,
                        @"isDirectory": @NO,
                        @"size": @(fileSize),
                        @"modtime": @(modtime),
                        @"etag": etag ?: @"",
                        @"isVirtualFile": @NO,
                        @"isDownloaded": @YES,
                        @"davUrl": davUrl,
                    } mutableCopy];
                    FileProviderItem *createdItem = [[FileProviderItem alloc] initWithDictionary:itemDict];
                    os_log_info(extensionLog(), "createItem: uploaded %{public}@ id=%{public}@", filename, fileId);
                    // Persist the new item in the shared plist so subsequent
                    // enumerations include it. Without this, the enumerator
                    // would not report the item, and fileproviderd would
                    // eventually remove it from Finder.
                    [self _appendItemToPlist:itemDict];
                    progress.completedUnitCount = 100;
                    completionHandler(createdItem, NSFileProviderItemFields(0), NO, nil);
                };

                // PROPFIND (Depth: 0) to get the canonical oc:id the sync journal will store.
                // PUT's OC-FileId header may use a different format (bare numeric) than
                // the journal (spaceId!UUID). A mismatch causes NSFileProvider to see two
                // items with the same name → file "reappears" in the source folder.
                NSMutableURLRequest *pfReq = [NSMutableURLRequest requestWithURL:putURL];
                pfReq.HTTPMethod = @"PROPFIND";
                [pfReq setValue:[NSString stringWithFormat:@"Bearer %@", accessToken]
                     forHTTPHeaderField:@"Authorization"];
                [pfReq setValue:@"0" forHTTPHeaderField:@"Depth"];
                [pfReq setValue:@"application/xml; charset=utf-8" forHTTPHeaderField:@"Content-Type"];
                pfReq.HTTPBody = [@"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                    "<d:propfind xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\">"
                    "<d:prop><oc:id/><d:getetag/><d:getlastmodified/></d:prop>"
                    "</d:propfind>" dataUsingEncoding:NSUTF8StringEncoding];

                NSURLSession *pfSession = [NSURLSession sessionWithConfiguration:
                    [NSURLSessionConfiguration defaultSessionConfiguration]];
                [[pfSession dataTaskWithRequest:pfReq
                             completionHandler:^(NSData *pfData, NSURLResponse *pfResp, NSError *pfErr) {
                    NSString *canonicalId = nil;
                    NSString *etag        = nil;
                    int64_t  modtime      = (int64_t)[[NSDate date] timeIntervalSince1970];

                    if (!pfErr && pfData) {
                        NSString *xml = [[NSString alloc] initWithData:pfData encoding:NSUTF8StringEncoding];
                        NSRegularExpression *idRe = [NSRegularExpression
                            regularExpressionWithPattern:@"<[^:>]+:id[^>]*>([^<]+)</[^:>]+:id>"
                                                 options:0 error:nil];
                        NSTextCheckingResult *m = [idRe firstMatchInString:xml options:0
                                                                      range:NSMakeRange(0, xml.length)];
                        if (m && m.numberOfRanges > 1) {
                            canonicalId = [xml substringWithRange:[m rangeAtIndex:1]];
                        }
                        NSRegularExpression *etagRe = [NSRegularExpression
                            regularExpressionWithPattern:@"<[^:>]+:getetag[^>]*>\"?([^<\"]+)\"?</[^:>]+:getetag>"
                                                 options:0 error:nil];
                        NSTextCheckingResult *em = [etagRe firstMatchInString:xml options:0
                                                                         range:NSMakeRange(0, xml.length)];
                        if (em && em.numberOfRanges > 1) {
                            etag = [xml substringWithRange:[em rangeAtIndex:1]];
                        }
                    }

                    if (!canonicalId || canonicalId.length == 0) {
                        canonicalId = [http.allHeaderFields[@"OC-FileId"] copy]
                            ?: [NSString stringWithFormat:@"ext!%@", [[NSUUID UUID] UUIDString]];
                        os_log_info(extensionLog(),
                            "createItem: PUT PROPFIND id not found, using fallback: %{public}@", canonicalId);
                        appendTrace([NSString stringWithFormat:
                            @"[%@] createItem PUT-PROPFIND-FALLBACK name=%@ id=%@ pfErr=%@ pfStatus=%ld\n",
                            [NSDate date], filename, canonicalId,
                            pfErr.localizedDescription ?: @"nil",
                            (long)((NSHTTPURLResponse *)pfResp).statusCode]);
                    } else {
                        os_log_info(extensionLog(),
                            "createItem: PUT PROPFIND canonical id=%{public}@", canonicalId);
                        appendTrace([NSString stringWithFormat:
                            @"[%@] createItem PUT-PROPFIND-OK name=%@ id=%@\n",
                            [NSDate date], filename, canonicalId]);
                    }

                    finishFile(canonicalId, etag ?: putEtag, modtime);
                }] resume];
            }] resume];
        }

        return progress;
    }

}

#pragma mark - NSFileProviderReplicatedExtension (Modify)

- (NSProgress *)modifyItem:(id<NSFileProviderItem>)item
               baseVersion:(NSFileProviderItemVersion *)version
              changedFields:(NSFileProviderItemFields)changedFields
                  contents:(NSURL *)newContents
                   options:(NSFileProviderModifyItemOptions)options
                   request:(NSFileProviderRequest *)request
         completionHandler:(void (^)(NSFileProviderItem _Nullable,
                                     NSFileProviderItemFields,
                                     BOOL,
                                     NSError * _Nullable))completionHandler {
    os_log_info(extensionLog(), "modifyItem: %{public}@ changedFields=0x%lx",
                item.filename, (unsigned long)changedFields);
    appendTrace([NSString stringWithFormat:@"[%@] modifyItem: name=%@ id=%@ changedFields=0x%lx contents=%@\n",
        [NSDate date], item.filename, item.itemIdentifier,
        (unsigned long)changedFields, newContents.path ?: @"(nil)"]);

    NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:100];

    NSString *fileId = [item.itemIdentifier copy];

    // Handle re-parent (move) via direct WebDAV MOVE — no XPC needed.
    // This must be checked BEFORE the XPC proxy check below, otherwise
    // moves fail when XPC is unavailable and Finder creates a copy instead.
    if (changedFields & NSFileProviderItemParentItemIdentifier) {
        NSString *newParentId = [item.parentItemIdentifier copy];
        os_log_info(extensionLog(), "modifyItem: moving %{public}@ to parent %{public}@", fileId, newParentId);

        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (!containerURL) {
            completionHandler(nil, 0, NO, configUnavailableError(@"App-Container nicht verfügbar"));
            return progress;
        }

        NSURL *cfgURL = [containerURL URLByAppendingPathComponent:configPlistName(self->_domain, containerURL)];
        NSData *cfgData = [NSData dataWithContentsOfURL:cfgURL];
        NSDictionary *cfg = cfgData ? [NSPropertyListSerialization propertyListWithData:cfgData
                                          options:NSPropertyListImmutable format:nil error:nil] : nil;
        NSString *movAccessToken = cfg[@"accessToken"];
        NSString *movDavUrl = cfg[@"davUrl"];

        // Look up source path and per-item davUrl.
        NSString *srcPath = nil;
        NSString *newParentPath = @"";
        NSURL *metURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
        NSData *metData = [NSData dataWithContentsOfURL:metURL];
        if (metData) {
            NSArray *allItems = [NSPropertyListSerialization propertyListWithData:metData
                                    options:NSPropertyListImmutable format:nil error:nil];
            for (NSDictionary *it in allItems) {
                if ([it[@"fileId"] isEqualToString:fileId]) {
                    srcPath = it[@"path"];
                    if (it[@"davUrl"]) movDavUrl = it[@"davUrl"];
                }
                if ([it[@"fileId"] isEqualToString:newParentId]) {
                    newParentPath = it[@"path"] ?: @"";
                }
            }
        }

        if (!srcPath || !movDavUrl || !movAccessToken) {
            completionHandler(nil, 0, NO, configUnavailableError(@"Verschieben nicht möglich — Konfiguration fehlt"));
            return progress;
        }

        NSString *filename = item.filename;
        NSString *destPath = newParentPath.length > 0
            ? [NSString stringWithFormat:@"%@/%@", newParentPath, filename] : filename;
        NSString *movDavBase = [movDavUrl hasSuffix:@"/"] ? [movDavUrl substringToIndex:movDavUrl.length - 1] : movDavUrl;

        NSString *srcEncoded = [srcPath stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet URLPathAllowedCharacterSet]];
        NSString *destEncoded = [destPath stringByAddingPercentEncodingWithAllowedCharacters:[NSCharacterSet URLPathAllowedCharacterSet]];
        NSString *srcURLString = [NSString stringWithFormat:@"%@/%@", movDavBase, srcEncoded];
        NSString *destURLString = [NSString stringWithFormat:@"%@/%@", movDavBase, destEncoded];

        os_log_info(extensionLog(), "modifyItem: MOVE %{public}@ -> %{public}@", srcURLString, destURLString);

        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:srcURLString]];
        req.HTTPMethod = @"MOVE";
        [req setValue:[NSString stringWithFormat:@"Bearer %@", movAccessToken] forHTTPHeaderField:@"Authorization"];
        [req setValue:destURLString forHTTPHeaderField:@"Destination"];
        [req setValue:@"F" forHTTPHeaderField:@"Overwrite"];

        NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
        [[session dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            if (error) {
                os_log_error(extensionLog(), "modifyItem: MOVE failed: %{public}@", error.localizedDescription);
                completionHandler(nil, 0, NO, error);
                return;
            }
            NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
            if (http.statusCode < 200 || http.statusCode >= 300) {
                os_log_error(extensionLog(), "modifyItem: MOVE HTTP %ld", (long)http.statusCode);
                completionHandler(nil, 0, NO, [NSError errorWithDomain:NSFileProviderErrorDomain
                    code:NSFileProviderErrorServerUnreachable
                    userInfo:@{NSLocalizedDescriptionKey:
                        [NSString stringWithFormat:@"Verschieben fehlgeschlagen (HTTP %ld)", (long)http.statusCode]}]);
                return;
            }

            // Update the plist entry in-place: change path/parent but do NOT
            // mark as extensionCreated. The file was already in the journal —
            // the sync engine will discover it at the new location. Without
            // extensionCreated, server-side deletions propagate correctly.
            [self _updateItemPathInPlist:fileId
                                newPath:destPath
                            newParentId:newParentId
                          newParentPath:newParentPath
                                 davUrl:movDavUrl];

            FileProviderItem *movedItem = [[FileProviderItem alloc]
                initWithIdentifier:fileId filename:filename parentIdentifier:newParentId
                isDirectory:NO size:[item.documentSize longLongValue] modDate:item.contentModificationDate];
            os_log_info(extensionLog(), "modifyItem: MOVE succeeded for %{public}@", fileId);
            progress.completedUnitCount = 100;
            completionHandler(movedItem, NSFileProviderItemFields(0), NO, nil);
        }] resume];
        return progress;
    }

    // Handle rename via direct WebDAV MOVE (same parent, new filename) — no XPC needed.
    // NsfpXpcDelegate does not implement renameItem:newName:completionHandler:, so
    // going through XPC would silently drop the operation and leave the server copy
    // with the original name ("Neuer Ordner"), causing the sync engine to re-create
    // the old name in Finder and producing a duplicate folder.
    if (changedFields & NSFileProviderItemFilename) {
        NSString *newName = [item.filename copy];
        os_log_info(extensionLog(), "modifyItem: renaming %{public}@ to '%{public}@'", fileId, newName);

        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (!containerURL) {
            completionHandler(nil, 0, NO, configUnavailableError(@"App-Container nicht verfügbar"));
            return progress;
        }

        NSURL *cfgURL = [containerURL URLByAppendingPathComponent:configPlistName(self->_domain, containerURL)];
        NSData *cfgData = [NSData dataWithContentsOfURL:cfgURL];
        NSDictionary *cfg = cfgData
            ? [NSPropertyListSerialization propertyListWithData:cfgData options:NSPropertyListImmutable format:nil error:nil]
            : nil;
        NSString *renAccessToken = cfg[@"accessToken"];
        NSString *renDavUrl = cfg[@"davUrl"];

        // Look up current path, parent path, isDirectory, and per-item davUrl from plist.
        NSString *srcPath = nil;
        NSString *parentPath = @"";
        NSString *currentParentId = [item.parentItemIdentifier copy];
        BOOL isDirectory = NO;
        NSURL *metURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
        NSData *metData = [NSData dataWithContentsOfURL:metURL];
        if (metData) {
            NSArray *allItems = [NSPropertyListSerialization propertyListWithData:metData
                                    options:NSPropertyListImmutable format:nil error:nil];
            for (NSDictionary *it in allItems) {
                if ([it[@"fileId"] isEqualToString:fileId]) {
                    srcPath = it[@"path"];
                    parentPath = it[@"parentPath"] ?: @"";
                    isDirectory = [it[@"isDirectory"] boolValue];
                    if (it[@"davUrl"]) renDavUrl = it[@"davUrl"];
                    break;
                }
            }
        }

        if (!srcPath || !renDavUrl || !renAccessToken) {
            completionHandler(nil, 0, NO, configUnavailableError(@"Umbenennen nicht möglich — Konfiguration fehlt"));
            return progress;
        }

        // Destination: same parent directory, new filename.
        NSString *destPath = parentPath.length > 0
            ? [NSString stringWithFormat:@"%@/%@", parentPath, newName] : newName;
        NSString *renDavBase = [renDavUrl hasSuffix:@"/"]
            ? [renDavUrl substringToIndex:renDavUrl.length - 1] : renDavUrl;
        NSString *srcEncoded  = [srcPath   stringByAddingPercentEncodingWithAllowedCharacters:
                                 [NSCharacterSet URLPathAllowedCharacterSet]];
        NSString *destEncoded = [destPath  stringByAddingPercentEncodingWithAllowedCharacters:
                                 [NSCharacterSet URLPathAllowedCharacterSet]];
        NSString *srcURLString  = [NSString stringWithFormat:@"%@/%@", renDavBase, srcEncoded];
        NSString *destURLString = [NSString stringWithFormat:@"%@/%@", renDavBase, destEncoded];

        os_log_info(extensionLog(), "modifyItem: MOVE (rename) %{public}@ -> %{public}@",
                    srcURLString, destURLString);

        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:srcURLString]];
        req.HTTPMethod = @"MOVE";
        [req setValue:[NSString stringWithFormat:@"Bearer %@", renAccessToken] forHTTPHeaderField:@"Authorization"];
        [req setValue:destURLString forHTTPHeaderField:@"Destination"];
        [req setValue:@"F" forHTTPHeaderField:@"Overwrite"];

        NSURLSession *session = [NSURLSession sessionWithConfiguration:
                                 [NSURLSessionConfiguration defaultSessionConfiguration]];
        BOOL capturedIsDirectory = isDirectory;
        [[session dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            if (error) {
                os_log_error(extensionLog(), "modifyItem: rename MOVE failed: %{public}@",
                             error.localizedDescription);
                completionHandler(nil, 0, NO, error);
                return;
            }
            NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
            if (http.statusCode < 200 || http.statusCode >= 300) {
                os_log_error(extensionLog(), "modifyItem: rename MOVE HTTP %ld", (long)http.statusCode);
                completionHandler(nil, 0, NO, [NSError errorWithDomain:NSFileProviderErrorDomain
                    code:NSFileProviderErrorServerUnreachable
                    userInfo:@{NSLocalizedDescriptionKey:
                        [NSString stringWithFormat:@"Umbenennen fehlgeschlagen (HTTP %ld)",
                         (long)http.statusCode]}]);
                return;
            }

            // Update the plist entry with the new name/path so the enumerator
            // immediately reflects the rename and the sync engine does not
            // re-create the old name as a duplicate in Finder.
            [self _updateItemPathInPlist:fileId
                                newPath:destPath
                            newParentId:currentParentId
                          newParentPath:parentPath
                                 davUrl:renDavUrl];

            FileProviderItem *renamedItem = [[FileProviderItem alloc]
                initWithIdentifier:fileId
                          filename:newName
                  parentIdentifier:currentParentId
                       isDirectory:capturedIsDirectory
                              size:[item.documentSize longLongValue]
                           modDate:item.contentModificationDate];
            os_log_info(extensionLog(), "modifyItem: rename succeeded for %{public}@ -> '%{public}@'",
                        fileId, newName);
            progress.completedUnitCount = 100;
            completionHandler(renamedItem, NSFileProviderItemFields(0), NO, nil);
        }] resume];
        return progress;
    }

    // Handle content update (re-upload via XPC).
    id<OpenCloudXPCServiceProtocol> proxy = _xpcService.remoteObjectProxy;
    if (changedFields & NSFileProviderItemContents) {
        if (!proxy) {
            os_log_error(extensionLog(), "modifyItem: no XPC proxy for content update of %{public}@", fileId);
            completionHandler(nil, 0, NO, xpcUnavailableError());
            return progress;
        }
        if (!newContents) {
            os_log_error(extensionLog(), "modifyItem: content change flagged but no content URL for %{public}@",
                         fileId);
            completionHandler(nil, 0, NO, [NSError errorWithDomain:NSFileProviderErrorDomain
                                                               code:NSFileProviderErrorNoSuchItem
                                                           userInfo:@{NSLocalizedDescriptionKey: @"Content URL missing for content update"}]);
            return progress;
        }

        NSString *parentId = [item.parentItemIdentifier copy];

        // Stage the content for upload.
        NSString *stagingDir = NSTemporaryDirectory();
        NSString *stagingFilename = [NSString stringWithFormat:@"reupload-%@-%@",
                                     fileId, [[NSUUID UUID] UUIDString]];
        NSURL *stagingURL = [NSURL fileURLWithPath:[stagingDir stringByAppendingPathComponent:stagingFilename]];

        NSError *copyError = nil;
        [[NSFileManager defaultManager] copyItemAtURL:newContents toURL:stagingURL error:&copyError];
        if (copyError) {
            os_log_error(extensionLog(), "modifyItem: failed to stage content: %{public}@",
                         copyError.localizedDescription);
            completionHandler(nil, 0, NO, copyError);
            return progress;
        }

        os_log_info(extensionLog(), "modifyItem: re-uploading content for %{public}@", fileId);

        [proxy scheduleUpload:stagingURL parentIdentifier:parentId completionHandler:^(NSString *serverFileId, NSError *error) {
            [[NSFileManager defaultManager] removeItemAtURL:stagingURL error:nil];

            if (error) {
                os_log_error(extensionLog(), "modifyItem: re-upload failed: %{public}@",
                             error.localizedDescription);
                completionHandler(nil, 0, NO, error);
                return;
            }

            os_log_info(extensionLog(), "modifyItem: re-upload succeeded for %{public}@", fileId);

            FileProviderItem *updatedItem = [[FileProviderItem alloc]
                initWithIdentifier:serverFileId ?: fileId
                          filename:item.filename
                  parentIdentifier:parentId
                       isDirectory:NO
                              size:[item.documentSize longLongValue]
                           modDate:[NSDate date]];
            progress.completedUnitCount = 100;
            completionHandler(updatedItem, NSFileProviderItemFields(0), NO, nil);
        }];
        return progress;
    }

    // No recognized field changes — return the item unchanged.
    os_log_info(extensionLog(), "modifyItem: no actionable field changes for %{public}@", fileId);
    FileProviderItem *unchangedItem = [[FileProviderItem alloc]
        initWithIdentifier:fileId
                  filename:item.filename
          parentIdentifier:item.parentItemIdentifier
               isDirectory:NO
                      size:[item.documentSize longLongValue]
                   modDate:item.contentModificationDate];
    completionHandler(unchangedItem, NSFileProviderItemFields(0), NO, nil);
    return progress;
}

#pragma mark - NSFileProviderReplicatedExtension (Delete)

- (NSProgress *)deleteItemWithIdentifier:(NSFileProviderItemIdentifier)identifier
                             baseVersion:(NSFileProviderItemVersion *)version
                                 options:(NSFileProviderDeleteItemOptions)options
                                 request:(NSFileProviderRequest *)request
                       completionHandler:(void (^)(NSError * _Nullable))completionHandler {
    os_log_info(extensionLog(), "deleteItem: %{public}@", identifier);

    NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:1];
    NSString *fileId = [identifier copy];

    // --- Direct WebDAV DELETE (no XPC needed) ---
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) {
        completionHandler(configUnavailableError(@"App-Container nicht verfügbar"));
        return progress;
    }

    // Read access token from global config.
    NSURL *configURL = [containerURL URLByAppendingPathComponent:configPlistName(self->_domain, containerURL)];
    NSData *configData = [NSData dataWithContentsOfURL:configURL];
    NSDictionary *config = configData
        ? [NSPropertyListSerialization propertyListWithData:configData options:NSPropertyListImmutable format:nil error:nil]
        : nil;
    NSString *davUrl = config[@"davUrl"]; // fallback
    NSString *accessToken = config[@"accessToken"];

    if (!accessToken || accessToken.length == 0) {
        completionHandler(configUnavailableError(@"Anmeldung fehlt"));
        return progress;
    }

    // Look up file path and per-item davUrl from items plist.
    NSString *filePath = nil;
    NSURL *metaURL = [containerURL URLByAppendingPathComponent:itemsPlistName(self->_domain, containerURL)];
    NSData *metaData = [NSData dataWithContentsOfURL:metaURL];
    if (metaData) {
        NSArray *items = [NSPropertyListSerialization propertyListWithData:metaData
                                                                  options:NSPropertyListImmutable format:nil error:nil];
        for (NSDictionary *item in items) {
            if ([item[@"fileId"] isEqualToString:fileId]) {
                filePath = item[@"path"];
                if (item[@"davUrl"]) davUrl = item[@"davUrl"];
                break;
            }
        }
    }

    if (!davUrl || davUrl.length == 0) {
        completionHandler(configUnavailableError(@"Server-URL nicht konfiguriert"));
        return progress;
    }

    if (!filePath || filePath.length == 0) {
        os_log_error(extensionLog(), "deleteItem: fileId %{public}@ not found in items plist", fileId);
        completionHandler([NSError errorWithDomain:NSFileProviderErrorDomain
                                              code:NSFileProviderErrorNoSuchItem
                                          userInfo:@{NSLocalizedDescriptionKey: @"Item not found in metadata"}]);
        return progress;
    }

    NSString *davBase = [davUrl hasSuffix:@"/"] ? [davUrl substringToIndex:davUrl.length - 1] : davUrl;
    NSString *encodedPath = [filePath stringByAddingPercentEncodingWithAllowedCharacters:
                             [NSCharacterSet URLPathAllowedCharacterSet]];
    NSString *deleteURLString = [NSString stringWithFormat:@"%@/%@", davBase, encodedPath];
    NSURL *deleteURL = [NSURL URLWithString:deleteURLString];

    os_log_info(extensionLog(), "deleteItem: DELETE %{public}@", deleteURLString);

    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:deleteURL];
    req.HTTPMethod = @"DELETE";
    [req setValue:[NSString stringWithFormat:@"Bearer %@", accessToken] forHTTPHeaderField:@"Authorization"];

    NSURLSession *session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
    [[session dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        if (error) {
            os_log_error(extensionLog(), "deleteItem: DELETE failed: %{public}@", error.localizedDescription);
            completionHandler(error);
            return;
        }
        NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
        if (http.statusCode >= 200 && http.statusCode < 300) {
            os_log_info(extensionLog(), "deleteItem: DELETE succeeded for %{public}@ (HTTP %ld)",
                        fileId, (long)http.statusCode);
            // Remove the item from the shared plist so the enumerator
            // no longer returns it.
            [self _removeStaleItemFromPlist:fileId];
            progress.completedUnitCount = 1;
            completionHandler(nil);
        } else {
            os_log_error(extensionLog(), "deleteItem: DELETE HTTP %ld for %{public}@",
                         (long)http.statusCode, fileId);
            completionHandler([NSError errorWithDomain:NSFileProviderErrorDomain
                code:NSFileProviderErrorServerUnreachable
                userInfo:@{NSLocalizedDescriptionKey:
                    [NSString stringWithFormat:@"Löschen fehlgeschlagen (HTTP %ld)", (long)http.statusCode]}]);
        }
    }] resume];

    return progress;
}

#pragma mark - NSFileProviderEnumerating

- (id<NSFileProviderEnumerator>)enumeratorForContainerItemIdentifier:(NSFileProviderItemIdentifier)containerItemIdentifier
                                                             request:(NSFileProviderRequest *)request
                                                               error:(NSError *__autoreleasing *)error {
    os_log_info(extensionLog(), "enumeratorForContainerItemIdentifier: %{public}@", containerItemIdentifier);

    // The root container, folder identifiers, and the working set all use the
    // same enumerator class. The enumerator fetches items via XPC for the given container.
    if ([containerItemIdentifier isEqualToString:NSFileProviderRootContainerItemIdentifier]
        || [containerItemIdentifier isEqualToString:NSFileProviderWorkingSetContainerItemIdentifier]
        || containerItemIdentifier.length > 0) {

        FileProviderEnumerator *enumerator =
            [[FileProviderEnumerator alloc] initWithContainerIdentifier:containerItemIdentifier
                                                                domain:_domain
                                                                 cache:_itemCache];
        return enumerator;
    }

    os_log_error(extensionLog(), "enumeratorForContainerItemIdentifier: unsupported container %{public}@",
                 containerItemIdentifier);
    if (error) {
        *error = [NSError errorWithDomain:NSFileProviderErrorDomain
                                     code:NSFileProviderErrorNoSuchItem
                                 userInfo:@{NSLocalizedDescriptionKey: @"Unsupported container identifier"}];
    }
    return nil;
}

#pragma mark - NSFileProviderThumbnailing

- (NSProgress *)fetchThumbnailsForItemIdentifiers:(NSArray<NSFileProviderItemIdentifier> *)itemIdentifiers
                                    requestedSize:(CGSize)size
                      perThumbnailCompletionHandler:(void (^)(NSFileProviderItemIdentifier,
                                                              NSData * _Nullable,
                                                              NSError * _Nullable))perThumbnailHandler
                                completionHandler:(void (^)(NSError * _Nullable))completionHandler {
    os_log_info(extensionLog(), "fetchThumbnails: requested for %lu items at %.0fx%.0f",
                (unsigned long)itemIdentifiers.count, size.width, size.height);

    NSProgress *progress = [NSProgress progressWithTotalUnitCount:(int64_t)itemIdentifiers.count];

    dispatch_group_t group = dispatch_group_create();

    for (NSFileProviderItemIdentifier identifier in itemIdentifiers) {
        dispatch_group_enter(group);

        [_thumbnails fetchThumbnail:identifier size:size completionHandler:^(NSData *imageData, NSError *error) {
            perThumbnailHandler(identifier, imageData, error);
            progress.completedUnitCount += 1;
            dispatch_group_leave(group);
        }];
    }

    dispatch_group_notify(group, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        os_log_info(extensionLog(), "fetchThumbnails: completed for %lu items",
                    (unsigned long)itemIdentifiers.count);
        completionHandler(nil);
    });

    return progress;
}

@end
