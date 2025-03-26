// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "gui/notifications/systemnotification.h"


#include <QMap>
#include <QObject>

namespace OCC {
class SystemNotificationBackend;

class SystemNotificationManager : public QObject
{
    Q_OBJECT
public:
    SystemNotificationManager(QObject *parent);

    /**
     * Returns a SystemNotifcaion or null, the caller hast to take ownership
     */
    SystemNotification *notify(SystemNotificationRequest &&notification);

    /**
     *
     * Returns an active notification or null
     */
    SystemNotification *notification(quint64 id);

Q_SIGNALS:
    /**
     * A notification we no longer track was clicked.
     * This can be the case when a notification in the action center is clicked, that is already timed out.
     */
    void unknownNotifationClicked();

private:
    SystemNotificationBackend *_backend = nullptr;
    QMap<quint64, SystemNotification *> _activeNotifications;
};
}
