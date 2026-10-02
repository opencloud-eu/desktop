/*
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 * Copyright (C) 2025 OpenCloud GmbH
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#import "LocalSocketClient.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

@interface LocalSocketClient () {
    NSString *_socketPath;
    id<LineProcessor> _lineProcessor;
    int _sock;
    atomic_bool _connected;
    dispatch_queue_t _localSocketQueue;
    dispatch_source_t _readSource;
    dispatch_source_t _writeSource;
    BOOL _writeSuspended;
    NSUInteger _connectionGeneration;
    NSMutableData *_inBuffer;
    NSMutableData *_outBuffer;
}
@end

@implementation LocalSocketClient

- (instancetype)initWithSocketPath:(NSString *)socketPath lineProcessor:(id<LineProcessor>)lineProcessor
{
    self = [super init];
    if (self) {
        _socketPath = [socketPath copy];
        _lineProcessor = lineProcessor;
        _sock = -1;
        atomic_init(&_connected, false);
        _localSocketQueue = dispatch_queue_create("eu.opencloud.localSocketQueue", DISPATCH_QUEUE_SERIAL);
        _inBuffer = [NSMutableData data];
        _outBuffer = [NSMutableData data];
    }
    return self;
}

- (BOOL)isConnected
{
    return atomic_load(&_connected);
}

- (void)start
{
    dispatch_async(_localSocketQueue, ^{ [self startOnQueue]; });
}

- (void)startOnQueue
{
    if (self.isConnected) {
        return;
    }
    struct sockaddr_un address = {0};
    if (!_socketPath || [_socketPath lengthOfBytesUsingEncoding:NSUTF8StringEncoding] >= sizeof(address.sun_path)) {
        NSLog(@"LocalSocketClient: invalid socket path");
        return;
    }
    address.sun_family = AF_UNIX;
    address.sun_len = sizeof(address);
    strlcpy(address.sun_path, _socketPath.fileSystemRepresentation, sizeof(address.sun_path));
    _sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_sock == -1) {
        [self restartOnQueue];
        return;
    }
    // A disconnected server must produce an error, never SIGPIPE in the extension.
    int noSigPipe = 1;
    setsockopt(_sock, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
    if (connect(_sock, (struct sockaddr *)&address, sizeof(address)) == -1 || fcntl(_sock, F_SETFL, fcntl(_sock, F_GETFL, 0) | O_NONBLOCK) == -1) {
        [self restartOnQueue];
        return;
    }
    _readSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, _sock, 0, _localSocketQueue);
    dispatch_source_set_event_handler(_readSource, ^{ [self readFromSocket]; });
    _writeSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE, _sock, 0, _localSocketQueue);
    dispatch_source_set_event_handler(_writeSource, ^{ [self writeToSocket]; });
    _writeSuspended = YES;
    atomic_store(&_connected, true);
    dispatch_resume(_readSource);
    if ([_lineProcessor respondsToSelector:@selector(connectionDidOpen)]) {
        [_lineProcessor connectionDidOpen];
    }
}

- (void)restart
{
    dispatch_async(_localSocketQueue, ^{ [self restartOnQueue]; });
}

- (void)restartOnQueue
{
    [self closeConnectionOnQueue];
    const NSUInteger generation = _connectionGeneration;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), _localSocketQueue, ^{
        if (generation == self->_connectionGeneration) {
            [self startOnQueue];
        }
    });
}

- (void)closeConnection
{
    dispatch_async(_localSocketQueue, ^{ [self closeConnectionOnQueue]; });
}

- (void)closeConnectionOnQueue
{
    ++_connectionGeneration;
    BOOL wasConnected = atomic_exchange(&_connected, false);
    const int socketToClose = _sock;
    _sock = -1;
    dispatch_group_t cancellations = dispatch_group_create();
    if (_readSource) {
        dispatch_group_enter(cancellations);
        dispatch_source_set_cancel_handler(_readSource, ^{ dispatch_group_leave(cancellations); });
        dispatch_source_cancel(_readSource);
        _readSource = nil;
    }
    if (_writeSource) {
        dispatch_group_enter(cancellations);
        dispatch_source_set_cancel_handler(_writeSource, ^{ dispatch_group_leave(cancellations); });
        // A suspended source cannot finish cancellation until it is resumed.
        if (_writeSuspended) {
            dispatch_resume(_writeSource);
        }
        dispatch_source_cancel(_writeSource);
        _writeSource = nil;
    }
    if (socketToClose != -1) {
        dispatch_group_notify(cancellations, _localSocketQueue, ^{ close(socketToClose); });
    }
    [_inBuffer setLength:0];
    [_outBuffer setLength:0];
    if (wasConnected && [_lineProcessor respondsToSelector:@selector(connectionDidClose)]) {
        [_lineProcessor connectionDidClose];
    }
}

- (void)sendMessage:(NSString *)message
{
    dispatch_async(_localSocketQueue, ^{
        if (!self.isConnected || message.length == 0) {
            return;
        }
        [self->_outBuffer appendData:[message dataUsingEncoding:NSUTF8StringEncoding]];
        if (self->_writeSuspended) {
            self->_writeSuspended = NO;
            dispatch_resume(self->_writeSource);
        }
    });
}

- (void)askOnSocket:(NSString *)path query:(NSString *)verb
{
    [self sendMessage:[NSString stringWithFormat:@"%@:%@\n", verb, path]];
}

- (void)askForIcon:(NSString *)path isDirectory:(BOOL)isDirectory
{
    [self askOnSocket:path query:isDirectory ? @"RETRIEVE_FOLDER_STATUS" : @"RETRIEVE_FILE_STATUS"];
}

- (void)writeToSocket
{
    if (!self.isConnected) {
        return;
    }
    if (_outBuffer.length > 0) {
        ssize_t written = write(_sock, _outBuffer.bytes, _outBuffer.length);
        if (written < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                [self restartOnQueue];
            }
            return;
        }
        if (written == 0) {
            [self restartOnQueue];
            return;
        }
        [_outBuffer replaceBytesInRange:NSMakeRange(0, written) withBytes:NULL length:0];
    }
    if (_outBuffer.length == 0 && !_writeSuspended) {
        _writeSuspended = YES;
        dispatch_suspend(_writeSource);
    }
}

- (void)readFromSocket
{
    if (!self.isConnected) {
        return;
    }
    char buffer[BUF_SIZE];
    while (true) {
        ssize_t count = read(_sock, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (count <= 0) {
            [self restartOnQueue];
            return;
        }
        [_inBuffer appendBytes:buffer length:count];
        [self processInBuffer];
    }
}

- (void)processInBuffer
{
    NSData *newline = [@"\n" dataUsingEncoding:NSUTF8StringEncoding];
    while (_inBuffer.length > 0) {
        NSRange separator = [_inBuffer rangeOfData:newline options:0 range:NSMakeRange(0, _inBuffer.length)];
        if (separator.location == NSNotFound) {
            return; // Keep incomplete lines, including split UTF-8 characters, for the next read.
        }
        NSString *line = [[NSString alloc] initWithBytes:_inBuffer.bytes length:separator.location encoding:NSUTF8StringEncoding];
        [_inBuffer replaceBytesInRange:NSMakeRange(0, separator.location + 1) withBytes:NULL length:0];
        if (line) {
            [_lineProcessor process:line];
        }
    }
}
@end
