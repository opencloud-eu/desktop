// FileProviderItemCache -- see header.

#import "FileProviderItemCache.h"

@implementation FileProviderItemCache {
    NSURL *_url;
    NSLock *_lock;
    // fileId -> relative path
    NSMutableDictionary<NSString *, NSString *> *_idToPath;
    // container path -> { @"etag": NSString, @"children": NSArray<NSString*> }
    NSMutableDictionary<NSString *, NSDictionary *> *_containers;
    // fileId -> full item metadata dict
    NSMutableDictionary<NSString *, NSDictionary *> *_metadata;
}

- (instancetype)initWithFileURL:(NSURL *)url {
    if ((self = [super init])) {
        _url = url;
        _lock = [[NSLock alloc] init];
        _idToPath = [NSMutableDictionary dictionary];
        _containers = [NSMutableDictionary dictionary];
        _metadata = [NSMutableDictionary dictionary];
        [self reload];
    }
    return self;
}

#pragma mark id <-> path

- (NSString *)pathForFileId:(NSString *)fileId {
    if (fileId.length == 0) return nil;
    [_lock lock];
    NSString *p = _idToPath[fileId];
    [_lock unlock];
    return p;
}

- (void)setPath:(NSString *)path forFileId:(NSString *)fileId {
    if (fileId.length == 0 || path == nil) return;
    [_lock lock];
    _idToPath[fileId] = path;
    [_lock unlock];
}

- (NSDictionary *)metadataForFileId:(NSString *)fileId {
    if (fileId.length == 0) return nil;
    [_lock lock];
    NSDictionary *m = _metadata[fileId];
    [_lock unlock];
    return m;
}

- (void)setMetadata:(NSDictionary *)metadata forFileId:(NSString *)fileId {
    if (fileId.length == 0 || metadata == nil) return;
    [_lock lock];
    _metadata[fileId] = metadata;
    NSString *path = metadata[@"path"];
    if ([path isKindOfClass:[NSString class]]) {
        _idToPath[fileId] = path;
    }
    [_lock unlock];
}

#pragma mark container snapshot

- (NSString *)etagForContainerPath:(NSString *)containerPath {
    if (containerPath == nil) return nil;
    [_lock lock];
    NSString *e = _containers[containerPath][@"etag"];
    [_lock unlock];
    return e;
}

- (NSArray<NSString *> *)childFileIdsForContainerPath:(NSString *)containerPath {
    if (containerPath == nil) return nil;
    [_lock lock];
    NSArray<NSString *> *c = _containers[containerPath][@"children"];
    [_lock unlock];
    return c;
}

- (void)setContainerPath:(NSString *)containerPath
                    etag:(NSString *)etag
            childFileIds:(NSArray<NSString *> *)childFileIds {
    if (containerPath == nil) return;
    [_lock lock];
    _containers[containerPath] = @{
        @"etag": etag ?: @"",
        @"children": childFileIds ?: @[],
    };
    [_lock unlock];
}

#pragma mark persistence

- (BOOL)save {
    [_lock lock];
    NSDictionary *root = @{
        @"idToPath": [_idToPath copy],
        @"containers": [_containers copy],
        @"metadata": [_metadata copy],
    };
    [_lock unlock];

    NSError *err = nil;
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:root
                                                             format:NSPropertyListBinaryFormat_v1_0
                                                            options:0
                                                              error:&err];
    if (!data) return NO;
    return [data writeToURL:_url options:NSDataWritingAtomic error:&err];
}

- (void)reload {
    NSData *data = [NSData dataWithContentsOfURL:_url];
    [_lock lock];
    [_idToPath removeAllObjects];
    [_containers removeAllObjects];
    [_metadata removeAllObjects];
    if (data) {
        NSDictionary *root = [NSPropertyListSerialization propertyListWithData:data
                                                                      options:NSPropertyListImmutable
                                                                       format:nil error:nil];
        if ([root isKindOfClass:[NSDictionary class]]) {
            NSDictionary *i2p = root[@"idToPath"];
            NSDictionary *cnt = root[@"containers"];
            NSDictionary *md = root[@"metadata"];
            if ([i2p isKindOfClass:[NSDictionary class]]) [_idToPath addEntriesFromDictionary:i2p];
            if ([cnt isKindOfClass:[NSDictionary class]]) [_containers addEntriesFromDictionary:cnt];
            if ([md isKindOfClass:[NSDictionary class]]) [_metadata addEntriesFromDictionary:md];
        }
    }
    [_lock unlock];
}

@end