/*
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 * Copyright (C) 2025 OpenCloud GmbH
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LineProcessor_h
#define LineProcessor_h

#import <Foundation/Foundation.h>

/// Protocol for processing lines received from a socket connection.
@protocol LineProcessor <NSObject>

/// Process a single line received from the socket.
/// @param line The line to process (without trailing newline).
- (void)process:(NSString *)line;

@optional
- (void)connectionDidOpen;
- (void)connectionDidClose;

@end

#endif /* LineProcessor_h */
