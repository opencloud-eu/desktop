// FileProviderWorkingSetDelta -- see header.

#import "FileProviderWorkingSetDelta.h"

@implementation FPWorkingSetDelta
@end

FPWorkingSetDelta *FPComputeWorkingSetDelta(
    NSArray<NSDictionary *> *currentItems,
    NSDictionary<NSString *, NSString *> *previousEtagByFileId,
    NSArray<NSString *> *previousFileIds) {

    NSMutableArray<NSDictionary *> *changed = [NSMutableArray array];
    NSMutableArray<NSString *> *currentIds = [NSMutableArray arrayWithCapacity:currentItems.count];
    NSMutableSet<NSString *> *currentIdSet = [NSMutableSet setWithCapacity:currentItems.count];

    for (NSDictionary *dict in currentItems) {
        NSString *fid = dict[@"fileId"] ?: @"";
        [currentIds addObject:fid];
        [currentIdSet addObject:fid];

        NSString *prevEtag = previousEtagByFileId[fid]; // nil => unknown/new
        NSString *newEtag = dict[@"etag"] ?: @"";
        BOOL isNew = (prevEtag == nil);
        if (isNew || ![prevEtag isEqualToString:newEtag]) {
            [changed addObject:dict];
        }
    }

    // Deletions: ids known before that are no longer present.
    NSMutableArray<NSString *> *deleted = [NSMutableArray array];
    for (NSString *fid in previousFileIds) {
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
