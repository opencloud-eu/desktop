// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once
#include "gui/notifications/systemnotificationbackend.h"

namespace OCC {
class SystemNotificationRequest;
class MacNotificationsPrivate;

class MacNotifications : public SystemNotificationBackend
{
    Q_OBJECT
public:
    explicit MacNotifications(SystemNotificationManager *parent);
    ~MacNotifications();

    bool isReady() const override;
    void notify(const SystemNotificationRequest &notificationRequest) override;

private:
    Q_DECLARE_PRIVATE(MacNotifications)
    MacNotificationsPrivate *d_ptr;
};

}
