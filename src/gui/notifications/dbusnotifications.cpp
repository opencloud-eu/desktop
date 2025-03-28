// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/dbusnotifications.h"

#include "libsync/theme.h"

#include <QDBusInterface>
#include <QDBusMessage>

using namespace OCC;

namespace {
auto NOTIFICATIONS_SERVICE_C()
{
    return QStringLiteral("org.freedesktop.Notifications");
}

auto NOTIFICATIONS_PATH_C()
{
    return QStringLiteral("/org/freedesktop/Notifications");
}

auto NOTIFICATIONS_IFACE_C()
{
    return QStringLiteral("org.freedesktop.Notifications");
}
}

DBusNotifications::DBusNotifications(SystemNotificationManager *parent)
    : SystemNotificationBackend(parent)
{
}

bool DBusNotifications::isReady() const
{
    return QDBusInterface(NOTIFICATIONS_SERVICE_C(), NOTIFICATIONS_PATH_C(), NOTIFICATIONS_IFACE_C()).isValid();
}

void DBusNotifications::notify(const SystemNotificationRequest &notificationRequest)
{
    QList<QVariant> args = QList<QVariant>() << Theme::instance()->appNameGUI() << quint32(0) << Theme::instance()->applicationIconName()
                                             << notificationRequest.title() << notificationRequest.text() << QStringList() << QVariantMap() << qint32(-1);
    QDBusMessage method = QDBusMessage::createMethodCall(NOTIFICATIONS_SERVICE_C(), NOTIFICATIONS_PATH_C(), NOTIFICATIONS_IFACE_C(), QStringLiteral("Notify"));
    method.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(method);
}
