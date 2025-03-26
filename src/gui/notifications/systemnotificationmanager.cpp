// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/systemnotificationmanager.h"

#include "gui/application.h"
#include "gui/notifications/systemnotificationbackend.h"

#ifdef WITH_SNORE_TOAST
#include "gui/notifications/snoretoast.h"
#endif

using namespace OCC;

SystemNotificationManager::SystemNotificationManager(QObject *parent)
    : QObject(parent)
#if defined(WITH_SNORE_TOAST)
    , _backend(new SnoreToast(this))
#endif
{
}

SystemNotification *SystemNotificationManager::notify(SystemNotificationRequest &&notification)
{
    if (_backend && _backend->isReady()) {
        auto *n = new SystemNotification(std::move(notification), this);
        _activeNotifications.insert(n->request().id(), n);
        connect(n, &SystemNotification::destroyed, this, [n, this]() { _activeNotifications.remove(n->request().id()); });
        _backend->notify(n->request());
        return n;
    } else {
        // the result is not directly connected to a specific notification
        connect(ocApp()->systemTrayIcon(), &QSystemTrayIcon::messageClicked, this, &SystemNotificationManager::unknownNotifationClicked, Qt::UniqueConnection);
        ocApp()->systemTrayIcon()->showMessage(notification.title(), notification.text(), notification.icon());
        return nullptr;
    }
}

SystemNotification *SystemNotificationManager::notification(quint64 id)
{
    return _activeNotifications.value(id);
}
