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
