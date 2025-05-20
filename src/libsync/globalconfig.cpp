// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/globalconfig.h"

#include "libsync/theme.h"

#include <QSettings>

using namespace OCC;

QUrl GlobalConfig::serverUrl()
{
    return getValue("Wizard/ServerUrl").toUrl();
}

QVariant GlobalConfig::getValue(QAnyStringView param, const QVariant &defaultValue)
{
    static const QSettings systemSettings = {
#ifdef Q_OS_MAC
        QStringLiteral("/Library/Preferences/%1.plist").arg(Theme::instance()->orgDomainName()), QSettings::NativeFormat
#elif defined(Q_OS_UNIX)
        QStringLiteral("/etc/%1/%1.conf").arg(Theme::instance()->appName()), QSettings::NativeFormat
#elif defined(Q_OS_WIN)
        QStringLiteral(R"(HKEY_LOCAL_MACHINE\Software\%1\%2)").arg(Theme::instance()->vendor(), Theme::instance()->appNameGUI()), QSettings::NativeFormat
#else
#error "Unsupported platform"
#endif
    };
    return systemSettings.value(param, defaultValue);
}

#ifdef Q_OS_WIN
QVariant GlobalConfig::getPolicySetting(QAnyStringView setting, const QVariant &defaultValue)
{
    // check for policies first and return immediately if a value is found.
    QVariant out = QSettings(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Policies\%1\%2)").arg(Theme::instance()->vendor(), Theme::instance()->appNameGUI()),
        QSettings::NativeFormat)
                       .value(setting);

    if (!out.isValid()) {
        out = QSettings(QStringLiteral(R"(HKEY_LOCAL_MACHINE\Software\Policies\%1\%2)").arg(Theme::instance()->vendor(), Theme::instance()->appNameGUI()),
            QSettings::NativeFormat)
                  .value(setting, defaultValue);
    }
    return out;
}
#endif
