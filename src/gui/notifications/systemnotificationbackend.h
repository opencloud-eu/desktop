// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include <QObject>

namespace OCC {
class SystemNotificationManager;
class SystemNotificationRequest;
class SystemNotification;

class SystemNotificationBackend : public QObject
{
    Q_OBJECT
public:
    SystemNotificationBackend(SystemNotificationManager *parent = nullptr);

    SystemNotificationManager *systemNotificationManager() const;

    virtual void notify(const SystemNotificationRequest &notificationRequest) = 0;
    [[nodiscard]] virtual bool isReady() const = 0;

protected:
    SystemNotification *activeNotification(quint64 id);

private:
    SystemNotificationManager *_parent;
};
}
