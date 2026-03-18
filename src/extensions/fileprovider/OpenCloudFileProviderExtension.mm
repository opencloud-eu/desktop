// OpenCloudFileProviderExtension -- NSFileProviderReplicatedExtension implementation.
// Runs as a separate process managed by the macOS File Provider framework.

#import "OpenCloudFileProviderExtension.h"

#import "FileProviderEnumerator.h"
#import "FileProviderItem.h"
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
}

#pragma mark - Lifecycle

- (instancetype)initWithDomain:(NSFileProviderDomain *)domain {
    self = [super init];
    if (self) {
        _domain = domain;
        // Multiple trace mechanisms to diagnose
        NSLog(@">>> EXTENSION INIT domain=%@", domain.identifier);
        os_log_fault(extensionLog(), ">>> EXTENSION INIT domain=%{public}@", domain.identifier);

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

    // Look up the item from the shared metadata plist in the App Group container.
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (containerURL) {
        NSURL *metadataURL = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
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
        NSURL *configURL = [containerURL URLByAppendingPathComponent:@"fileprovider_config.plist"];
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
        NSString *davUrl = config[@"davUrl"];
        NSString *accessToken = config[@"accessToken"];
        if (!davUrl || davUrl.length == 0) {
            os_log_error(extensionLog(), "fetchContents: no davUrl in config");
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:configUnavailableError(@"Server-URL nicht konfiguriert")];
            return;
        }
        if (!accessToken || accessToken.length == 0) {
            os_log_error(extensionLog(), "fetchContents: no access token — app may still be starting");
            // Use a transient error so fileproviderd retries later instead of
            // removing the item from Finder (which NotAuthenticated would do).
            [self _completeHydrationForFileId:fileId url:nil item:nil
                error:configUnavailableError(@"Anmeldung wird vorbereitet — bitte kurz warten")];
            return;
        }

        // Look up the file path from the items plist.
        NSURL *metadataURL = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
        NSData *metaData = [NSData dataWithContentsOfURL:metadataURL];
        NSString *filePath = nil;
        NSDictionary *itemDict = nil;
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
                    fpCode = NSFileProviderErrorNoSuchItem;
                    userMessage = NSLocalizedString(
                        @"Diese Datei wurde auf dem Server nicht gefunden. Sie wurde möglicherweise gelöscht oder verschoben.",
                        @"FileProvider HTTP 404");
                    [self _removeStaleItemFromPlist:fileId];
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

    NSURL *metadataURL = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
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

        appendTrace([NSString stringWithFormat:@"[%@] createItem: name=%@ parent=%@ options=%lu\n",
            [NSDate date], templateName, templateParent, (unsigned long)options]);

        NSURL *containerURL = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (containerURL) {
            NSURL *metadataURL = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
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
                        os_log_fault(extensionLog(), "createItem: PLIST MATCH %{public}@ id=%{public}@",
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

        // Read config
        NSURL *configURL = [containerURL URLByAppendingPathComponent:@"fileprovider_config.plist"];
        NSData *configData = [NSData dataWithContentsOfURL:configURL];
        NSDictionary *config = configData
            ? [NSPropertyListSerialization propertyListWithData:configData options:NSPropertyListImmutable format:nil error:nil]
            : nil;
        NSString *davUrl = config[@"davUrl"];
        NSString *accessToken = config[@"accessToken"];

        if (!davUrl || davUrl.length == 0 || !accessToken || accessToken.length == 0) {
            completionHandler(nil, 0, NO, configUnavailableError(@"Server-Konfiguration oder Anmeldung fehlt"));
            return progress;
        }

        // Resolve parent path from items plist
        NSString *parentPath = @"";
        if (![parentId isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
            NSURL *metaURL = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
            NSData *metaData = [NSData dataWithContentsOfURL:metaURL];
            if (metaData) {
                NSArray *items = [NSPropertyListSerialization propertyListWithData:metaData
                                                                          options:NSPropertyListImmutable format:nil error:nil];
                for (NSDictionary *item in items) {
                    if ([item[@"fileId"] isEqualToString:parentId]) {
                        parentPath = item[@"path"] ?: @"";
                        break;
                    }
                }
            }
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

                NSString *newFileId = [http.allHeaderFields[@"OC-FileId"] copy]
                    ?: [NSString stringWithFormat:@"%@!%@", parentId, [[NSUUID UUID] UUIDString]];

                FileProviderItem *createdItem = [[FileProviderItem alloc]
                    initWithIdentifier:newFileId filename:filename parentIdentifier:parentId
                    isDirectory:YES size:0 modDate:[NSDate date]];
                os_log_info(extensionLog(), "createItem: directory created id=%{public}@", newFileId);
                progress.completedUnitCount = 100;
                completionHandler(createdItem, NSFileProviderItemFields(0), NO, nil);
            }] resume];

        } else {
            // --- File upload via PUT ---
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
            [[session uploadTaskWithRequest:req fromFile:url completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
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

                NSString *newFileId = [http.allHeaderFields[@"OC-FileId"] copy]
                    ?: [NSString stringWithFormat:@"%@!%@", parentId, [[NSUUID UUID] UUIDString]];
                NSDictionary *sizeHeader = http.allHeaderFields;
                int64_t fileSize = [sizeHeader[@"Content-Length"] longLongValue];
                if (fileSize == 0) {
                    // Get size from the uploaded file
                    NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:url.path error:nil];
                    fileSize = [attrs[NSFileSize] longLongValue];
                }

                NSMutableDictionary *itemDict = [@{
                    @"fileId": newFileId,
                    @"filename": filename,
                    @"path": filePath,
                    @"parentId": parentId,
                    @"parentPath": parentPath,
                    @"isDirectory": @NO,
                    @"size": @(fileSize),
                    @"modtime": @((int64_t)[[NSDate date] timeIntervalSince1970]),
                    @"etag": [http.allHeaderFields[@"ETag"] copy] ?: @"",
                    @"isVirtualFile": @NO,
                    @"isDownloaded": @YES,
                } mutableCopy];

                FileProviderItem *createdItem = [[FileProviderItem alloc] initWithDictionary:itemDict];
                os_log_info(extensionLog(), "createItem: uploaded %{public}@ id=%{public}@", filename, newFileId);
                progress.completedUnitCount = 100;
                completionHandler(createdItem, NSFileProviderItemFields(0), NO, nil);
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

    NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:100];

    NSString *fileId = [item.itemIdentifier copy];

    // Check which fields actually require XPC communication with the main app.
    const NSFileProviderItemFields criticalFields =
        NSFileProviderItemFilename |
        NSFileProviderItemParentItemIdentifier |
        NSFileProviderItemContents;

    id<OpenCloudXPCServiceProtocol> proxy = _xpcService.remoteObjectProxy;
    if (!proxy) {
        if (changedFields & criticalFields) {
            // Rename, move, or content upload requires XPC — report error.
            os_log_error(extensionLog(), "modifyItem: no XPC proxy for critical change 0x%lx on %{public}@",
                         (unsigned long)changedFields, fileId);
            completionHandler(nil, 0, NO, xpcUnavailableError());
            return progress;
        }
        // Non-critical fields (e.g. lastUsedDate, contentPolicy) — return item
        // unchanged so fileproviderd does not mark it as errored.
        os_log_info(extensionLog(), "modifyItem: no XPC needed for fields 0x%lx on %{public}@",
                    (unsigned long)changedFields, fileId);
        FileProviderItem *unchanged = [[FileProviderItem alloc]
            initWithIdentifier:fileId
                      filename:item.filename
              parentIdentifier:item.parentItemIdentifier
                   isDirectory:NO
                          size:[item.documentSize longLongValue]
                       modDate:item.contentModificationDate];
        progress.completedUnitCount = 100;
        completionHandler(unchanged, 0, NO, nil);
        return progress;
    }

    // Handle rename.
    if (changedFields & NSFileProviderItemFilename) {
        NSString *newName = [item.filename copy];
        os_log_info(extensionLog(), "modifyItem: renaming %{public}@ to '%{public}@'", fileId, newName);

        [proxy renameItem:fileId newName:newName completionHandler:^(NSDictionary *itemDict, NSError *error) {
            if (error) {
                os_log_error(extensionLog(), "modifyItem: rename failed: %{public}@",
                             error.localizedDescription);
                completionHandler(nil, 0, NO, error);
                return;
            }

            FileProviderItem *updatedItem = [[FileProviderItem alloc] initWithDictionary:itemDict];
            os_log_info(extensionLog(), "modifyItem: rename succeeded for %{public}@", fileId);
            progress.completedUnitCount = 100;
            completionHandler(updatedItem, NSFileProviderItemFields(0), NO, nil);
        }];
        return progress;
    }

    // Handle re-parent (move).
    if (changedFields & NSFileProviderItemParentItemIdentifier) {
        NSString *newParentId = [item.parentItemIdentifier copy];
        os_log_info(extensionLog(), "modifyItem: moving %{public}@ to parent %{public}@", fileId, newParentId);

        [proxy moveItem:fileId newParent:newParentId completionHandler:^(NSDictionary *itemDict, NSError *error) {
            if (error) {
                os_log_error(extensionLog(), "modifyItem: move failed: %{public}@",
                             error.localizedDescription);
                completionHandler(nil, 0, NO, error);
                return;
            }

            FileProviderItem *updatedItem = [[FileProviderItem alloc] initWithDictionary:itemDict];
            os_log_info(extensionLog(), "modifyItem: move succeeded for %{public}@", fileId);
            progress.completedUnitCount = 100;
            completionHandler(updatedItem, NSFileProviderItemFields(0), NO, nil);
        }];
        return progress;
    }

    // Handle content update (re-upload).
    if (changedFields & NSFileProviderItemContents) {
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

    id<OpenCloudXPCServiceProtocol> proxy = _xpcService.remoteObjectProxy;
    if (!proxy) {
        os_log_error(extensionLog(), "deleteItem: no XPC proxy available");
        completionHandler(xpcUnavailableError());
        return progress;
    }

    NSString *fileId = [identifier copy];

    [proxy deleteItem:fileId completionHandler:^(NSError *error) {
        if (error) {
            os_log_error(extensionLog(), "deleteItem: failed for %{public}@: %{public}@",
                         fileId, error.localizedDescription);
            completionHandler(error);
            return;
        }

        os_log_info(extensionLog(), "deleteItem: succeeded for %{public}@", fileId);
        progress.completedUnitCount = 1;
        completionHandler(nil);
    }];

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
                                                            xpcService:_xpcService];
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
