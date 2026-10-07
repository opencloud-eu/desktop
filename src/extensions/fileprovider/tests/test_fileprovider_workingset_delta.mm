// Standalone unit test for FileProviderWorkingSetDelta (pure working-set diff).
//
// Build & run:
//   clang++ -fobjc-arc -framework Foundation -I.. \
//     test_fileprovider_workingset_delta.mm ../FileProviderWorkingSetDelta.mm \
//     -o /tmp/test_fp_ws && /tmp/test_fp_ws
//
#import <Foundation/Foundation.h>
#import "FileProviderWorkingSetDelta.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; fprintf(stderr, "FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static NSDictionary *item(NSString *fileId, NSString *etag) {
    return @{ @"fileId": fileId, @"etag": etag, @"filename": fileId, @"path": fileId };
}

// Item with an explicit path (for rename tests: same fileId+etag, different path).
static NSDictionary *itemP(NSString *fileId, NSString *etag, NSString *path) {
    return @{ @"fileId": fileId, @"etag": etag, @"filename": path, @"path": path };
}

static BOOL containsId(NSArray<NSString *> *ids, NSString *fid) {
    return [ids containsObject:fid];
}

static NSString *changedId(FPWorkingSetDelta *d, NSUInteger i) {
    return d.changedItems[i][@"fileId"];
}

