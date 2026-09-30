// FileProviderWebDAV -- WebDAV PROPFIND multistatus parser for the macOS
// File Provider extension. Self-contained: depends only on Foundation so it
// can be unit-tested standalone (see tests/test_fileprovider_webdav.mm).
#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// One entry parsed from a WebDAV PROPFIND multistatus response.
@interface FileProviderWebDAVEntry : NSObject

/// Server file id (oc:fileid / oc:id). Empty string if absent.
@property (nonatomic, copy) NSString *fileId;
/// Path relative to the space root, percent-decoded, no leading/trailing slash.
/// The space root itself yields @"" (empty string).
@property (nonatomic, copy) NSString *relativePath;
/// Last path component (display name). Empty for the space root.
@property (nonatomic, copy) NSString *name;
/// YES if <d:resourcetype> contains <d:collection/>.
@property (nonatomic) BOOL isDirectory;
/// File size in bytes (0 for directories).
@property (nonatomic) int64_t size;
/// Last-modified time as seconds since 1970 (0 if absent/unparseable).
@property (nonatomic) int64_t modtime;
/// ETag with surrounding quotes stripped. Empty string if absent.
@property (nonatomic, copy) NSString *etag;
/// oc:permissions string (e.g. "RDNVWZP"). Empty string if absent.
@property (nonatomic, copy) NSString *permissions;

@end

@interface FileProviderWebDAV : NSObject

/// Parses a PROPFIND multistatus XML body into entries.
///
/// @param xml        Raw multistatus XML data.
/// @param hrefPrefix The path portion of the space DAV URL (e.g.
///                   "/dav/spaces/74351999-...$1651695e-..."), used to turn
///                   absolute hrefs into space-relative paths. May be passed
///                   with or without a trailing slash.
/// @param error      Set on parse failure.
/// @return Array of entries (including the space-root self entry, relativePath
///         @""), or nil on error.
+ (nullable NSArray<FileProviderWebDAVEntry *> *)parseMultistatus:(NSData *)xml hrefPrefix:(NSString *)hrefPrefix error:(NSError **)error;

/// Performs a live `PROPFIND Depth:1` against a folder and returns its entries
/// (the folder itself, relativePath @"", plus its immediate children).
///
/// @param davBase      Space DAV base URL (e.g. ".../dav/spaces/<space>"), as
///                     stored in the extension config plist (may be %-encoded).
/// @param relativePath Space-relative folder path (@"" for the space root).
/// @param token        OAuth bearer token.
/// @param completion   Called on a background queue with parsed entries or an error.
+ (void)propfindChildrenAtDavBase:(NSString *)davBase
                     relativePath:(NSString *)relativePath
                            token:(NSString *)token
                       completion:(void (^)(NSArray<FileProviderWebDAVEntry *> *_Nullable entries, NSError *_Nullable error))completion;

@end

NS_ASSUME_NONNULL_END
