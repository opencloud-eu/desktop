// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>


#pragma once
#include "gui/notifications/systemnotificationbackend.h"
#include <QObject>

class QLocalServer;

namespace OCC {
class SystemNotificationRequest;

class SnoreToast : public SystemNotificationBackend
{
    Q_OBJECT
public:
    explicit SnoreToast(SystemNotificationManager *parent);

    bool isReady() const override;
    void notify(const SystemNotificationRequest &notificationRequest) override;

private:
    QString _snoreSystemPath;
    QLocalServer *_server;
};

}
