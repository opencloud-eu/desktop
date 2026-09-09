// FileProviderConfig -- see header.

#import "FileProviderConfig.h"
#import "FileProviderXPCService.h" // kOpenCloudAppGroupIdentifier

@implementation FileProviderConfig

/// Loads the per-domain config plist (legacy global fallback) from the App Group.
+ (NSDictionary *)configForDomainIdentifier:(NSString *)domainId {
    NSURL *container = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!container) return nil;

    NSString *perDomain = [NSString stringWithFormat:@"fileprovider_config_%@.plist", domainId];
    NSURL *perDomainURL = [container URLByAppendingPathComponent:perDomain];
    NSURL *url = [[NSFileManager defaultManager] fileExistsAtPath:perDomainURL.path]
        ? perDomainURL
        : [container URLByAppendingPathComponent:@"fileprovider_config.plist"];

    NSData *data = [NSData dataWithContentsOfURL:url];
    if (!data) return nil;
    NSDictionary *cfg = [NSPropertyListSerialization propertyListWithData:data
                                                                 options:NSPropertyListImmutable
                                                                  format:nil error:nil];
    return [cfg isKindOfClass:[NSDictionary class]] ? cfg : nil;
}

+ (NSString *)davBaseForDomainIdentifier:(NSString *)domainId {
    NSString *dav = [self configForDomainIdentifier:domainId][@"davUrl"];
    return dav.length > 0 ? dav : nil;
}

+ (NSString *)accessTokenForDomainIdentifier:(NSString *)domainId {
    NSString *tok = [self configForDomainIdentifier:domainId][@"accessToken"];
    return tok.length > 0 ? tok : nil;
}

@end
