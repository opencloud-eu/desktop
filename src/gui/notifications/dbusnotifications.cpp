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

    QMap<quint32, quint64> _idMap;
};


DBusNotifications::DBusNotifications(SystemNotificationManager *parent)
    : SystemNotificationBackend(parent)
    , d_ptr(new DBusNotificationsPrivate(this))

{
    Q_D(DBusNotifications);
    connect(&d->dbusInterface, &org::freedesktop::Notifications::ActionInvoked, this, [this](uint systemId, const QString &actionKey) {
        Q_D(DBusNotifications);
        // we get called for all global notifications, only handle ours
        if (auto id = Utility::optionalFind(d->_idMap, systemId)) {
            qCDebug(lcDbusNotification) << u"ActionInvoked" << id->value() << u"SystemId" << systemId << actionKey;
            if (auto *notification = activeNotification(id->value())) {
                const qsizetype index = actionKey.toLongLong();
                if (index < notification->request().buttons().size()) {
                    Q_EMIT notification->buttonClicked(notification->request().buttons().at(index));
                } else {
                    qCDebug(lcDbusNotification) << actionKey << u"is out of range";
                }
            }
        }
    });

    connect(&d->dbusInterface, &org::freedesktop::Notifications::NotificationClosed, this, [this](uint systemId, uint reason) {
        Q_D(DBusNotifications);
        // we get called for all global notifications, only handle ours
        if (auto id = Utility::optionalFind(d->_idMap, systemId)) {
            d->_idMap.erase(*id);
            if (auto *notification = activeNotification(id->value())) {
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
                    qCWarning(lcDbusNotification) << u"Unsupported close reason" << reason;
                    break;
                }
                qCDebug(lcDbusNotification) << u"NotificationClosed" << id->value() << u"SystemId" << systemId << reason << result;
                finishNotification(notification, result);
            } else {
                qCDebug(lcDbusNotification) << u"Unknown NotificationClicked" << id->value() << u"SystemId" << systemId << reason;
                Q_EMIT systemNotificationManager() -> unknownNotificationClicked();
            }
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

    qCDebug(lcDbusNotification) << u"Creating system notification" << notificationRequest.id();
    const auto reply = d->dbusInterface.Notify(Theme::instance()->appNameGUI(), 0, Resources::iconToFileSystemUrl(qGuiApp->windowIcon()).toString(),
        notificationRequest.title(), notificationRequest.text(), actionList, hints, -1);

    auto *watcher = new QDBusPendingCallWatcher(reply, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [id = notificationRequest.id(), this](QDBusPendingCallWatcher *watcher) {
        Q_D(DBusNotifications);
        watcher->deleteLater();

        const auto systemID = QDBusPendingReply<uint>(*watcher).argumentAt<0>();
        qCDebug(lcDbusNotification) << u"System notification" << id << u"was assigned the system notification id" << systemID;
        d->_idMap.insert(systemID, id);
    });
}
