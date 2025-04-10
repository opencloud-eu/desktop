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

Q_LOGGING_CATEGORY(lcNavPane, "gui.folder.navigationpane", QtInfoMsg)

void OCC::NavigationPaneHelper::updateCloudStorageRegistry()
{
    // Start by looking at every registered namespace extension for the sidebar, and look for an "ApplicationName" value
    // that matches ours when we saved.
    const QString explorerNamespace = QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace)");
    QVector<QUuid> entriesToRemove;
    QSettings explorerNamespaceRegistry(explorerNamespace, QSettings::NativeFormat);
    for (auto &k : explorerNamespaceRegistry.childGroups()) {
        explorerNamespaceRegistry.beginGroup(k);
        if (explorerNamespaceRegistry.value("ApplicationName").toString() == Theme::instance()->appNameGUI()) {
            const auto clsid = QUuid::fromString(k);
            Q_ASSERT(!clsid.isNull());
            entriesToRemove.append(clsid);
        }
        explorerNamespaceRegistry.endGroup();
    }

    // Then re-save every account that with hasDefaultSyncRoot to the registry.
    for (const auto &acc : AccountManager::instance()->accounts()) {
        if (acc->account()->hasDefaultSyncRoot()) {
            // If it already exists, unmark it for removal, this is a valid sync root.
            entriesToRemove.removeOne(acc->account()->uuid());

            const QString clsidStr = acc->account()->uuid().toString();

            QString title = Theme::instance()->appNameGUI();
            // Write the account name in the sidebar only when using more than one account.
            if (AccountManager::instance()->accounts().size() > 1) {
                title = QStringLiteral("%1 - %2").arg(title, acc->account()->davDisplayName());
            };

            qCInfo(lcNavPane) << "Explorer Cloud storage provider: saving path" << acc->account()->defaultSyncRoot() << "to CLSID" << clsidStr;
            {
                QSettings settings(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Classes\CLSID\%1)").arg(clsidStr), QSettings::NativeFormat);
                // Steps taken from: https://msdn.microsoft.com/en-us/library/windows/desktop/dn889934%28v=vs.85%29.aspx
                // Step 1: Add your CLSID and name your extension
                settings.setValue("Default", title);
                // Step 2: Set the image for your icon
                settings.setValue("DefaultIcon/Default", QDir::toNativeSeparators(qApp->applicationFilePath()));
                // Step 3: Add your extension to the Navigation Pane and make it visible
                settings.setValue("System.IsPinnedToNameSpaceTree", 0x1);
                // Step 4: Set the location for your extension in the Navigation Pane
                settings.setValue("SortOrderIndex", 0x42);
                // Step 5: Provide the dll that hosts your extension.
                // QSettings doesn't support REG_EXPAND_SZ
                // our workaround here is to expand %systemroot%\system32\shell32.dll ourselves
                settings.setValue("InProcServer32/Default", QStringLiteral(R"(%1\system32\shell32.dll)").arg(qEnvironmentVariable("SYSTEMROOT")));
                // Step 6: Define the instance object
                // Indicate that your namespace extension should function like other file folder structures in File Explorer.
                settings.setValue("Instance/CLSID", QStringLiteral("{0E5AAE11-A475-4c5b-AB00-C66DE400274E}"));
                // Step 7: Provide the file system attributes of the target folder
                settings.setValue("Instance/InitPropertyBag/Attributes", 0x11);
                // Step 8: Set the path for the sync root
                settings.setValue("Instance/InitPropertyBag/TargetFolderPath", QDir::toNativeSeparators(acc->account()->defaultSyncRoot()));
                // Step 9: Set appropriate shell flags
                settings.setValue("ShellFolder/FolderValueFlags", 0x28);
                // Step 10: Set the appropriate flags to control your shell behavior
                settings.setValue("ShellFolder/Attributes", 0xF080004D);
            }
            {
                // Hide your extension from the Desktop
                QSettings newStartPanel(
                    QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel\%1)").arg(clsidStr),
                    QSettings::NativeFormat);
                newStartPanel.setValue("Default", 0x1);
            }

            // For us, to later be able to iterate and find our own namespace entries and associated CLSID.
            explorerNamespaceRegistry.beginGroup(clsidStr);
            explorerNamespaceRegistry.setValue("Default", title);
            explorerNamespaceRegistry.setValue("ApplicationName", Theme::instance()->appNameGUI());
            explorerNamespaceRegistry.endGroup();
        }
    }

    // Then remove anything that isn't in our folder list anymore.
    for (const auto &clsid : std::as_const(entriesToRemove)) {
        const QString clsidStr = clsid.toString();
        qCInfo(lcNavPane) << "Explorer Cloud storage provider: now unused, removing own CLSID" << clsid;
        auto removeGroup = [](const QString &key) {
            qCDebug(lcNavPane) << "Removing" << key;
            QSettings settings(key, QSettings::NativeFormat);
            settings.remove(QString());
        };
        removeGroup(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace\%1)").arg(clsidStr));
        removeGroup(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Classes\CLSID\%1)").arg(clsidStr));
        removeGroup(QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel\%1)").arg(clsidStr));
    }
}
