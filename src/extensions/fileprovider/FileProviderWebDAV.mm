// FileProviderWebDAV -- WebDAV PROPFIND multistatus parser. See header.

#import "FileProviderWebDAV.h"

static NSString *const kDAVNS = @"DAV:";
static NSString *const kOCNS = @"http://owncloud.org/ns";

@implementation FileProviderWebDAVEntry
@end

#pragma mark - SAX delegate

@interface FPWebDAVParserDelegate : NSObject <NSXMLParserDelegate>
@property (nonatomic, copy) NSString *hrefPrefix; // normalised, no trailing slash
@property (nonatomic, strong) NSMutableArray<FileProviderWebDAVEntry *> *entries;
@end

@implementation FPWebDAVParserDelegate {
    // Per-response state.
    NSString *_href;
    NSMutableDictionary *_committed; // props from 200 propstats
    // Per-propstat state.
    NSMutableDictionary *_tmp;
    NSString *_propstatStatus;
    BOOL _inResourcetype;
    NSMutableString *_text;
    NSDateFormatter *_httpDateFormatter;
}

- (instancetype)init {
    if ((self = [super init])) {
        _entries = [NSMutableArray array];
        _text = [NSMutableString string];
        _httpDateFormatter = [[NSDateFormatter alloc] init];
        _httpDateFormatter.locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
        _httpDateFormatter.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
        _httpDateFormatter.dateFormat = @"EEE, dd MMM yyyy HH:mm:ss 'GMT'";
    }
    return self;
}

- (int64_t)epochFromHTTPDate:(NSString *)s {
    if (s.length == 0) return 0;
    NSDate *d = [_httpDateFormatter dateFromString:s];
    return d ? (int64_t)d.timeIntervalSince1970 : 0;
}

/// Turns an absolute-or-relative href into a space-relative, percent-decoded path.
/// The space root yields @"".
- (NSString *)relativePathForHref:(NSString *)href {
    if (href.length == 0) return @"";
    NSString *remainder = href;
    NSRange r = [href rangeOfString:self.hrefPrefix];
    if (r.location != NSNotFound) {
        remainder = [href substringFromIndex:r.location + r.length];
    }
    // Trim surrounding slashes.
    while ([remainder hasPrefix:@"/"]) remainder = [remainder substringFromIndex:1];
    while ([remainder hasSuffix:@"/"]) remainder = [remainder substringToIndex:remainder.length - 1];
    NSString *decoded = [remainder stringByRemovingPercentEncoding];
    return decoded ?: remainder;
}

#pragma mark NSXMLParserDelegate

- (void)parser:(NSXMLParser *)parser
    didStartElement:(NSString *)elementName
       namespaceURI:(NSString *)namespaceURI
      qualifiedName:(NSString *)qName
         attributes:(NSDictionary *)attributeDict {
    [_text setString:@""];

    if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"response"]) {
        _href = nil;
        _committed = [NSMutableDictionary dictionary];
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"propstat"]) {
        _tmp = [NSMutableDictionary dictionary];
        _propstatStatus = nil;
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"resourcetype"]) {
        _inResourcetype = YES;
    } else if (_inResourcetype && [namespaceURI isEqualToString:kDAVNS]
               && [elementName isEqualToString:@"collection"]) {
        _tmp[@"isDir"] = @YES;
    }
}

- (void)parser:(NSXMLParser *)parser foundCharacters:(NSString *)string {
    [_text appendString:string];
}

- (void)parser:(NSXMLParser *)parser
     didEndElement:(NSString *)elementName
      namespaceURI:(NSString *)namespaceURI
     qualifiedName:(NSString *)qName {
    NSString *text = [_text stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceAndNewlineCharacterSet]];

    if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"href"]) {
        if (!_href) _href = text; // first href = the response's own href
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"status"]) {
        _propstatStatus = text;
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"resourcetype"]) {
        _inResourcetype = NO;
    } else if ([namespaceURI isEqualToString:kOCNS] && [elementName isEqualToString:@"fileid"]) {
        if (text.length) _tmp[@"fileId"] = text; // oc:fileid takes priority
    } else if ([namespaceURI isEqualToString:kOCNS] && [elementName isEqualToString:@"id"]) {
        if (text.length && !_tmp[@"fileId"]) _tmp[@"fileId"] = text;
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"getetag"]) {
        _tmp[@"etag"] = [text stringByTrimmingCharactersInSet:
                         [NSCharacterSet characterSetWithCharactersInString:@"\""]];
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"getcontentlength"]) {
        _tmp[@"size"] = @([text longLongValue]);
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"getlastmodified"]) {
        _tmp[@"modtime"] = @([self epochFromHTTPDate:text]);
    } else if ([namespaceURI isEqualToString:kOCNS] && [elementName isEqualToString:@"permissions"]) {
        _tmp[@"permissions"] = text;
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"propstat"]) {
        // Commit props only from a 200 propstat.
        if (_propstatStatus && [_propstatStatus rangeOfString:@" 200 "].location != NSNotFound) {
            [_committed addEntriesFromDictionary:_tmp];
        }
        _tmp = nil;
    } else if ([namespaceURI isEqualToString:kDAVNS] && [elementName isEqualToString:@"response"]) {
        FileProviderWebDAVEntry *e = [[FileProviderWebDAVEntry alloc] init];
        NSString *rel = [self relativePathForHref:_href];
        e.relativePath = rel;
        NSRange slash = [rel rangeOfString:@"/" options:NSBackwardsSearch];
        e.name = (slash.location == NSNotFound) ? rel : [rel substringFromIndex:slash.location + 1];
        e.fileId = _committed[@"fileId"] ?: @"";
        e.isDirectory = [_committed[@"isDir"] boolValue];
        e.size = [_committed[@"size"] longLongValue];
        e.modtime = [_committed[@"modtime"] longLongValue];
        e.etag = _committed[@"etag"] ?: @"";
        e.permissions = _committed[@"permissions"] ?: @"";
        [_entries addObject:e];
        _href = nil;
        _committed = nil;
    }
}

