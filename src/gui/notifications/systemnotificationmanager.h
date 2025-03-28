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

Q_SIGNALS:
    /**
     * A notification we no longer track was clicked.
     * This can be the case when a notification in the action center is clicked, that is already timed out.
     */
    void unknownNotifationClicked();

private:
    /**
     *
     * Returns an active notification or null
     */
    SystemNotification *activeNotification(quint64 id);

    SystemNotificationBackend *_backend = nullptr;
    QMap<quint64, SystemNotification *> _activeNotifications;
    friend class SystemNotificationBackend;
};
}
