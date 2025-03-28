// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>


#pragma once
#include "gui/notifications/systemnotificationbackend.h"


namespace OCC {
class SystemNotificationRequest;

class DBusNotifications : public SystemNotificationBackend
{
    Q_OBJECT
public:
    DBusNotifications(SystemNotificationManager *parent);

    bool isReady() const override;
    void notify(const SystemNotificationRequest &notificationRequest) override;
};
}
