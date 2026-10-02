/*
 * Copyright (C) 2025 OpenCloud GmbH
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Based on Nextcloud Desktop Client:
 * https://github.com/nextcloud/desktop/blob/master/shell_integration/MacOSX/NextcloudIntegration/FinderSyncExt/FinderSyncSocketLineProcessor.h
 */

#ifndef FinderSyncSocketLineProcessor_h
#define FinderSyncSocketLineProcessor_h

#import "LineProcessor.h"
#import <Foundation/Foundation.h>

/// Protocol for the FinderSync delegate to receive parsed commands.
/// This matches the existing SyncClientProxyDelegate but with cleaner naming.
@protocol SyncClientDelegate <NSObject>

- (void)setResult:(NSString *)result forPath:(NSString *)path;
- (void)reFetchFileNameCacheForPath:(NSString *)path;
- (void)registerPath:(NSString *)path;
- (void)unregisterPath:(NSString *)path;
- (void)setString:(NSString *)key value:(NSString *)value;
- (void)resetMenuItems;
- (void)addMenuItem:(NSDictionary *)item;
- (void)menuHasCompleted;
- (void)connectionDidOpen;
- (void)connectionDidDie;

@end

/// Finder UI updates run on the main queue. Menu replies run on the socket queue
/// because Finder waits for them; the delegate protects menu state with a condition.
///
/// These tasks are parsed from byte data (UTF8 strings) acquired from the socket; look at the
/// LocalSocketClient for more detail on how data is read from and written to the socket.

@interface FinderSyncSocketLineProcessor : NSObject <LineProcessor>

@property (nonatomic, weak) id<SyncClientDelegate> delegate;

- (instancetype)initWithDelegate:(id<SyncClientDelegate>)delegate;

@end

#endif /* FinderSyncSocketLineProcessor_h */
