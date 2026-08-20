// Standalone unit test for FileProviderWebDAV (PROPFIND parser).
//
// Build & run (no full app build needed):
//   clang -fobjc-arc -framework Foundation \
//     -I.. \
//     test_fileprovider_webdav.mm ../FileProviderWebDAV.mm \
//     -o /tmp/test_fp_webdav && /tmp/test_fp_webdav
//
#import <Foundation/Foundation.h>
#import "FileProviderWebDAV.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; fprintf(stderr, "FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

// Representative oCIS "spaces" PROPFIND Depth:1 response for a space root with
// a folder (umlaut, percent-encoded), a file, and a file whose name has a space.
static NSData *fixtureXML(void) {
    NSString *xml = @"<?xml version=\"1.0\"?>\n"
    "<d:multistatus xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\" xmlns:s=\"http://sabredav.org/ns\">\n"
    "  <d:response>\n"
    "    <d:href>/dav/spaces/74351999-70d1-4480-9a3f-f1cff123dfa6$1651695e-a108-1040-9584-abc777505ae3/</d:href>\n"
    "    <d:propstat><d:prop>\n"
    "      <oc:id>74351999$1651695e!root</oc:id>\n"
    "      <d:resourcetype><d:collection/></d:resourcetype>\n"
    "      <d:getetag>\"rootetag123\"</d:getetag>\n"
    "      <oc:permissions>RDNVCK</oc:permissions>\n"
    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
    "  </d:response>\n"
    "  <d:response>\n"
    // Directory entry in the real-server shape: oc:id + oc:fileid identical, an
    // oc:size, AND a separate 404 propstat for getcontentlength (dirs have none).
    // The parser must commit only the 200 propstat and ignore the 404 block.
    "    <d:href>/dav/spaces/74351999-70d1-4480-9a3f-f1cff123dfa6$1651695e-a108-1040-9584-abc777505ae3/Arbeitsvertr%C3%A4ge/</d:href>\n"
    "    <d:propstat><d:prop>\n"
    "      <oc:id>74351999$1651695e!17df2811</oc:id>\n"
    "      <oc:fileid>74351999$1651695e!17df2811</oc:fileid>\n"
    "      <d:resourcetype><d:collection/></d:resourcetype>\n"
    "      <d:getetag>\"folderetag\"</d:getetag>\n"
    "      <d:getlastmodified>Wed, 18 Feb 2026 15:23:00 GMT</d:getlastmodified>\n"
    "      <oc:permissions>RDNVCK</oc:permissions>\n"
    "      <oc:size>0</oc:size>\n"
    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
    "    <d:propstat><d:prop>\n"
    "      <d:getcontentlength/>\n"
    "    </d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat>\n"
    "  </d:response>\n"
    "  <d:response>\n"
    "    <d:href>/dav/spaces/74351999-70d1-4480-9a3f-f1cff123dfa6$1651695e-a108-1040-9584-abc777505ae3/LP_neu.md</d:href>\n"
    "    <d:propstat><d:prop>\n"
    "      <oc:id>74351999$1651695e!lpneu</oc:id>\n"
    "      <d:resourcetype/>\n"
    "      <d:getcontentlength>1234</d:getcontentlength>\n"
    "      <d:getetag>\"fileetag\"</d:getetag>\n"
    "      <d:getlastmodified>Tue, 18 Feb 2026 16:42:00 GMT</d:getlastmodified>\n"
    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
    "  </d:response>\n"
    "  <d:response>\n"
    "    <d:href>/dav/spaces/74351999-70d1-4480-9a3f-f1cff123dfa6$1651695e-a108-1040-9584-abc777505ae3/Neue%20Datei.txt</d:href>\n"
    "    <d:propstat><d:prop>\n"
    "      <oc:id>74351999$1651695e!neue</oc:id>\n"
    "      <d:resourcetype/>\n"
    "      <d:getcontentlength>0</d:getcontentlength>\n"
    "      <d:getetag>\"emptyetag\"</d:getetag>\n"
    "    </d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>\n"
    "  </d:response>\n"
    "</d:multistatus>\n";
    return [xml dataUsingEncoding:NSUTF8StringEncoding];
}

static FileProviderWebDAVEntry *find(NSArray<FileProviderWebDAVEntry *> *entries, NSString *relPath) {
    for (FileProviderWebDAVEntry *e in entries) {
        if ([e.relativePath isEqualToString:relPath]) return e;
    }
    return nil;
}

int main(void) {
    @autoreleasepool {
        NSString *prefix = @"/dav/spaces/74351999-70d1-4480-9a3f-f1cff123dfa6$1651695e-a108-1040-9584-abc777505ae3";
        NSError *err = nil;
        NSArray<FileProviderWebDAVEntry *> *entries =
            [FileProviderWebDAV parseMultistatus:fixtureXML() hrefPrefix:prefix error:&err];

        CHECK(entries != nil, "parse returned nil");
        CHECK(err == nil, "parse set an error");
        CHECK(entries.count == 4, "expected 4 entries");

        // Space root (self): relativePath "", directory.
        FileProviderWebDAVEntry *root = find(entries, @"");
        CHECK(root != nil, "self/root entry missing");
        CHECK(root.isDirectory, "root should be a directory");
        CHECK([root.fileId isEqualToString:@"74351999$1651695e!root"], "root fileId wrong");
        CHECK([root.etag isEqualToString:@"rootetag123"], "root etag quotes not stripped");

        // Folder with umlaut: percent-decoded path + name.
        FileProviderWebDAVEntry *av = find(entries, @"Arbeitsverträge");
        CHECK(av != nil, "Arbeitsverträge entry missing (percent-decoding?)");
        CHECK(av.isDirectory, "Arbeitsverträge should be a directory");
        CHECK([av.name isEqualToString:@"Arbeitsverträge"], "Arbeitsverträge name wrong");
        CHECK([av.fileId isEqualToString:@"74351999$1651695e!17df2811"], "folder fileId wrong");
        CHECK(av.modtime > 0, "folder modtime not parsed");
        // The 404 propstat for getcontentlength must not leak a size onto the dir.
        CHECK(av.size == 0, "dir size should be 0 despite 404 getcontentlength block");

        // Regular file with content length.
        FileProviderWebDAVEntry *lp = find(entries, @"LP_neu.md");
        CHECK(lp != nil, "LP_neu.md entry missing");
        CHECK(!lp.isDirectory, "LP_neu.md should be a file");
        CHECK(lp.size == 1234, "LP_neu.md size wrong");
        CHECK(lp.modtime > 0, "file modtime not parsed");

        // File whose name contains a space.
        FileProviderWebDAVEntry *neue = find(entries, @"Neue Datei.txt");
        CHECK(neue != nil, "Neue Datei.txt entry missing (space decoding?)");
        CHECK(!neue.isDirectory, "Neue Datei.txt should be a file");
        CHECK(neue.size == 0, "Neue Datei.txt size should be 0");

        if (g_failures == 0) {
            printf("OK: %d checks passed\n", g_checks);
            return 0;
        }
        fprintf(stderr, "%d/%d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
}