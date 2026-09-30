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

/// Diffs `currentItems` against `previousItems` (each dict carries "fileId",
/// "etag" and "path"/"filename"). An item counts as changed when it is new OR its
/// fingerprint differs, where the fingerprint is etag+path — so a server-side
/// RENAME (same fileId and etag, different name) is reported as changed. The
/// previous etag-only comparison missed renames entirely.
FPWorkingSetDelta *FPComputeWorkingSetDelta(NSArray<NSDictionary *> *currentItems, NSArray<NSDictionary *> *previousItems);

/// Identity fingerprint of an item: changes on content (etag) OR name/path change.
NSString *FPItemFingerprint(NSDictionary *item);

/// Deterministic content signature of an item set, used as the FileProvider sync
/// anchor. It MUST change whenever any item is added, removed, renamed, moved or
/// its content changes — otherwise fileproviderd, which re-enumerates only when
/// the anchor changes, never picks the change up. The previous implementation used
/// `count + max(modtime)`, which is invariant under a rename (count and modtime are
/// both unchanged), so server-side renames never propagated to Finder. This hashes
/// the sorted `fileId|path|etag` of every item, so a rename (path change and/or new
/// fileId) always moves the anchor. Foundation-only and order-independent so it is
/// unit-testable and stable across process launches.
NSString *FPItemSetSignature(NSArray<NSDictionary *> *items);

NS_ASSUME_NONNULL_END