@end

#pragma mark - FileProviderWebDAV

@implementation FileProviderWebDAV

+ (NSArray<FileProviderWebDAVEntry *> *)parseMultistatus:(NSData *)xml
                                             hrefPrefix:(NSString *)hrefPrefix
                                                  error:(NSError **)error {
    if (xml.length == 0) {
        if (error) {
            *error = [NSError errorWithDomain:@"FileProviderWebDAV" code:1
                                     userInfo:@{NSLocalizedDescriptionKey: @"empty body"}];
        }
        return nil;
    }

    NSString *prefix = hrefPrefix ?: @"";
    while ([prefix hasSuffix:@"/"]) prefix = [prefix substringToIndex:prefix.length - 1];

    FPWebDAVParserDelegate *delegate = [[FPWebDAVParserDelegate alloc] init];
    delegate.hrefPrefix = prefix;

    NSXMLParser *parser = [[NSXMLParser alloc] initWithData:xml];
    parser.shouldProcessNamespaces = YES;
    parser.delegate = delegate;

    if (![parser parse]) {
        if (error) *error = parser.parserError;
        return nil;
    }
    return delegate.entries;
}

+ (void)propfindChildrenAtDavBase:(NSString *)davBase
                     relativePath:(NSString *)relativePath
                            token:(NSString *)token
                       completion:(void (^)(NSArray<FileProviderWebDAVEntry *> *_Nullable,
                                            NSError *_Nullable))completion {
    NSString *base = davBase;
    while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];

    // Build the request URL: base + "/" + percent-encoded relative path.
    NSString *urlString = base;
    if (relativePath.length > 0) {
        NSString *encoded = [relativePath stringByAddingPercentEncodingWithAllowedCharacters:
                             [NSCharacterSet URLPathAllowedCharacterSet]];
        urlString = [NSString stringWithFormat:@"%@/%@", base, encoded];
    }
    urlString = [urlString stringByAppendingString:@"/"]; // PROPFIND on a collection

    NSURL *url = [NSURL URLWithString:urlString];
    if (!url) {
        completion(nil, [NSError errorWithDomain:@"FileProviderWebDAV" code:2
                                        userInfo:@{NSLocalizedDescriptionKey: @"bad URL"}]);
        return;
    }

    // hrefPrefix = the space-root path as it appears (decoded) in response hrefs.
    NSString *hrefPrefix = [NSURL URLWithString:base].path ?: @"";

    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    req.HTTPMethod = @"PROPFIND";
    [req setValue:[NSString stringWithFormat:@"Bearer %@", token] forHTTPHeaderField:@"Authorization"];
    [req setValue:@"1" forHTTPHeaderField:@"Depth"];
    [req setValue:@"application/xml; charset=utf-8" forHTTPHeaderField:@"Content-Type"];
    req.HTTPBody = [@"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<d:propfind xmlns:d=\"DAV:\" xmlns:oc=\"http://owncloud.org/ns\"><d:prop>"
        "<oc:id/><oc:fileid/><d:resourcetype/><d:getcontentlength/>"
        "<d:getlastmodified/><d:getetag/><oc:permissions/>"
        "</d:prop></d:propfind>" dataUsingEncoding:NSUTF8StringEncoding];

    // Reuse a single session so TCP/TLS connections are kept alive between
    // PROPFINDs (a fresh session per request forces a full handshake every
    // folder open — the main cause of the per-folder browse latency).
    static NSURLSession *sharedSession = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration defaultSessionConfiguration];
        cfg.HTTPMaximumConnectionsPerHost = 6;
        cfg.timeoutIntervalForRequest = 30;
        sharedSession = [NSURLSession sessionWithConfiguration:cfg];
    });
    [[sharedSession dataTaskWithRequest:req completionHandler:^(NSData *data, NSURLResponse *response, NSError *netErr) {
        if (netErr) {
            completion(nil, netErr);
            return;
        }
        NSHTTPURLResponse *http = (NSHTTPURLResponse *)response;
        if (http.statusCode < 200 || http.statusCode >= 300) {
            NSError *e = [NSError errorWithDomain:@"FileProviderWebDAV" code:http.statusCode
                userInfo:@{NSLocalizedDescriptionKey:
                    [NSString stringWithFormat:@"PROPFIND HTTP %ld", (long)http.statusCode]}];
            completion(nil, e);
            return;
        }
        NSError *parseErr = nil;
        NSArray<FileProviderWebDAVEntry *> *entries =
            [self parseMultistatus:data hrefPrefix:hrefPrefix error:&parseErr];
        completion(entries, parseErr);
    }] resume];
}

@end
