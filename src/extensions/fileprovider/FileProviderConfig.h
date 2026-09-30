// FileProviderConfig -- reads the per-domain server config (davUrl + access
// token) that the main app writes into the App Group container.
#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface FileProviderConfig : NSObject

/// Space DAV base URL for the domain (config key "davUrl"), or nil.
+ (nullable NSString *)davBaseForDomainIdentifier:(NSString *)domainId;

/// Current OAuth bearer token for the domain (config key "accessToken"), or nil.
+ (nullable NSString *)accessTokenForDomainIdentifier:(NSString *)domainId;

@end

NS_ASSUME_NONNULL_END
