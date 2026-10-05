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

#import "FinderSync.h"
#import <Security/Security.h>

@interface FinderSync()
{
    NSMutableSet *_registeredDirectories;
    NSString *_shareMenuTitle;
    NSMutableDictionary *_strings;
    NSMutableArray *_menuItems;
    NSCondition *_menuIsComplete;
    BOOL _menuReady;
    BOOL _menuRequestPending;
}
@end

@implementation FinderSync

- (instancetype)init
{
    self = [super init];

    if (self) {
        _registeredDirectories = NSMutableSet.set;
        _strings = NSMutableDictionary.dictionary;
        _menuIsComplete = [[NSCondition alloc] init];
        FIFinderSyncController *syncController = [FIFinderSyncController defaultController];
        NSBundle *extBundle = [NSBundle bundleForClass:[self class]];
        // This was added to the bundle's Info.plist to get it from the build system
        NSString *socketApiPrefix = [extBundle objectForInfoDictionaryKey:@"SocketApiPrefix"];

        NSImage *ok = [extBundle imageForResource:@"ok.icns"];
        NSImage *ok_swm = [extBundle imageForResource:@"ok_swm.icns"];
        NSImage *sync = [extBundle imageForResource:@"sync.icns"];
        NSImage *warning = [extBundle imageForResource:@"warning.icns"];
        NSImage *error = [extBundle imageForResource:@"error.icns"];

        [syncController setBadgeImage:ok label:@"Up to date" forBadgeIdentifier:@"OK"];
        [syncController setBadgeImage:sync label:@"Synchronizing" forBadgeIdentifier:@"SYNC"];
        [syncController setBadgeImage:sync label:@"Synchronizing" forBadgeIdentifier:@"NEW"];
        [syncController setBadgeImage:warning label:@"Ignored" forBadgeIdentifier:@"IGNORE"];
        [syncController setBadgeImage:error label:@"Error" forBadgeIdentifier:@"ERROR"];
        [syncController setBadgeImage:ok_swm label:@"Shared" forBadgeIdentifier:@"OK+SWM"];
        [syncController setBadgeImage:sync label:@"Synchronizing" forBadgeIdentifier:@"SYNC+SWM"];
        [syncController setBadgeImage:sync label:@"Synchronizing" forBadgeIdentifier:@"NEW+SWM"];
        [syncController setBadgeImage:warning label:@"Ignored" forBadgeIdentifier:@"IGNORE+SWM"];
        [syncController setBadgeImage:error label:@"Error" forBadgeIdentifier:@"ERROR+SWM"];

        // Get socket path from App Group container
        // The socket file is created by the main app in the shared App Group container.
        // Path: ~/Library/Group Containers/<TEAM>.<bundle-id>/.socket
        // We try multiple possible App Group IDs to handle both dev and signed builds.
        NSURL *container = nil;
        NSURL *socketPath = nil;

        // Build list of App Group candidates to try
        NSMutableArray<NSString *> *candidates = [NSMutableArray array];

        // 1. Try Info.plist configured value first
        if (socketApiPrefix.length > 0) {
            [candidates addObject:socketApiPrefix];
        }

        // 2. Try with team ID from code signing (if different from plist value)
        NSString *teamId = [self getTeamIdentifierFromSigning];
        if (teamId.length > 0) {
            NSString *teamPrefixed = [NSString stringWithFormat:@"%@.eu.opencloud.desktop", teamId];
            if (![candidates containsObject:teamPrefixed]) {
                [candidates addObject:teamPrefixed];
            }
        }

        // 3. Try plain domain (dev builds)
        if (![candidates containsObject:@"eu.opencloud.desktop"]) {
            [candidates addObject:@"eu.opencloud.desktop"];
        }

        // Find first working App Group
        for (NSString *candidate in candidates) {
            NSLog(@"FinderSync: Trying App Group: %@", candidate);
            NSURL *tryContainer = [[NSFileManager defaultManager] containerURLForSecurityApplicationGroupIdentifier:candidate];
            if (tryContainer) {
                container = tryContainer;
                socketPath = [container URLByAppendingPathComponent:@".socket" isDirectory:NO];
                NSLog(@"FinderSync: Found valid App Group: %@ -> %@", candidate, container.path);
                break;
            }
        }

        if (!container) {
            NSLog(@"FinderSync: ERROR - No valid App Group found from candidates: %@", [candidates componentsJoinedByString:@", "]);
        }

        NSLog(@"FinderSync: Socket path: %@", socketPath.path);

        if (socketPath.path) {
            self.lineProcessor = [[FinderSyncSocketLineProcessor alloc] initWithDelegate:self];
            self.localSocketClient = [[LocalSocketClient alloc] initWithSocketPath:socketPath.path
                                                                     lineProcessor:self.lineProcessor];
            [self.localSocketClient start];
        } else {
            NSLog(@"FinderSync: No socket path. Not initiating local socket client.");
            self.localSocketClient = nil;
        }
    }

    return self;
}

