/*
 * Copyright (C) 2025 OpenCloud GmbH
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Based on Nextcloud Desktop Client:
 * https://github.com/nextcloud/desktop/blob/master/shell_integration/MacOSX/NextcloudIntegration/NCDesktopClientSocketKit/LineProcessor.h
 */

#ifndef LineProcessor_h
#define LineProcessor_h

#import <Foundation/Foundation.h>

/// Protocol for processing lines received from the socket.
/// Implementers handle parsing and dispatching commands to the appropriate delegate methods.
@protocol LineProcessor <NSObject>

- (void)process:(NSString *)line;

@optional
- (void)connectionDidOpen;
- (void)connectionDidClose;

@end

#endif /* LineProcessor_h */
