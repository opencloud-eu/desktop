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
    return @{ @"fileId": fileId, @"etag": etag, @"filename": fileId };
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
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, @{}, @[]);
            CHECK(d.changedItems.count == 2, "first run: all items changed");
            CHECK(d.deletedFileIds.count == 0, "first run: nothing deleted");
            CHECK(d.currentFileIds.count == 2, "first run: 2 current ids");
            CHECK(containsId(d.currentFileIds, @"a") && containsId(d.currentFileIds, @"b"),
                  "first run: current ids a,b");
        }

        // (2) Identical second run: same etags -> ZERO changed, zero deleted. (The churn fix.)
        {
            NSArray *current = @[ item(@"a", @"e1"), item(@"b", @"e1") ];
            NSDictionary *prevEtags = @{ @"a": @"e1", @"b": @"e1" };
            NSArray *prevIds = @[ @"a", @"b" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prevEtags, prevIds);
            CHECK(d.changedItems.count == 0, "identical run: zero changed");
            CHECK(d.deletedFileIds.count == 0, "identical run: zero deleted");
        }

        // (3) One etag changed -> only that one reported.
        {
            NSArray *current = @[ item(@"a", @"e2"), item(@"b", @"e1") ];
            NSDictionary *prevEtags = @{ @"a": @"e1", @"b": @"e1" };
            NSArray *prevIds = @[ @"a", @"b" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prevEtags, prevIds);
            CHECK(d.changedItems.count == 1, "one changed: count 1");
            CHECK([changedId(d, 0) isEqualToString:@"a"], "one changed: it's 'a'");
            CHECK(d.deletedFileIds.count == 0, "one changed: nothing deleted");
        }

        // (4) New item added -> only the new one reported.
        {
            NSArray *current = @[ item(@"a", @"e1"), item(@"b", @"e1"), item(@"c", @"e1") ];
            NSDictionary *prevEtags = @{ @"a": @"e1", @"b": @"e1" };
            NSArray *prevIds = @[ @"a", @"b" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prevEtags, prevIds);
            CHECK(d.changedItems.count == 1, "new item: count 1");
            CHECK([changedId(d, 0) isEqualToString:@"c"], "new item: it's 'c'");
        }

        // (5) Item removed -> reported as deleted, nothing changed.
        {
            NSArray *current = @[ item(@"a", @"e1") ];
            NSDictionary *prevEtags = @{ @"a": @"e1", @"b": @"e1" };
            NSArray *prevIds = @[ @"a", @"b" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prevEtags, prevIds);
            CHECK(d.changedItems.count == 0, "removed: zero changed");
            CHECK(d.deletedFileIds.count == 1, "removed: one deleted");
            CHECK([d.deletedFileIds[0] isEqualToString:@"b"], "removed: it's 'b'");
        }

        // (6) Mixed: change 'a', add 'd', delete 'c'.
        {
            NSArray *current = @[ item(@"a", @"e2"), item(@"b", @"e1"), item(@"d", @"e1") ];
            NSDictionary *prevEtags = @{ @"a": @"e1", @"b": @"e1", @"c": @"e1" };
            NSArray *prevIds = @[ @"a", @"b", @"c" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prevEtags, prevIds);
            CHECK(d.changedItems.count == 2, "mixed: 2 changed (a,d)");
            CHECK(d.deletedFileIds.count == 1 && [d.deletedFileIds[0] isEqualToString:@"c"],
                  "mixed: c deleted");
        }

        // (7) Empty current with previous -> everything deleted.
        {
            NSDictionary *prevEtags = @{ @"a": @"e1", @"b": @"e1" };
            NSArray *prevIds = @[ @"a", @"b" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(@[], prevEtags, prevIds);
            CHECK(d.changedItems.count == 0, "empty: zero changed");
            CHECK(d.deletedFileIds.count == 2, "empty: both deleted");
        }

        // (8) Item with empty/missing etag previously, now has one -> treated as changed.
        {
            NSArray *current = @[ item(@"a", @"e1") ];
            NSDictionary *prevEtags = @{ @"a": @"" };
            NSArray *prevIds = @[ @"a" ];
            FPWorkingSetDelta *d = FPComputeWorkingSetDelta(current, prevEtags, prevIds);
            CHECK(d.changedItems.count == 1, "etag appeared: changed");
        }

        fprintf(stderr, "\n%d checks, %d failures\n", g_checks, g_failures);
        return g_failures == 0 ? 0 : 1;
    }
}
