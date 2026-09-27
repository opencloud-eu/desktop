/*
 * Copyright (C) 2025 OpenCloud GmbH
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Based on Nextcloud Desktop Client:
 * https://github.com/nextcloud/desktop/blob/master/shell_integration/MacOSX/NextcloudIntegration/FinderSyncExt/FinderSyncSocketLineProcessor.m
 */

#import <Foundation/Foundation.h>
#import "FinderSyncSocketLineProcessor.h"

@implementation FinderSyncSocketLineProcessor

-(instancetype)initWithDelegate:(id<SyncClientDelegate>)delegate
{
    NSLog(@"FinderSyncSocketLineProcessor: Init with delegate.");
    self = [super init];
    if (self) {
        self.delegate = delegate;
    }
    return self;
}

-(void)process:(NSString*)line
{
    NSLog(@"FinderSyncSocketLineProcessor: Processing line: '%@'", line);
    NSArray *split = [line componentsSeparatedByString:@":"];

    if ([split count] == 0) {
        return;
    }

    NSString *command = [split objectAtIndex:0];

    if([command isEqualToString:@"STATUS"]) {
        if ([split count] < 3) {
            NSLog(@"FinderSyncSocketLineProcessor: STATUS command malformed");
            return;
        }
        NSString *result = [split objectAtIndex:1];
        NSArray *pathSplit = [split subarrayWithRange:NSMakeRange(2, [split count] - 2)]; // Get everything after location 2
        NSString *path = [pathSplit componentsJoinedByString:@":"];

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate setResult:result forPath:path];
        });
    } else if([command isEqualToString:@"UPDATE_VIEW"]) {
        if ([split count] < 2) {
            return;
        }
        NSString *path = [[split subarrayWithRange:NSMakeRange(1, split.count - 1)] componentsJoinedByString:@":"];

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate reFetchFileNameCacheForPath:path];
        });
    } else if([command isEqualToString:@"REGISTER_PATH"]) {
        if ([split count] < 2) {
            return;
        }
        NSString *path = [[split subarrayWithRange:NSMakeRange(1, split.count - 1)] componentsJoinedByString:@":"];

        dispatch_async(dispatch_get_main_queue(), ^{
            NSLog(@"FinderSyncSocketLineProcessor: Registering path %@", path);
            [self.delegate registerPath:path];
        });
    } else if([command isEqualToString:@"UNREGISTER_PATH"]) {
        if ([split count] < 2) {
            return;
        }
        NSString *path = [[split subarrayWithRange:NSMakeRange(1, split.count - 1)] componentsJoinedByString:@":"];

        dispatch_async(dispatch_get_main_queue(), ^{
            NSLog(@"FinderSyncSocketLineProcessor: Unregistering path %@", path);
            [self.delegate unregisterPath:path];
        });
    } else if([command isEqualToString:@"GET_STRINGS"]) {
        // BEGIN and END messages, do nothing.
        return;
    } else if([command isEqualToString:@"STRING"]) {
        if ([split count] < 3) {
            return;
        }
        NSString *key = [split objectAtIndex:1];
        NSString *value = [[split subarrayWithRange:NSMakeRange(2, split.count - 2)] componentsJoinedByString:@":"];

        dispatch_async(dispatch_get_main_queue(), ^{
            [self.delegate setString:key value:value];
        });
    } else if([command isEqualToString:@"GET_MENU_ITEMS"]) {
        if ([split count] < 2) {
            return;
        }
        if([[split objectAtIndex:1] isEqualToString:@"BEGIN"]) {
            [self.delegate resetMenuItems];
        } else {
            // END message - signal that menu is complete
            [self.delegate menuHasCompleted];
        }
    } else if([command isEqualToString:@"MENU_ITEM"]) {
        if ([split count] < 4) {
            return;
        }
        NSDictionary *item = @{
            @"command" : [split objectAtIndex:1],
            @"flags" : [split objectAtIndex:2],
            @"text" : [[split subarrayWithRange:NSMakeRange(3, split.count - 3)] componentsJoinedByString:@":"]
        };

        [self.delegate addMenuItem:item];
    } else {
        NSLog(@"FinderSyncSocketLineProcessor: Unknown command: %@", command);
    }
}

- (void)connectionDidOpen
{
    dispatch_async(dispatch_get_main_queue(), ^{ [self.delegate connectionDidOpen]; });
}

- (void)connectionDidClose
{
    // Wake any menu wait immediately; the delegate dispatches Finder UI cleanup.
    [self.delegate connectionDidDie];
}

@end