#pragma mark - Primary Finder Sync protocol methods

- (void)requestBadgeIdentifierForURL:(NSURL *)url
{
    BOOL isDir;
    if ([[NSFileManager defaultManager] fileExistsAtPath:[url path] isDirectory:&isDir] == NO) {
        NSLog(@"FinderSync: ERROR - Could not determine file type of %@", [url path]);
        isDir = NO;
    }

    NSString *normalizedPath = [[url path] decomposedStringWithCanonicalMapping];
    [self.localSocketClient askForIcon:normalizedPath isDirectory:isDir];
}

#pragma mark - Menu and toolbar item support

- (NSString *)selectedPathsSeparatedByRecordSeparator
{
    FIFinderSyncController *syncController = [FIFinderSyncController defaultController];
    NSMutableString *string = [[NSMutableString alloc] init];
    [syncController.selectedItemURLs enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
        if (string.length > 0) {
            [string appendString:@"\x1e"]; // record separator
        }
        NSString *normalizedPath = [[obj path] decomposedStringWithCanonicalMapping];
        [string appendString:normalizedPath];
    }];
    return string;
}

- (NSMenu *)menuForMenuKind:(FIMenuKind)whichMenu
{
    if (![self.localSocketClient isConnected]) {
        return nil;
    }

    FIFinderSyncController *syncController = [FIFinderSyncController defaultController];
    NSMutableSet *rootPaths = [[NSMutableSet alloc] init];
    [syncController.directoryURLs enumerateObjectsUsingBlock:^(id obj, BOOL *stop) {
        [rootPaths addObject:[obj path]];
    }];

    // The server doesn't support sharing a root directory so do not show the option in this case.
    __block BOOL onlyRootsSelected = YES;
    [syncController.selectedItemURLs enumerateObjectsUsingBlock:^(id obj, NSUInteger idx, BOOL *stop) {
        if (![rootPaths member:[obj path]]) {
            onlyRootsSelected = NO;
            *stop = YES;
        }
    }];

    NSString *paths = [self selectedPathsSeparatedByRecordSeparator];
    [_menuIsComplete lock];
    // Do not confuse a late reply from a timed-out request with a new selection.
    if (_menuRequestPending) {
        [_menuIsComplete unlock];
        return nil;
    }
    _menuRequestPending = YES;
    _menuReady = NO;
    _menuItems = [NSMutableArray array];
    [self.localSocketClient askOnSocket:paths query:@"GET_MENU_ITEMS"];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
    while (!_menuReady && [_menuIsComplete waitUntilDate:deadline]) { }
    NSArray *menuItems = _menuReady ? [_menuItems copy] : @[];
    BOOL timedOut = !_menuReady;
    [_menuIsComplete unlock];
    if (timedOut) {
        // Drop this connection so a late response cannot become the next menu.
        [self.localSocketClient restart];
        return nil;
    }

    id contextMenuTitle = [_strings objectForKey:@"CONTEXT_MENU_TITLE"];
    if (contextMenuTitle && !onlyRootsSelected && menuItems.count > 0) {
        NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
        NSMenu *subMenu = [[NSMenu alloc] initWithTitle:@""];
        NSMenuItem *subMenuItem = [menu addItemWithTitle:contextMenuTitle action:nil keyEquivalent:@""];
        subMenuItem.submenu = subMenu;
        subMenuItem.image = [[NSBundle mainBundle] imageForResource:@"app.icns"];

        for (NSDictionary *item in menuItems) {
            NSMenuItem *actionItem = [subMenu addItemWithTitle:[item valueForKey:@"text"]
                                                        action:@selector(subMenuActionClicked:)
                                                 keyEquivalent:@""];
            actionItem.representedObject = @{@"command" : item[@"command"], @"paths" : paths};
            [actionItem setTarget:self];
            NSString *flags = [item valueForKey:@"flags"]; // e.g. "d"
            if ([flags rangeOfString:@"d"].location != NSNotFound) {
                [actionItem setEnabled:false];
            }
        }
        return menu;
    }
    return nil;
}

