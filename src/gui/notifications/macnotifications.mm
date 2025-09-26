// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/macnotifications.h"

#include "gui/notifications/systemnotification.h"
#include "gui/notifications/systemnotificationmanager.h"

#include <QLoggingCategory>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Foundation/NSString.h>
#import <Foundation/NSUserNotification.h>
#import <dispatch/dispatch.h>

Q_LOGGING_CATEGORY(lcMacNotifications, "gui.notifications.mac", QtInfoMsg)

namespace {
auto iconSizeC = 64;
}

Q_FORWARD_DECLARE_OBJC_CLASS(OurDelegate);

namespace OCC {

class MacNotificationsPrivate
{
public:
    MacNotificationsPrivate(MacNotifications *q);

    ~MacNotificationsPrivate() { [_delegate release]; }

    inline auto *activeNotification(quint64 id)
    {
        Q_Q(MacNotifications);
        return q->activeNotification(id);
    }

    inline auto *systemNotificationManager()
    {
        Q_Q(MacNotifications);
        return q->systemNotificationManager();
    }

    inline void finishNotification(SystemNotification *notification, SystemNotification::Result result)
    {
        Q_Q(MacNotifications);
        q->finishNotification(notification, result);
    }

private:
    OurDelegate *_delegate;

    Q_DECLARE_PUBLIC(MacNotifications)
    MacNotifications *q_ptr;
};
}

@interface OurDelegate : NSObject <NSUserNotificationCenterDelegate>

@property OCC::MacNotificationsPrivate *macNotifications;
@end

@implementation OurDelegate

// Always show, even if app is active at the moment.
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification
{
    Q_UNUSED(center)
    Q_UNUSED(notification)

    qCDebug(lcMacNotifications) << "userNotificationCenter" << notification.identifier;
    return YES;
}

- (void)userNotificationCenter:(NSUserNotificationCenter *)center didDeliverNotification:(NSUserNotification *)notification
{
    Q_UNUSED(center);
    qCDebug(lcMacNotifications) << "Send notification " << notification.identifier;
}

- (void)userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification;
{
    Q_UNUSED(center);
    const auto id = QString::fromNSString(notification.identifier).toULongLong();

    OCC::SystemNotification *systemNotification = _macNotifications->activeNotification(id);

    if (systemNotification) {
        OCC::SystemNotification::Result result;
        switch ([notification activationType]) {
        case NSUserNotificationActivationTypeContentsClicked:
            result = OCC::SystemNotification::Result::Clicked;
            break;
        case NSUserNotificationActivationTypeActionButtonClicked:
            if (!systemNotification->request().buttons().isEmpty()) {
                result = OCC::SystemNotification::Result::ButtonClicked;
                // TODO: get which button was clicked
                // for now, we only support one button
                Q_EMIT systemNotification->buttonClicked(systemNotification->request().buttons().first());
            } else {
                // the show button was clicked
                result = OCC::SystemNotification::Result::Clicked;
            }
            break;
        case NSUserNotificationActivationTypeAdditionalActionClicked:
            [[fallthrough]];
        case NSUserNotificationActivationTypeReplied:
            Q_UNIMPLEMENTED();
            break;
        case NSUserNotificationActivationTypeNone:
            Q_UNREACHABLE();
            break;
        }
        qCDebug(lcMacNotifications) << "Activated notification " << id << result;
        _macNotifications->finishNotification(systemNotification, result);
    } else {
        qCDebug(lcMacNotifications) << "Unknown notification activated " << id;
        Q_EMIT _macNotifications->systemNotificationManager()->unknownNotificationClicked();
    }
}


@end

namespace OCC {
MacNotificationsPrivate::MacNotificationsPrivate(MacNotifications *q)
    : q_ptr(q)
{
    _delegate = [[OurDelegate alloc] init];
    [_delegate setMacNotifications:this];
    [[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:_delegate];

    NSArray<NSUserNotification *> *deliveredNotifications = [NSUserNotificationCenter defaultUserNotificationCenter].deliveredNotifications;
    for (NSUserNotification *deliveredNotification : deliveredNotifications) {
        [[NSUserNotificationCenter defaultUserNotificationCenter] removeDeliveredNotification: deliveredNotification];
    }
}


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
    if (notificationRequest.buttons().size() > 1) {
        qCWarning(lcMacNotifications) << "Displaying more than one action button is currently not implemented";
    }

    @autoreleasepool {
        NSUserNotification *notification = [[[NSUserNotification alloc] init] autorelease];
        [notification setTitle:notificationRequest.title().toNSString()];
        [notification setInformativeText:notificationRequest.text().toNSString()];
        [notification setIdentifier: QString::number(notificationRequest.id()).toNSString()];

        [notification setContentImage:[[NSImage alloc] initWithCGImage:notificationRequest.icon().pixmap(iconSizeC).toImage().toCGImage()
                                                                  size:NSMakeSize(iconSizeC, iconSizeC)]];

        if (!notificationRequest.buttons().isEmpty()) {
            [notification setHasActionButton:true];
            [notification setActionButtonTitle:notificationRequest.buttons().first().toNSString()];
        }

        [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
    }
}


} // namespace OCC
