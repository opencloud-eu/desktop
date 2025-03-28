// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/systemnotificationbackend.h"

#include "gui/notifications/systemnotificationmanager.h"

using namespace OCC;

SystemNotificationBackend::SystemNotificationBackend(SystemNotificationManager *parent)
    : QObject(parent)
    , _parent(parent)
{
}

SystemNotificationManager *SystemNotificationBackend::systemNotificationManager() const
{
    return _parent;
}

SystemNotification *SystemNotificationBackend::activeNotification(quint64 id)
{
    return systemNotificationManager()->activeNotification(id);
}

void SystemNotificationBackend::finishNotification(SystemNotification *notification, SystemNotification::Result result)
{
    Q_EMIT notification->finished(result);
    Q_EMIT systemNotificationManager() -> notificationFinished(notification, result);
    systemNotificationManager()->_activeNotifications.remove(notification->request().id());
    notification->deleteLater();
}
