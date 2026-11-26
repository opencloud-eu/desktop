// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/opencloudsynclib.h"

#include <QSettings>

namespace OCC {
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
