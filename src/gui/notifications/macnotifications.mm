// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/macnotifications.h"

#include "gui/notifications/systemnotification.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Foundation/NSString.h>
#import <Foundation/NSUserNotification.h>
#import <dispatch/dispatch.h>


namespace {
auto iconSizeC = 64;
}

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
class MacNotificationsPrivate
{
public:
    MacNotificationsPrivate(MacNotifications *q)
        : q_ptr(q)
    {
        _delegate = [[OurDelegate alloc] init];
        [[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:_delegate];
    }

    ~MacNotificationsPrivate() { [_delegate release]; }

private:
    OurDelegate *_delegate;

    Q_DECLARE_PUBLIC(MacNotifications)
    MacNotifications *q_ptr;
};

MacNotifications::MacNotifications(SystemNotificationManager *parent)
    : SystemNotificationBackend(parent)
    , d_ptr(new MacNotificationsPrivate(this))
{
}

MacNotifications::~MacNotifications()
{
    Q_D(MacNotifications);
    delete d;
}

bool MacNotifications::isReady() const
{
    return true;
}

void MacNotifications::notify(const SystemNotificationRequest &notificationRequest)
{
    @autoreleasepool {
        NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
        [notification setTitle:notificationRequest.title().toNSString()];
        [notification setInformativeText:notificationRequest.text().toNSString()];
        [notification setContentImage:[[NSImage alloc] initWithCGImage:notificationRequest.icon().pixmap(iconSizeC).toImage().toCGImage()
                                                                  size:NSMakeSize(iconSizeC, iconSizeC)]];
        [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
    }
}


} // namespace OCC
