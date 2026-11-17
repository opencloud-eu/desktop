// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/settings.h"

#include <QLoggingCategory>

using namespace OCC;

Q_LOGGING_CATEGORY(lcSettings, "sync.settings", QtInfoMsg)

Settings::Settings(const QString &fileName, Format format)
    : QSettings(fileName, format)
{
}

Settings::~Settings()
{
    syncAndLog();
}

bool Settings::syncAndLog()
{
    sync();
    if (status() == NoError) {
        return true;
    }
    if (!_errorLogged) {
        qCWarning(lcSettings) << u"Failed to access settings" << fileName() << status();
        _errorLogged = true;
    }
    return false;
}
