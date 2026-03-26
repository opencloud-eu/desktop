// FileProviderEnumerator -- NSFileProviderEnumerator implementation that serves
// directory listings to the macOS File Provider system via XPC.
#pragma once

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>

@class FileProviderXPCService;

NS_ASSUME_NONNULL_BEGIN

/// Enumerates items within a given container (directory) for the File Provider
/// framework. Obtains item listings from the main application via XPC since
/// the extension runs in a separate process without direct sync journal access.
API_AVAILABLE(macos(12.0))
@interface FileProviderEnumerator : NSObject <NSFileProviderEnumerator>

/// Designated initializer.
/// @param containerId  The identifier of the container to enumerate
///                     (e.g. root container or a folder's file ID).
/// @param service      The XPC service used to communicate with the main app.
- (instancetype)initWithContainerIdentifier:(NSFileProviderItemIdentifier)containerId
                                 xpcService:(FileProviderXPCService *)service
                                     domain:(NSFileProviderDomain *)domain;

@end

NS_ASSUME_NONNULL_END