- (void)subMenuActionClicked:(id)sender
{
    NSDictionary *action = [(NSMenuItem *)sender representedObject];
    [self.localSocketClient askOnSocket:action[@"paths"] query:action[@"command"]];
}

#pragma mark - Helper methods

/// Get team identifier from code signing information
- (NSString *)getTeamIdentifierFromSigning
{
    SecStaticCodeRef staticCode = NULL;
    NSURL *bundleURL = [[NSBundle bundleForClass:[self class]] bundleURL];

    OSStatus status = SecStaticCodeCreateWithPath((__bridge CFURLRef)bundleURL, kSecCSDefaultFlags, &staticCode);
    if (status != errSecSuccess || !staticCode) {
        return nil;
    }

    CFDictionaryRef signingInfo = NULL;
    status = SecCodeCopySigningInformation(staticCode, kSecCSSigningInformation, &signingInfo);
    CFRelease(staticCode);

    if (status != errSecSuccess || !signingInfo) {
        return nil;
    }

    NSString *teamId = nil;
    CFStringRef teamIdentifier = CFDictionaryGetValue(signingInfo, kSecCodeInfoTeamIdentifier);
    if (teamIdentifier) {
        teamId = (__bridge NSString *)teamIdentifier;
    }
    CFRelease(signingInfo);
    return teamId;
}

#pragma mark - SyncClientDelegate implementation

- (void)setResult:(NSString *)result forPath:(NSString *)path
{
    NSString *const normalizedPath = path.decomposedStringWithCanonicalMapping;
    NSURL *const urlForPath = [NSURL fileURLWithPath:normalizedPath];
    if (urlForPath == nil) {
        return;
    }
    [FIFinderSyncController.defaultController setBadgeIdentifier:result forURL:urlForPath];
}

- (void)reFetchFileNameCacheForPath:(NSString *)path
{
    // Not implemented - could trigger a refresh of the Finder view if needed
}

- (void)registerPath:(NSString *)path
{
    NSLog(@"FinderSync: Registering path %@", path);
    [_registeredDirectories addObject:[NSURL fileURLWithPath:path]];
    [FIFinderSyncController defaultController].directoryURLs = _registeredDirectories;
}

- (void)unregisterPath:(NSString *)path
{
    NSLog(@"FinderSync: Unregistering path %@", path);
    [_registeredDirectories removeObject:[NSURL fileURLWithPath:path]];
    [FIFinderSyncController defaultController].directoryURLs = _registeredDirectories;
}

- (void)setString:(NSString *)key value:(NSString *)value
{
    [_strings setObject:value forKey:key];
}

- (void)resetMenuItems
{
    [_menuIsComplete lock];
    _menuItems = [NSMutableArray array];
    [_menuIsComplete unlock];
}

- (void)addMenuItem:(NSDictionary *)item
{
    [_menuIsComplete lock];
    if (_menuRequestPending) {
        [_menuItems addObject:item];
    }
    [_menuIsComplete unlock];
}

- (void)menuHasCompleted
{
    // Signal that the menu is ready
    [_menuIsComplete lock];
    _menuReady = YES;
    _menuRequestPending = NO;
    [_menuIsComplete signal];
    [_menuIsComplete unlock];
}

- (void)connectionDidOpen
{
    [self.localSocketClient askOnSocket:@"" query:@"GET_STRINGS"];
}

- (void)connectionDidDie
{
    [_menuIsComplete lock];
    _menuItems = [NSMutableArray array];
    _menuReady = YES;
    _menuRequestPending = NO;
    [_menuIsComplete signal];
    [_menuIsComplete unlock];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"FinderSync: Connection to main app died");
        [self->_strings removeAllObjects];
        [self->_registeredDirectories removeAllObjects];
        // For some reason the FIFinderSync cache doesn't seem to be cleared for the root item when
        // we reset the directoryURLs (seen on macOS 10.12 at least).
        // First setting it to the FS root and then setting it to nil seems to work around the issue.
        [FIFinderSyncController defaultController].directoryURLs = [NSSet setWithObject:[NSURL fileURLWithPath:@"/"]];
        // This will tell Finder that this extension isn't attached to any directory
        // until we can reconnect to the sync client.
        [FIFinderSyncController defaultController].directoryURLs = nil;
    });
}

@end
