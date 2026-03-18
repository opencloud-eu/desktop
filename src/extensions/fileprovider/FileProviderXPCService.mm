// FileProviderXPCService -- XPC communication bridge implementation.
// Manages the NSXPCConnection lifecycle between extension and main app.

#import "FileProviderXPCService.h"

static os_log_t xpcLog(void) {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("eu.opencloud.desktop.fileprovider", "xpc");
    });
    return log;
}

/// Maximum number of automatic reconnection attempts before giving up.
static const NSUInteger MAX_RECONNECT_ATTEMPTS = 3;

/// Delay between reconnection attempts (in seconds).
static const NSTimeInterval RECONNECT_DELAY = 2.0;

#pragma mark - FileProviderXPCService

API_AVAILABLE(macos(12.0))
@implementation FileProviderXPCService {
    NSXPCConnection *_connection;
    NSUInteger _reconnectAttempts;
    BOOL _invalidated;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _reconnectAttempts = 0;
        _invalidated = NO;
        // Connection is established lazily on first remoteObjectProxy call.
        // Enumeration uses the shared plist and does not need XPC.
    }
    return self;
}

#pragma mark - NSFileProviderServiceSource

- (NSFileProviderServiceName)serviceName {
    return kOpenCloudXPCServiceName;
}

- (nullable NSXPCListenerEndpoint *)makeListenerEndpointAndReturnError:(NSError *__autoreleasing *)error {
    // This method is called by the system when the main app wants to connect
    // to this extension's service. For the extension-to-app direction, we
    // use the connection created in _establishConnection instead.
    //
    // Return nil here; the actual communication channel is set up via
    // NSXPCConnection to the main app's Mach service.
    os_log_info(xpcLog(), "makeListenerEndpointAndReturnError called");

    if (error) {
        *error = [NSError errorWithDomain:NSFileProviderErrorDomain
                                     code:NSFileProviderErrorServerUnreachable
                                 userInfo:@{NSLocalizedDescriptionKey: @"Service endpoint not available from extension side"}];
    }
    return nil;
}

#pragma mark - Connection Management

- (void)_establishConnection {
    if (_invalidated) {
        os_log_info(xpcLog(), "Connection not established: service has been invalidated");
        return;
    }

    // Read the listener endpoint from the App Group shared container.
    // The main app writes this file when it starts its anonymous NSXPCListener.
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) {
        os_log_error(xpcLog(), "Cannot access App Group container: %{public}@", kOpenCloudAppGroupIdentifier);
        [self _handleConnectionFailure];
        return;
    }

    NSURL *endpointURL = [containerURL URLByAppendingPathComponent:kOpenCloudXPCEndpointFilename];
    NSData *endpointData = [NSData dataWithContentsOfURL:endpointURL];
    if (!endpointData) {
        os_log_error(xpcLog(), "XPC endpoint file not found at: %{public}@ (main app may not be running)",
                     endpointURL.path);
        [self _handleConnectionFailure];
        return;
    }

    NSError *unarchiveError = nil;
    NSXPCListenerEndpoint *endpoint = [NSKeyedUnarchiver unarchivedObjectOfClass:[NSXPCListenerEndpoint class]
                                                                        fromData:endpointData
                                                                           error:&unarchiveError];
    if (!endpoint || unarchiveError) {
        os_log_error(xpcLog(), "Failed to unarchive XPC endpoint: %{public}@",
                     unarchiveError.localizedDescription);
        [self _handleConnectionFailure];
        return;
    }

    os_log_info(xpcLog(), "Read XPC listener endpoint from App Group container");

    _connection = [[NSXPCConnection alloc] initWithListenerEndpoint:endpoint];

    // Configure the remote interface (what we expect the main app to implement).
    _connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(OpenCloudXPCServiceProtocol)];

    __weak __typeof__(self) weakSelf = self;

    _connection.interruptionHandler = ^{
        os_log_error(xpcLog(), "XPC connection interrupted");
        [weakSelf _handleConnectionFailure];
    };

    _connection.invalidationHandler = ^{
        os_log_error(xpcLog(), "XPC connection invalidated");
        [weakSelf _handleConnectionFailure];
    };

    [_connection resume];
    _reconnectAttempts = 0;

    os_log_info(xpcLog(), "XPC connection established via App Group endpoint");
}

- (void)_handleConnectionFailure {
    if (_invalidated) {
        return;
    }

    _connection = nil;

    if (_reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        os_log_error(xpcLog(), "Max reconnection attempts (%lu) reached, giving up", (unsigned long)MAX_RECONNECT_ATTEMPTS);
        return;
    }

    _reconnectAttempts++;
    os_log_info(xpcLog(), "Scheduling reconnection attempt %lu/%lu in %.0f seconds",
                (unsigned long)_reconnectAttempts,
                (unsigned long)MAX_RECONNECT_ATTEMPTS,
                RECONNECT_DELAY);

    __weak __typeof__(self) weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(RECONNECT_DELAY * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [weakSelf _establishConnection];
    });
}

#pragma mark - Remote Object Access

- (nullable id<OpenCloudXPCServiceProtocol>)remoteObjectProxy {
    if (!_connection) {
        // Lazily establish the connection on first use.
        os_log_info(xpcLog(), "remoteObjectProxy: establishing connection on demand");
        [self _establishConnection];
    }

    if (!_connection) {
        os_log_error(xpcLog(), "remoteObjectProxy: no connection available after attempt");
        return nil;
    }

    return (id<OpenCloudXPCServiceProtocol>)[_connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        os_log_error(xpcLog(), "Remote object proxy error: %{public}@", error.localizedDescription);
    }];
}

#pragma mark - Teardown

- (void)invalidate {
    os_log_info(xpcLog(), "Invalidating XPC service");
    _invalidated = YES;

    if (_connection) {
        [_connection invalidate];
        _connection = nil;
    }
}

@end
