// OpenCloudFileProviderExtension -- NSFileProviderReplicatedExtension implementation
// for macOS Files On Demand. Runs in an isolated extension process.
#pragma once

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>
#import <os/log.h>

/// The principal class for the OpenCloud File Provider App Extension.
/// Implements NSFileProviderReplicatedExtension (and NSFileProviderEnumerating)
/// to integrate with the macOS Files On Demand system.
///
/// All protocol methods are currently stubbed and return appropriate
/// "not implemented" errors while calling their completion handlers
/// to prevent deadlocks.
API_AVAILABLE(macos(12.0))
@interface OpenCloudFileProviderExtension : NSObject <NSFileProviderReplicatedExtension, NSFileProviderEnumerating, NSFileProviderThumbnailing>

/// The file provider domain this extension instance serves.
@property (nonatomic, readonly, strong) NSFileProviderDomain *domain;

@end
