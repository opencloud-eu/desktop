// FileProviderItem -- NSFileProviderItem adapter wrapping sync journal data
// for the macOS File Provider extension.
#pragma once

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C class conforming to NSFileProviderItem that wraps sync journal
/// data into the form expected by the macOS File Provider framework.
///
/// Since the extension runs in a separate process with no direct Qt access,
/// all data is stored as Objective-C types (NSString, NSDate, NSNumber).
API_AVAILABLE(macos(12.0))
@interface FileProviderItem : NSObject <NSFileProviderItem>

#pragma mark - NSFileProviderItem required properties

@property (nonatomic, readonly, copy) NSFileProviderItemIdentifier itemIdentifier;
@property (nonatomic, readonly, copy) NSFileProviderItemIdentifier parentItemIdentifier;
@property (nonatomic, readonly, copy) NSString *filename;
@property (nonatomic, readonly, copy) NSString *typeIdentifier;
@property (nonatomic, readonly, copy) UTType *contentType;
@property (nonatomic, readonly) NSFileProviderItemCapabilities capabilities;
@property (nonatomic, readonly, nullable) NSNumber *documentSize;
@property (nonatomic, readonly, nullable) NSDate *contentModificationDate;
@property (nonatomic, readonly, nullable) NSDate *creationDate;
@property (nonatomic, readonly, nullable) NSNumber *childItemCount;
@property (nonatomic, readonly) NSFileProviderItemVersion *itemVersion;

#pragma mark - Transfer state properties

@property (nonatomic, readonly) BOOL isUploaded;
@property (nonatomic, readonly) BOOL isDownloaded;
@property (nonatomic, readonly) BOOL isDownloading;
@property (nonatomic, readonly) BOOL isUploading;

#pragma mark - Directory properties

#pragma mark - Initializers

/// Designated initializer using a dictionary of metadata (typically received via XPC).
/// Keys: @"fileId", @"filename", @"parentId", @"isDirectory", @"size", @"modDate",
///       @"isUploaded", @"isDownloaded", @"isDownloading", @"isUploading", @"childItemCount"
- (instancetype)initWithDictionary:(NSDictionary *)dict;

/// Convenience initializer with explicit parameters.
- (instancetype)initWithIdentifier:(NSString *)fileId
                          filename:(NSString *)name
                  parentIdentifier:(NSFileProviderItemIdentifier)parentId
                       isDirectory:(BOOL)isDir
                              size:(int64_t)size
                           modDate:(nullable NSDate *)date;

/// Returns a placeholder root container item.
+ (instancetype)rootContainerItem;

@end

NS_ASSUME_NONNULL_END
