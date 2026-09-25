// FileProviderWorkingSetDelta -- see header.

#import "FileProviderWorkingSetDelta.h"

@implementation FPWorkingSetDelta
@end

NSString *FPItemFingerprint(NSDictionary *item) {
    NSString *etag = item[@"etag"] ?: @"";
    NSString *path = item[@"path"] ?: (item[@"filename"] ?: @"");
    return [NSString stringWithFormat:@"%@|%@", etag, path];
}

FPWorkingSetDelta *FPComputeWorkingSetDelta(
    NSArray<NSDictionary *> *currentItems,
    NSArray<NSDictionary *> *previousItems) {

    // Build the previous fingerprint map and id set from the previous snapshot.
    NSMutableDictionary<NSString *, NSString *> *prevFingerprint =
        [NSMutableDictionary dictionaryWithCapacity:previousItems.count];
    for (NSDictionary *dict in previousItems) {
        NSString *fid = dict[@"fileId"] ?: @"";
        if (fid.length) prevFingerprint[fid] = FPItemFingerprint(dict);
    }

    NSMutableArray<NSDictionary *> *changed = [NSMutableArray array];
    NSMutableArray<NSString *> *currentIds = [NSMutableArray arrayWithCapacity:currentItems.count];
    NSMutableSet<NSString *> *currentIdSet = [NSMutableSet setWithCapacity:currentItems.count];

    for (NSDictionary *dict in currentItems) {
        NSString *fid = dict[@"fileId"] ?: @"";
        [currentIds addObject:fid];
        [currentIdSet addObject:fid];

        NSString *prevFp = prevFingerprint[fid]; // nil => unknown/new
        NSString *curFp = FPItemFingerprint(dict);
        BOOL isNew = (prevFp == nil);
        // Reported as changed when new, content (etag) changed, OR renamed/moved
        // (path changed) — the latter is what the old etag-only check missed.
        if (isNew || ![prevFp isEqualToString:curFp]) {
            [changed addObject:dict];
        }
    }

    // Deletions: ids known before that are no longer present.
    NSMutableArray<NSString *> *deleted = [NSMutableArray array];
    for (NSString *fid in prevFingerprint) {
        if (![currentIdSet containsObject:fid]) {
            [deleted addObject:fid];
        }
    }

    FPWorkingSetDelta *result = [[FPWorkingSetDelta alloc] init];
    result.changedItems = changed;
    result.deletedFileIds = deleted;
    result.currentFileIds = currentIds;
    return result;
}

NSString *FPItemSetSignature(NSArray<NSDictionary *> *items) {
    // Build one stable token per item from its identity-relevant fields, then
    // sort so the signature is order-independent. fileId catches add/remove (and
    // renames that yield a new id), path catches in-place renames/moves, etag
    // catches content changes.
    NSMutableArray<NSString *> *parts = [NSMutableArray arrayWithCapacity:items.count];
    for (NSDictionary *d in items) {
        NSString *fid = d[@"fileId"] ?: @"";
        NSString *path = d[@"path"] ?: (d[@"filename"] ?: @"");
        NSString *etag = d[@"etag"] ?: @"";
        [parts addObject:[NSString stringWithFormat:@"%@|%@|%@", fid, path, etag]];
    }
    [parts sortUsingSelector:@selector(compare:)];

    // FNV-1a 64-bit over the joined tokens. Deterministic across launches (unlike
    // -[NSString hash]), so an unchanged tree yields a stable anchor and does not
    // trigger spurious full re-enumeration after a relaunch.
    uint64_t h = 1469598103934665603ULL; // FNV offset basis
    NSString *joined = [parts componentsJoinedByString:@"\n"];
    const char *bytes = joined.UTF8String ?: "";
    for (const char *p = bytes; *p; ++p) {
        h ^= (uint8_t)(*p);
        h *= 1099511628211ULL; // FNV prime
    }
    return [NSString stringWithFormat:@"%lu-%016llx",
            (unsigned long)items.count, (unsigned long long)h];
}
