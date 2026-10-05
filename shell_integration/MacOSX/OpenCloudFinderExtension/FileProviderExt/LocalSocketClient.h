/*
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 * Copyright (C) 2025 OpenCloud GmbH
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#import "LineProcessor.h"
#import <Foundation/Foundation.h>

#ifndef LocalSocketClient_h
#define LocalSocketClient_h
#define BUF_SIZE 4096

/// Class handling asynchronous communication with a server over a local UNIX socket.
///
/// The implementation uses a `DispatchQueue` and `DispatchSource`s to handle asynchronous
/// communication and thread safety. The delegate that handles the line-decoding is
/// **not invoked on the UI thread**, but the (random) thread associated with the `DispatchQueue`.
///
/// If any UI work needs to be done, the `LineProcessor` class dispatches this work on the
/// main queue (so the UI thread) itself.
///
/// Public methods dispatch socket work onto a serial queue. Initialization happens on the
/// caller's thread, and isConnected reads an atomic flag. The serial queue owns all other state.
///
/// Sending and receiving data to and from the socket, is handled by two `DispatchSource`s.
/// These will run an event handler when data can be read from resp. written to the socket.
/// These handlers will also be run on the `DispatchQueue`.

@interface LocalSocketClient : NSObject

- (instancetype)initWithSocketPath:(NSString *)socketPath lineProcessor:(id<LineProcessor>)lineProcessor;

@property (readonly) BOOL isConnected;

- (void)start;
- (void)restart;
- (void)closeConnection;

- (void)sendMessage:(NSString *)message;
- (void)askOnSocket:(NSString *)path query:(NSString *)verb;
- (void)askForIcon:(NSString *)path isDirectory:(BOOL)isDirectory;

@end

#endif /* LocalSocketClient_h */
