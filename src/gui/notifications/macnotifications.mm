// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/macnotifications.h"

#include "gui/notifications/systemnotification.h"

#import <Foundation/NSString.h>
#import <Foundation/NSUserNotification.h>
#import <dispatch/dispatch.h>

@interface OurDelegate : NSObject <NSUserNotificationCenterDelegate>

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification;

@end

@implementation OurDelegate

// Always show, even if app is active at the moment.
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification
{
    Q_UNUSED(center)
    Q_UNUSED(notification)

    return YES;
}

@end

namespace OCC {

MacNotifications::MacNotifications(SystemNotificationManager *parent)
    : SystemNotificationBackend(parent)
{
    _delegate = [[OurDelegate alloc] init];
    [[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:static_cast<OurDelegate *>(_delegate)];
}
MacNotifications::~MacNotifications()
{
    [static_cast<OurDelegate *>(_delegate) release];
}

bool MacNotifications::isReady() const
{
    return true;
}

void MacNotifications::notify(const SystemNotificationRequest &notificationRequest)
{
    @autoreleasepool {
        NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
        [notification setTitle:[NSString stringWithUTF8String:notificationRequest.title().toUtf8().data()]];
        [notification setInformativeText:[NSString stringWithUTF8String:notificationRequest.text().toUtf8().data()]];
        [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
    }
}


} // namespace OCC
