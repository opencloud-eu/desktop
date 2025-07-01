// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "systemnotification.h"

using namespace OCC;
namespace {
quint64 nextNotificationId()
{
    static quint64 nextId = 0;
    return nextId++;
}
}

SystemNotificationRequest::SystemNotificationRequest(const QString &title, const QString &text, const QIcon &icon)
    : _title(title)
    , _text(text)
    , _icon(icon)
    , _id(nextNotificationId())
{
    Q_ASSERT(!icon.isNull());
}

QString SystemNotificationRequest::title() const
{
    return _title;
}

QString SystemNotificationRequest::text() const
{
    return _text;
}

QIcon SystemNotificationRequest::icon() const
{
    return _icon;
}

quint64 SystemNotificationRequest::id() const
{
    return _id;
}

void SystemNotificationRequest::setButtons(const QStringList &buttons)
{
    _buttons = buttons;
}

const QStringList &SystemNotificationRequest::buttons() const
{
    return _buttons;
}

SystemNotification::SystemNotification(SystemNotificationRequest &&notificationRequest, QObject *parent)
    : QObject(parent)
    , _notificationRequest(notificationRequest)
{
}

const SystemNotificationRequest &SystemNotification::request() const
{
    return _notificationRequest;
}
