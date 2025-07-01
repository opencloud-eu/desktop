// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "notifications.h"

#include "libsync/account.h"
#include "networkjobs/jsonjob.h"

using namespace OCC;

Notification::Notification() { }

Notification::Notification(const QString &title, const QString &message, const QString &id)
    : title(title)
    , message(message)
    , id(id)
{
}

bool Notification::operator==(const Notification &other) const
{
    return id == other.id;
}

JsonApiJob *Notification::dismissAllNotifications(const AccountPtr &account, const QSet<Notification> &notifications, QObject *parent)
{
    QStringList ids;
    for (const Notification &n : notifications) {
        ids.append(n.id);
    }

    return new JsonApiJob(account, account->url(), QStringLiteral("ocs/v2.php/apps/notifications/api/v1/notifications"), "DELETE",
        QJsonObject{{QStringLiteral("ids"), QJsonArray::fromStringList(ids)}}, {}, parent);
}

JsonApiJob *Notification::createNotificationsJob(const AccountPtr &account, QObject *parent)
{
    return new JsonApiJob(account, QStringLiteral("ocs/v2.php/apps/notifications/api/v1/notifications"), {}, {}, parent);
}

QSet<Notification> Notification::getNotifications(JsonApiJob *job)
{
    if (job->ocsSuccess()) {
        const auto data = job->data().value(QLatin1String("ocs")).toObject().value(QLatin1String("data")).toArray();
        QSet<Notification> notifications;
        notifications.reserve(data.size());
        for (const auto &notification : data) {
            const auto n = notification.toObject();
            notifications.insert({n.value(QLatin1String("subject")).toString(), n.value(QLatin1String("message")).toString(),
                n.value(QLatin1String("notification_id")).toString()});
        }
        return notifications;
    }
    return {};
}
