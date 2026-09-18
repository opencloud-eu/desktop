// Standalone unit test for FileProviderItemCache.
//
// Build & run:
//   clang++ -fobjc-arc -framework Foundation -I.. \
//     test_fileprovider_item_cache.mm ../FileProviderItemCache.mm \
//     -o /tmp/test_fp_cache && /tmp/test_fp_cache
//
#import <Foundation/Foundation.h>
#import "FileProviderItemCache.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; fprintf(stderr, "FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static NSURL *tempCacheURL(void) {
    NSString *name = [NSString stringWithFormat:@"fpcache-test-%@.plist", [[NSUUID UUID] UUIDString]];
    return [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:name]];
}

int main(void) {
    @autoreleasepool {
        NSURL *url = tempCacheURL();

        // Populate and persist.
        FileProviderItemCache *c1 = [[FileProviderItemCache alloc] initWithFileURL:url];
        [c1 setPath:@"Arbeitsverträge" forFileId:@"id1"];
        [c1 setPath:@"Arbeitsverträge/2024" forFileId:@"id2"];
        [c1 setContainerPath:@"" etag:@"rootetag" childFileIds:@[@"id1"]];
        [c1 setContainerPath:@"Arbeitsverträge" etag:@"folderetag" childFileIds:@[@"id2"]];
        CHECK([c1 save], "save returned NO");

        // In-memory lookups on the same instance.
        CHECK([[c1 pathForFileId:@"id1"] isEqualToString:@"Arbeitsverträge"], "id1 path wrong (live)");
        CHECK([c1 pathForFileId:@"missing"] == nil, "missing id should be nil");

        // Reload from disk into a fresh instance — persistence round-trip.
        FileProviderItemCache *c2 = [[FileProviderItemCache alloc] initWithFileURL:url];
        CHECK([[c2 pathForFileId:@"id1"] isEqualToString:@"Arbeitsverträge"], "id1 path wrong (reloaded)");
        CHECK([[c2 pathForFileId:@"id2"] isEqualToString:@"Arbeitsverträge/2024"], "id2 path wrong (reloaded)");
        CHECK([[c2 etagForContainerPath:@""] isEqualToString:@"rootetag"], "root etag wrong (reloaded)");
        CHECK([[c2 etagForContainerPath:@"Arbeitsverträge"] isEqualToString:@"folderetag"], "folder etag wrong");

        NSArray<NSString *> *rootKids = [c2 childFileIdsForContainerPath:@""];
        CHECK(rootKids.count == 1 && [rootKids[0] isEqualToString:@"id1"], "root children wrong");

        // Updating a container replaces its snapshot.
        [c2 setContainerPath:@"" etag:@"rootetag2" childFileIds:@[@"id1", @"idNew"]];
        CHECK([[c2 etagForContainerPath:@""] isEqualToString:@"rootetag2"], "etag not updated");
        CHECK([c2 childFileIdsForContainerPath:@""].count == 2, "children not updated");

        // Unknown container -> nil.
        CHECK([c2 etagForContainerPath:@"Nope"] == nil, "unknown container etag should be nil");

        // Per-item metadata round-trips and is queryable by id, surviving reload.
        [c2 setMetadata:@{ @"fileId": @"idM", @"filename": @"Report.pdf",
                           @"path": @"Privat/Report.pdf", @"isDirectory": @NO,
                           @"size": @4242, @"etag": @"mEtag" }
               forFileId:@"idM"];
        CHECK([c2 save], "save (metadata) returned NO");
        FileProviderItemCache *c3 = [[FileProviderItemCache alloc] initWithFileURL:url];
        NSDictionary *md = [c3 metadataForFileId:@"idM"];
        CHECK(md != nil, "metadata missing after reload");
        CHECK([md[@"filename"] isEqualToString:@"Report.pdf"], "metadata filename wrong");
        CHECK([md[@"size"] longLongValue] == 4242, "metadata size wrong");
        // setMetadata also registers the id->path mapping.
        CHECK([[c3 pathForFileId:@"idM"] isEqualToString:@"Privat/Report.pdf"], "metadata did not set path");
        CHECK([c3 metadataForFileId:@"unknown"] == nil, "unknown metadata should be nil");

        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

        if (g_failures == 0) {
            printf("OK: %d checks passed\n", g_checks);
            return 0;
        }
        fprintf(stderr, "%d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
}