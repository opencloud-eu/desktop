// FileProviderWorkingSetDelta -- pure diff of the working set against its
// previous snapshot. Foundation-only so it is unit-testable without the
// FileProvider framework.
//
// The working-set enumerator used to report EVERY item as "updated" on every
// change-enumeration pass, which made fileproviderd re-index the whole tree
// (hundreds of items) every ~30s — wasting CPU and constantly rewriting
// Finder's view. This helper computes the real delta so only genuinely new or
// changed items are reported.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface FPWorkingSetDelta : NSObject
/// Item dicts that are new or whose etag changed since the previous snapshot.
@property (nonatomic, strong) NSArray<NSDictionary *> *changedItems;
/// File IDs that were in the previous snapshot but are gone now.
@property (nonatomic, strong) NSArray<NSString *> *deletedFileIds;
/// All current file IDs, for persisting as the next snapshot.
@property (nonatomic, strong) NSArray<NSString *> *currentFileIds;
@end

/// Diffs `currentItems` (each dict carries "fileId" and "etag") against the
/// previous snapshot (`previousEtagByFileId`: fileId -> etag, and
/// `previousFileIds`: the ids known last time). An item counts as changed when
/// it is new (no previous etag) or its etag differs from the previous one.
FPWorkingSetDelta *FPComputeWorkingSetDelta(
    NSArray<NSDictionary *> *currentItems, NSDictionary<NSString *, NSString *> *previousEtagByFileId, NSArray<NSString *> *previousFileIds);

NS_ASSUME_NONNULL_END
