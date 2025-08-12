// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>
// SPDX-FileCopyrightText: Jocelyn Turcotte <jturcotte@woboq.com>

#include "gui/navigationpanehelper.h"
#include "gui/accountmanager.h"
#include "gui/accountstate.h"
#include "libsync/theme.h"

#include <QCoreApplication>
#include <QDir>

using namespace OCC;
using namespace Qt::Literals::StringLiterals;

Q_LOGGING_CATEGORY(lcNavPane, "gui.folder.navigationpane", QtInfoMsg)

void OCC::NavigationPaneHelper::removeLegacyCloudStorageRegistry()
{
    // TODO: remove in upcoming version, originally removed in 3.0
    // Start by looking at every registered namespace extension for the sidebar, and look for an "ApplicationName" value
    // that matches ours when we saved.

    auto removeGroup = [](const QString &key) {
        qCDebug(lcNavPane) << u"Removing" << key;
        QSettings settings(key, QSettings::NativeFormat);
        settings.remove(QString());
    };

    const auto groupKey = uR"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace)"_s;
    QSettings explorerNamespaceRegistry(groupKey, QSettings::NativeFormat);
    for (auto &clsi : explorerNamespaceRegistry.childGroups()) {
        const auto key = uR"(%1\%2)"_s.arg(groupKey, clsi);
        if (explorerNamespaceRegistry.value(uR"(%1\ApplicationName)"_s.arg(key)).toString() != Theme::instance()->appNameGUI()) {
            continue;
        }
        Q_ASSERT(!QUuid::fromString(clsi).isNull());
        removeGroup(key);
        removeGroup(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Classes\CLSID\%1)").arg(clsi));
        removeGroup(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel\%1)").arg(clsi));
    }
}
