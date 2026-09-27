#import "LocalSocketClient.h"
#import <Foundation/Foundation.h>
#ifdef TEST_FINDER
#import "FinderSyncSocketLineProcessor.h"
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(...)                                                                                                                                             \
    do {                                                                                                                                                       \
        if (!(__VA_ARGS__)) {                                                                                                                                  \
            NSLog(@"FAIL line %d: %s", __LINE__, #__VA_ARGS__);                                                                                                \
            exit(1);                                                                                                                                           \
        }                                                                                                                                                      \
    } while (0)

@interface LocalSocketClient (TestFraming)
- (void)processInBuffer;
@end

@interface Receiver : NSObject <LineProcessor>
@property NSMutableArray<NSString *> *lines;
@property dispatch_semaphore_t opened;
@property dispatch_semaphore_t closed;
@end
@implementation Receiver
- (instancetype)init
{
    if ((self = [super init])) {
        _lines = [NSMutableArray array];
        _opened = dispatch_semaphore_create(0);
        _closed = dispatch_semaphore_create(0);
    }
    return self;
}
- (void)process:(NSString *)line
{
    [_lines addObject:line];
}
- (void)connectionDidOpen
{
    dispatch_semaphore_signal(_opened);
}
- (void)connectionDidClose
{
    dispatch_semaphore_signal(_closed);
}
@end

#ifdef TEST_FINDER
@interface MenuReceiver : NSObject <SyncClientDelegate>
@property NSMutableArray *events;
@property NSString *registeredPath;
@property NSString *stringValue;
@end
@implementation MenuReceiver
- (instancetype)init
{
    if ((self = [super init])) {
        _events = [NSMutableArray array];
    }
    return self;
}
- (void)setResult:(NSString *)result forPath:(NSString *)path
{
}
- (void)reFetchFileNameCacheForPath:(NSString *)path
{
}
- (void)registerPath:(NSString *)path
{
    _registeredPath = path;
}
- (void)unregisterPath:(NSString *)path
{
}
- (void)setString:(NSString *)key value:(NSString *)value
{
    _stringValue = value;
}
- (void)resetMenuItems
{
    [_events addObject:@"begin"];
}
- (void)addMenuItem:(NSDictionary *)item
{
    [_events addObject:item[@"text"]];
}
- (void)menuHasCompleted
{
    [_events addObject:@"end"];
}
- (void)connectionDidOpen
{
}
- (void)connectionDidDie
{
}
@end
#endif

int main(void)
{
    @autoreleasepool {
        Receiver *receiver = [Receiver new];
        LocalSocketClient *client = [[LocalSocketClient alloc] initWithSocketPath:@"/unused" lineProcessor:receiver];
        NSMutableData *buffer = [client valueForKey:@"inBuffer"];
        [buffer appendData:[@"REGISTER_PATH:/some:" dataUsingEncoding:NSUTF8StringEncoding]];
        [client processInBuffer];
        CHECK(receiver.lines.count == 0);
        CHECK(buffer.length > 0);
        [buffer appendData:[@"folder\nSTATUS:OK:/one\n" dataUsingEncoding:NSUTF8StringEncoding]];
        [client processInBuffer];
        CHECK([receiver.lines isEqualToArray:@[ @"REGISTER_PATH:/some:folder", @"STATUS:OK:/one" ]]);
        NSData *unicode = [@"STATUS:OK:/café\n" dataUsingEncoding:NSUTF8StringEncoding];
        [buffer appendBytes:unicode.bytes length:unicode.length - 2];
        [client processInBuffer];
        CHECK(receiver.lines.count == 2);
        [buffer appendBytes:(const char *)unicode.bytes + unicode.length - 2 length:2];
        [client processInBuffer];
        CHECK([receiver.lines.lastObject isEqualToString:@"STATUS:OK:/café"]);
        const unsigned char invalid[] = {0xff, '\n'};
        [buffer appendBytes:invalid length:sizeof(invalid)];
        [client processInBuffer];
        CHECK(receiver.lines.count == 3);

        // Cancel while the write source is still suspended, reconnect, then close again.
        NSString *path = [NSString stringWithFormat:@"/tmp/oc-socket-test-%d", getpid()];
        struct sockaddr_un address = {0};
        address.sun_family = AF_UNIX;
        address.sun_len = sizeof(address);
        strlcpy(address.sun_path, path.fileSystemRepresentation, sizeof(address.sun_path));
        int listener = socket(AF_UNIX, SOCK_STREAM, 0);
        CHECK(listener >= 0);
        CHECK(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
        CHECK(listen(listener, 2) == 0);
        LocalSocketClient *networkClient = [[LocalSocketClient alloc] initWithSocketPath:path lineProcessor:receiver];
        [networkClient start];
        CHECK(dispatch_semaphore_wait(receiver.opened, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) == 0);
        int peer = accept(listener, NULL, NULL);
        CHECK(peer >= 0);
        close(peer);
        CHECK(dispatch_semaphore_wait(receiver.closed, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) == 0);
        CHECK(dispatch_semaphore_wait(receiver.opened, dispatch_time(DISPATCH_TIME_NOW, 7 * NSEC_PER_SEC)) == 0);
        peer = accept(listener, NULL, NULL);
        CHECK(peer >= 0);
        [networkClient sendMessage:@"GET_STRINGS:\n"];
        struct timeval timeout = {2, 0};
        setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        char response[64] = {0};
        CHECK(read(peer, response, sizeof(response)) == (ssize_t)strlen("GET_STRINGS:\n"));
        CHECK(strcmp(response, "GET_STRINGS:\n") == 0);
        [networkClient closeConnection];
        CHECK(dispatch_semaphore_wait(receiver.closed, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) == 0);
        CHECK(read(peer, response, sizeof(response)) == 0);
        close(peer);
        close(listener);
        unlink(path.fileSystemRepresentation);

#ifdef TEST_FINDER
        MenuReceiver *menu = [MenuReceiver new];
        FinderSyncSocketLineProcessor *processor = [[FinderSyncSocketLineProcessor alloc] initWithDelegate:menu];
        dispatch_semaphore_t processed = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
            [processor process:@"GET_MENU_ITEMS:BEGIN"];
            [processor process:@"MENU_ITEM:OPEN::Open: browser"];
            [processor process:@"GET_MENU_ITEMS:END"];
            dispatch_semaphore_signal(processed);
        });
        // Finder's main thread is waiting. All menu items must arrive before END.
        CHECK(dispatch_semaphore_wait(processed, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) == 0);
        CHECK([menu.events isEqualToArray:@[ @"begin", @"Open: browser", @"end" ]]);
        [processor process:@"REGISTER_PATH:/tmp/has:colon"];
        [processor process:@"STRING:TITLE:Open: cloud"];
        NSDate *until = [NSDate dateWithTimeIntervalSinceNow:1];
        while (!menu.stringValue && until.timeIntervalSinceNow > 0) {
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        CHECK([menu.registeredPath isEqualToString:@"/tmp/has:colon"]);
        CHECK([menu.stringValue isEqualToString:@"Open: cloud"]);
#endif
        NSLog(@"Socket regression tests passed");
    }
    return 0;
}
