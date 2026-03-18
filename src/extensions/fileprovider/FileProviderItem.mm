// FileProviderItem -- NSFileProviderItem adapter implementation.
// Maps sync journal metadata to the NSFileProviderItem protocol for Finder integration.

#import "FileProviderItem.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <os/log.h>

static os_log_t itemLog(void) {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("eu.opencloud.desktop.fileprovider", "item");
    });
    return log;
}

#pragma mark - UTI Helper

/// Derives a UTI from a filename extension. Returns "public.data" as fallback for files,
/// "public.folder" for directories.
static NSString *utiForFilename(NSString *filename, BOOL isDirectory) {
    if (isDirectory) {
        return UTTypeFolder.identifier;
    }

    NSString *extension = filename.pathExtension;
    if (extension.length > 0) {
        UTType *type = [UTType typeWithFilenameExtension:extension];
        if (type != nil) {
            return type.identifier;
        }
    }

    return UTTypeData.identifier;
}

#pragma mark - FileProviderItem

API_AVAILABLE(macos(12.0))
@implementation FileProviderItem {
    NSFileProviderItemIdentifier _itemIdentifier;
    NSFileProviderItemIdentifier _parentItemIdentifier;
    NSString *_filename;
    NSString *_typeIdentifier;
    BOOL _isDirectory;
    NSNumber *_documentSize;
    NSDate *_contentModificationDate;
    NSDate *_creationDate;
    BOOL _isUploaded;
    BOOL _isDownloaded;
    BOOL _isDownloading;
    BOOL _isUploading;
    NSNumber *_childItemCount;
    NSFileProviderItemVersion *_itemVersion;
}

#pragma mark - Initializers

- (instancetype)initWithIdentifier:(NSString *)fileId
                          filename:(NSString *)name
                  parentIdentifier:(NSFileProviderItemIdentifier)parentId
                       isDirectory:(BOOL)isDir
                              size:(int64_t)size
                           modDate:(nullable NSDate *)date {
    self = [super init];
    if (self) {
        _itemIdentifier = [fileId copy];
        _parentItemIdentifier = [parentId copy];
        _filename = [name copy];
        _isDirectory = isDir;
        _typeIdentifier = utiForFilename(name, isDir);
        _documentSize = isDir ? nil : @(size);
        _contentModificationDate = date;
        _creationDate = date;
        _isUploaded = YES;
        _isDownloaded = NO;
        _isDownloading = NO;
        _isUploading = NO;
        _childItemCount = nil;

        // Build itemVersion from modification date (or a static seed if no date).
        // NSFileProviderItemVersion is required for replicated extensions.
        NSData *versionData;
        if (date) {
            int64_t epoch = (int64_t)[date timeIntervalSince1970];
            versionData = [NSData dataWithBytes:&epoch length:sizeof(epoch)];
        } else {
            uint64_t seed = 1;
            versionData = [NSData dataWithBytes:&seed length:sizeof(seed)];
        }
        _itemVersion = [[NSFileProviderItemVersion alloc] initWithContentVersion:versionData
                                                                 metadataVersion:versionData];

        os_log_debug(itemLog(), "Created FileProviderItem id=%{public}@ name=%{public}@ dir=%d",
                     fileId, name, isDir);
    }
    return self;
}

- (instancetype)initWithDictionary:(NSDictionary *)dict {
    NSString *fileId = dict[@"fileId"] ?: @"";
    // Accept both "filename" (old XPC format) and "name" (shared plist format).
    NSString *filename = dict[@"filename"] ?: dict[@"name"] ?: @"";
    NSString *parentId = dict[@"parentId"] ?: NSFileProviderRootContainerItemIdentifier;
    BOOL isDirectory = [dict[@"isDirectory"] boolValue];
    int64_t size = [dict[@"size"] longLongValue];

    // Accept both NSDate "modDate" (old XPC format) and NSNumber "modtime" (shared plist, seconds since epoch).
    NSDate *modDate = dict[@"modDate"];
    if (!modDate && dict[@"modtime"]) {
        NSTimeInterval seconds = [dict[@"modtime"] doubleValue];
        if (seconds > 0) {
            modDate = [NSDate dateWithTimeIntervalSince1970:seconds];
        }
    }

    self = [self initWithIdentifier:fileId
                           filename:filename
                   parentIdentifier:parentId
                        isDirectory:isDirectory
                               size:size
                            modDate:modDate];
    if (self) {
        // Override transfer state from dictionary if present
        if (dict[@"isUploaded"] != nil) {
            _isUploaded = [dict[@"isUploaded"] boolValue];
        }
        if (dict[@"isDownloaded"] != nil) {
            _isDownloaded = [dict[@"isDownloaded"] boolValue];
        }
        if (dict[@"isDownloading"] != nil) {
            _isDownloading = [dict[@"isDownloading"] boolValue];
        }
        if (dict[@"isUploading"] != nil) {
            _isUploading = [dict[@"isUploading"] boolValue];
        }
        if (dict[@"childItemCount"] != nil) {
            _childItemCount = dict[@"childItemCount"];
        }
    }
    return self;
}

+ (instancetype)rootContainerItem {
    FileProviderItem *root = [[FileProviderItem alloc]
        initWithIdentifier:NSFileProviderRootContainerItemIdentifier
                  filename:@"OpenCloud"
          parentIdentifier:NSFileProviderRootContainerItemIdentifier
               isDirectory:YES
                      size:0
                   modDate:nil];
    return root;
}

#pragma mark - NSFileProviderItem Properties

- (NSFileProviderItemIdentifier)itemIdentifier {
    return _itemIdentifier;
}

- (NSFileProviderItemIdentifier)parentItemIdentifier {
    return _parentItemIdentifier;
}

- (NSString *)filename {
    return _filename;
}

- (NSString *)typeIdentifier {
    return _typeIdentifier;
}

- (NSFileProviderItemCapabilities)capabilities {
    if (_isDirectory) {
        return NSFileProviderItemCapabilitiesAllowsAll;
    }

    return NSFileProviderItemCapabilitiesAllowsReading
         | NSFileProviderItemCapabilitiesAllowsWriting
         | NSFileProviderItemCapabilitiesAllowsRenaming
         | NSFileProviderItemCapabilitiesAllowsDeleting
         | NSFileProviderItemCapabilitiesAllowsEvicting;
}

- (NSNumber *)documentSize {
    return _documentSize;
}

- (NSDate *)contentModificationDate {
    return _contentModificationDate;
}

- (NSDate *)creationDate {
    return _creationDate;
}

- (BOOL)isUploaded {
    return _isUploaded;
}

- (BOOL)isDownloaded {
    return _isDownloaded;
}

- (BOOL)isDownloading {
    return _isDownloading;
}

- (BOOL)isUploading {
    return _isUploading;
}

- (NSNumber *)childItemCount {
    return _childItemCount;
}

- (NSFileProviderItemVersion *)itemVersion {
    return _itemVersion;
}

- (UTType *)contentType {
    if (_isDirectory) {
        return UTTypeFolder;
    }

    NSString *extension = _filename.pathExtension;
    if (extension.length > 0) {
        UTType *type = [UTType typeWithFilenameExtension:extension];
        if (type != nil) {
            return type;
        }
    }

    return UTTypeData;
}

@end
