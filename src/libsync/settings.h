// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/opencloudsynclib.h"

#include <QLoggingCategory>
#include <QSettings>

namespace OCC {
OPENCLOUD_SYNC_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcSettings)

    class OPENCLOUD_SYNC_EXPORT Settings : public QSettings
{
public:
    Settings(const QString &fileName, Format format);
    ~Settings() override;

    bool syncAndLog();

private:
    bool _errorLogged = false;
};
}
