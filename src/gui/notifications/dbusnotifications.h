// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>


#pragma once
#include "gui/notifications/systemnotificationbackend.h"

namespace OCC {
class SystemNotificationRequest;
class DBusNotificationsPrivate;

// https://specifications.freedesktop.org/notification-spec/latest/protocol.html
class DBusNotifications : public SystemNotificationBackend
{
    Q_OBJECT
public:
    DBusNotifications(SystemNotificationManager *parent);
    ~DBusNotifications();

    bool isReady() const override;
    void notify(const SystemNotificationRequest &notificationRequest) override;

private:
    Q_DECLARE_PRIVATE(DBusNotifications)
    DBusNotificationsPrivate *d_ptr;
};
}