int main(void) {
    @autoreleasepool {
        // (1) First run: no previous snapshot -> everything is "changed", nothing deleted.
        {
            NSArray *current = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, @[]);
            CHECK(d.changedItems.count == 2, "first run: all items changed");
            CHECK(d.deletedFileIds.count == 0, "first run: nothing deleted");
            CHECK(d.currentFileIds.count == 2, "first run: 2 current ids");
            CHECK(containsId(d.currentFileIds, @"a") && containsId(d.currentFileIds, @"b"),
                  "first run: current ids a,b");
        }

        // (2) Identical second run: same fingerprints -> ZERO changed, zero deleted. (The churn fix.)
        {
            NSArray *prev    = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            NSArray *current = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 0, "identical run: zero changed");
            CHECK(d.deletedFileIds.count == 0, "identical run: zero deleted");
        }

        // (3) One etag changed -> only that one reported.
        {
            NSArray *prev    = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            NSArray *current = @[ item(@"a", @"e2"), item(@"b", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 1, "one changed: count 1");
            CHECK([changedId(d, 0) isEqualToString:@"a"], "one changed: it's 'a'");
            CHECK(d.deletedFileIds.count == 0, "one changed: nothing deleted");
        }

        // (4) New item added -> only the new one reported.
        {
            NSArray *prev    = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            NSArray *current = @[ item(@"a", @"e1"), item(@"b", @"e1"), item(@"c", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 1, "new item: count 1");
            CHECK([changedId(d, 0) isEqualToString:@"c"], "new item: it's 'c'");
        }

        // (5) Item removed -> reported as deleted, nothing changed.
        {
            NSArray *prev    = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            NSArray *current = @[ item(@"a", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 0, "removed: zero changed");
            CHECK(d.deletedFileIds.count == 1, "removed: one deleted");
            CHECK([d.deletedFileIds[0] isEqualToString:@"b"], "removed: it's 'b'");
        }

        // (6) Mixed: change 'a', add 'd', delete 'c'.
        {
            NSArray *prev    = @[ item(@"a", @"e1"), item(@"b", @"e1"), item(@"c", @"e1") ];
            NSArray *current = @[ item(@"a", @"e2"), item(@"b", @"e1"), item(@"d", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 2, "mixed: 2 changed (a,d)");
            CHECK(d.deletedFileIds.count == 1 && [d.deletedFileIds[0] isEqualToString:@"c"],
                  "mixed: c deleted");
        }

        // (7) Empty current with previous -> everything deleted.
        {
            NSArray *prev = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(@[], prev);
            CHECK(d.changedItems.count == 0, "empty: zero changed");
            CHECK(d.deletedFileIds.count == 2, "empty: both deleted");
        }

        // (8) Item with empty etag previously, now has one -> treated as changed.
        {
            NSArray *prev    = @[ item(@"a", @"") ];
            NSArray *current = @[ item(@"a", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 1, "etag appeared: changed");
        }

        // (8b) THE RENAME BUG: same fileId, same etag, only the path/name changed.
        //      The old etag-only diff reported nothing; now it must be "changed".
        {
            NSArray *prev    = @[ itemP(@"X", @"e1", @"apple_error_2.txt"),    item(@"b", @"e1") ];
            NSArray *current = @[ itemP(@"X", @"e1", @"apple_error_FIXED.txt"), item(@"b", @"e1") ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prev);
            CHECK(d.changedItems.count == 1, "rename (same id+etag): one changed");
            CHECK([changedId(d, 0) isEqualToString:@"X"], "rename: it's 'X'");
            CHECK(d.deletedFileIds.count == 0, "rename: nothing deleted (same fileId)");
        }

        // ── FPItemSetSignature: the sync-anchor signature ──────────────────
        // Regression: a server-side rename keeps child count and max(modtime)
        // unchanged, so the old "count-maxmodtime" anchor never moved and Finder
        // never re-enumerated. The signature must change on rename.
        NSDictionary *(^it)(NSString *, NSString *, NSString *, long long) =
            ^(NSString *fid, NSString *path, NSString *etag, long long modtime) {
                return @{ @"fileId": fid, @"path": path, @"filename": path,
                          @"etag": etag, @"modtime": @(modtime) };
            };

        // (9) Identical sets -> identical signature (stable, order-independent).
        {
            NSArray *a = @[ it(@"1", @"a.txt", @"e1", 100), it(@"2", @"b.txt", @"e1", 200) ];
            NSArray *b = @[ it(@"2", @"b.txt", @"e1", 200), it(@"1", @"a.txt", @"e1", 100) ];
            CHECK([FPItemSetSignature(a) isEqualToString:FPItemSetSignature(b)],
                  "signature: identical sets (any order) match");
        }

        // (10) THE BUG: rename keeps count AND max(modtime) but path changes.
        //      Old anchor (count-maxmodtime) would collide; signature must differ.
        {
            NSArray *before = @[ it(@"X", @"apple_error.txt",  @"e1", 500),
                                 it(@"Y", @"other.txt",        @"e2", 999) ];
            NSArray *after  = @[ it(@"X", @"apple_error_ren.txt", @"e1", 500),
                                 it(@"Y", @"other.txt",           @"e2", 999) ];
            // sanity: same count, same max modtime (the old anchor's inputs)
            CHECK(before.count == after.count, "rename: same count");
            CHECK(![FPItemSetSignature(before) isEqualToString:FPItemSetSignature(after)],
                  "signature: rename (same count+maxmodtime) changes anchor");
        }

        // (11) Rename that also yields a NEW fileId (this server's behaviour).
        {
            NSArray *before = @[ it(@"oldid", @"f.txt",  @"e1", 500) ];
            NSArray *after  = @[ it(@"newid", @"f2.txt", @"e1", 500) ];
            CHECK(![FPItemSetSignature(before) isEqualToString:FPItemSetSignature(after)],
                  "signature: rename with new fileId changes anchor");
        }

        // (12) Delete and add still move the anchor.
        {
            NSArray *base = @[ it(@"1", @"a.txt", @"e1", 100), it(@"2", @"b.txt", @"e1", 200) ];
            NSArray *del  = @[ it(@"1", @"a.txt", @"e1", 100) ];
            NSArray *add  = @[ it(@"1", @"a.txt", @"e1", 100), it(@"2", @"b.txt", @"e1", 200),
                               it(@"3", @"c.txt", @"e1", 50) ];
            CHECK(![FPItemSetSignature(base) isEqualToString:FPItemSetSignature(del)],
                  "signature: delete changes anchor");
            CHECK(![FPItemSetSignature(base) isEqualToString:FPItemSetSignature(add)],
                  "signature: add changes anchor");
        }

        // (13) Pure content change (etag) moves the anchor.
        {
            NSArray *before = @[ it(@"1", @"a.txt", @"e1", 100) ];
            NSArray *after  = @[ it(@"1", @"a.txt", @"e2", 100) ];
            CHECK(![FPItemSetSignature(before) isEqualToString:FPItemSetSignature(after)],
                  "signature: etag change moves anchor");
        }

        fprintf(stderr, "\n%d checks, %d failures\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }
}
