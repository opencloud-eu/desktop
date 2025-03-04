// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/accountfwd.h"

#include <QQmlEngine>

namespace OCC {
class JsonApiJob;

class Notification
{
    Q_GADGET
    Q_PROPERTY(QString title MEMBER title CONSTANT)
    Q_PROPERTY(QString message MEMBER message CONSTANT)
    Q_PROPERTY(QString id MEMBER id CONSTANT)
    QML_VALUE_TYPE(notification)

public:
    Notification();
    Notification(const QString &title, const QString &message, const QString &id);

    QString title;
    QString message;
    QString id;

    static JsonApiJob *createNotificationsJob(const AccountPtr &account, QObject *parent);

    static QList<Notification> getNotifications(JsonApiJob *job);

    static JsonApiJob *dismissAllNotifications(const AccountPtr &account, const QList<Notification> &notifications, QObject *parent);
};

}
