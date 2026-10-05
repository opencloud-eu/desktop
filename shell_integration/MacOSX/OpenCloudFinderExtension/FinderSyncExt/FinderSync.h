/*
 * Copyright (C) by Jocelyn Turcotte <jturcotte@woboq.com>
 * Copyright (C) 2025 OpenCloud GmbH
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * Updated to use Unix domain sockets instead of XPC.
 * Based on Nextcloud Desktop Client:
 * https://github.com/nextcloud/desktop
 */

#import "FinderSyncSocketLineProcessor.h"
#import "LocalSocketClient.h"
#import <Cocoa/Cocoa.h>
#import <FinderSync/FinderSync.h>

@interface FinderSync : FIFinderSync <SyncClientDelegate>

@property (nonatomic, strong) LocalSocketClient *localSocketClient;
@property (nonatomic, strong) FinderSyncSocketLineProcessor *lineProcessor;

@end
