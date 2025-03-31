// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "gui/notifications/dbusnotifications.h"
#include "gui/dbusnotifications_interface.h"

#include "libsync/theme.h"

#include <QPixmap>

#include "application.h"
#include "systemnotification.h"
#include "systemnotificationmanager.h"

Q_LOGGING_CATEGORY(lcDbusNotification, "gui.notifications.dbus", QtInfoMsg)

using namespace OCC;


class OCC::DBusNotificationsPrivate
{
public:
    DBusNotificationsPrivate(DBusNotifications *q)
        : q_ptr(q)
        , dbusInterface(org::freedesktop::Notifications(
              QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"), QDBusConnection::sessionBus()))
    {
    }

    ~DBusNotificationsPrivate() { }

private:
    Q_DECLARE_PUBLIC(DBusNotifications)
    DBusNotifications *q_ptr;

    org::freedesktop::Notifications dbusInterface;
};


DBusNotifications::DBusNotifications(SystemNotificationManager *parent)
    : SystemNotificationBackend(parent)
    , d_ptr(new DBusNotificationsPrivate(this))

{
    Q_D(DBusNotifications);
    connect(&d->dbusInterface, &org::freedesktop::Notifications::ActionInvoked, this, [this](uint id, const QString &actionKey) {
        qCDebug(lcDbusNotification) << "ActionInvoked" << id << actionKey;
        if (auto *notification = activeNotification(id)) {
            const qsizetype index = actionKey.toLongLong();
            if (index < notification->request().buttons().size()) {
                Q_EMIT notification->buttonClicked(notification->request().buttons().at(index));
            } else {
                qCDebug(lcDbusNotification) << actionKey << "is out of range";
            }
        }
    });

    connect(&d->dbusInterface, &org::freedesktop::Notifications::NotificationClosed, this, [this](uint id, uint reason) {
        qCDebug(lcDbusNotification) << "NotificationClosed" << id << reason;
        if (auto *notification = activeNotification(id)) {
            SystemNotification::Result result;
            switch (reason) {
            case 1:
                result = SystemNotification::Result::TimedOut;
                break;
            case 2:
                result = SystemNotification::Result::Dismissed;
                break;
            default:
                result = SystemNotification::Result::Dismissed;
                qCWarning(lcDbusNotification) << "Unsupported close reason" << reason;
                break;
            }
            systemNotificationManager()->notificationFinished(notification, result);
        } else {
            Q_EMIT systemNotificationManager() -> unknownNotifationClicked();
        }
    });
}
DBusNotifications::~DBusNotifications()
{
    Q_D(DBusNotifications);
    delete d;
}

bool DBusNotifications::isReady() const
{
    Q_D(const DBusNotifications);
    return d->dbusInterface.isValid();
}

void DBusNotifications::notify(const SystemNotificationRequest &notificationRequest)
{
    Q_D(DBusNotifications);
    QVariantMap hints{{QStringLiteral("image-path"), Resources::iconToFileSystemUrl(notificationRequest.icon()).toString()}};
    const QString desktopFileName = QGuiApplication::desktopFileName();
    if (!desktopFileName.isEmpty()) {
        hints[QStringLiteral("desktop-entry")] = desktopFileName;
    }
    QStringList actionList;
    for (size_t id = 0; const QString &action : notificationRequest.buttons()) {
        actionList.append(QString::number(id++));
        actionList.append(action);
    }
    d->dbusInterface.Notify(Theme::instance()->appNameGUI(), notificationRequest.id(), Resources::iconToFileSystemUrl(qGuiApp->windowIcon()).toString(),
        notificationRequest.title(), notificationRequest.text(), actionList, hints, -1);
}
