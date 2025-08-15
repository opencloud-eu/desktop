/*
 * Copyright (C) by Cédric Bellegarde <gnumdk@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "systray.h"

#include "common/restartmanager.h"
#include "gui/accountmanager.h"
#include "gui/application.h"
#include "gui/folderman.h"
#include "gui/networkinformation.h"
#include "libsync/theme.h"

#include <QApplication>
#include <QDesktopServices>
#include <QMenu>

namespace OCC {


Systray::Systray(QObject *parent)
    : QSystemTrayIcon(parent)
{
    connect(this, &QSystemTrayIcon::activated, this, [](QSystemTrayIcon::ActivationReason reason) {
        // Left click
        if (reason == QSystemTrayIcon::Trigger) {
            ocApp()->showSettings();
        }
    });
    connect(AccountManager::instance(), &AccountManager::accountAdded, this,
        [this](AccountStatePtr accountState) { connect(accountState.data(), &AccountState::stateChanged, this, &Systray::slotComputeOverallSyncStatus); });
    connect(FolderMan::instance(), &FolderMan::folderSyncStateChange, this, &Systray::slotComputeOverallSyncStatus);

    // init systray
    slotComputeOverallSyncStatus();
    computeContextMenu();
    show();
}

void Systray::setToolTip(const QString &tip)
{
    QSystemTrayIcon::setToolTip(tr("%1: %2").arg(Theme::instance()->appNameGUI(), tip));
}

void Systray::slotComputeOverallSyncStatus()
{
    auto getIcon = [](const SyncResult &result) { return Theme::instance()->themeTrayIcon(result); };
    auto getIconFromStatus = [getIcon](const SyncResult::Status &status) { return getIcon(SyncResult{status}); };
    bool allSignedOut = true;
    bool allPaused = true;
    QVector<AccountStatePtr> problemAccounts;

    for (const auto &a : AccountManager::instance()->accounts()) {
        if (!a->isSignedOut()) {
            allSignedOut = false;
        }
        if (!a->isConnected()) {
            problemAccounts.append(a);
        }
    }

    const auto &map = FolderMan::instance()->folders();
    for (auto *f : map) {
        if (!f->isSyncPaused()) {
            allPaused = false;
        }
    }

    if (!problemAccounts.empty()) {
        setIcon(getIconFromStatus(SyncResult::Status::Offline));
#ifdef Q_OS_WIN
        // Windows has a 128-char tray tooltip length limit.
        QStringList accountNames;
        for (const auto &a : std::as_const(problemAccounts)) {
            accountNames.append(a->account()->displayNameWithHost());
        }
        setToolTip(tr("Disconnected from %1").arg(accountNames.join(QLatin1String(", "))));
#else
        QStringList messages;
        messages.append(tr("Disconnected from accounts:"));
        for (const auto &a : std::as_const(problemAccounts)) {
            QString message = tr("Account %1").arg(a->account()->displayNameWithHost());
            if (!a->connectionErrors().empty()) {
                message += QLatin1String("\n") + a->connectionErrors().join(QLatin1String("\n"));
            }
            messages.append(message);
        }
        setToolTip(messages.join(QLatin1String("\n\n")));
#endif
        return;
    }

    if (allSignedOut) {
        setIcon(getIconFromStatus(SyncResult::Status::Offline));
        setToolTip(tr("Please sign in"));
        return;
    } else if (allPaused) {
        setIcon(getIconFromStatus(SyncResult::Paused));
        setToolTip(tr("Account synchronization is disabled"));
        return;
    }

    // display the info of the least successful sync (eg. do not just display the result of the latest sync)
    QString trayMessage;

    auto trayOverallStatusResult = FolderMan::trayOverallStatus(map);
    const QIcon statusIcon = getIcon(trayOverallStatusResult.overallStatus());
    setIcon(statusIcon);

    // create the tray blob message, check if we have an defined state
#ifdef Q_OS_WIN
    // Windows has a 128-char tray tooltip length limit.
    trayMessage = FolderMan::instance()->trayTooltipStatusString(trayOverallStatusResult.overallStatus(), false);
#else
    QStringList allStatusStrings;
    for (auto *folder : map) {
        QString folderMessage = FolderMan::trayTooltipStatusString(folder->syncResult(), folder->isSyncPaused());
        allStatusStrings += tr("Folder »%1«: %2").arg(folder->shortGuiLocalPath(), folderMessage);
    }
    trayMessage = allStatusStrings.join(QLatin1String("\n"));
#endif
    setToolTip(trayMessage);
}

void Systray::computeContextMenu()
{
    Q_ASSERT(!contextMenu());
    auto *menu = new QMenu(Theme::instance()->appNameGUI());

    menu->addAction(Theme::instance()->applicationIcon(), tr("Show %1").arg(Theme::instance()->appNameGUI()), ocApp(), &Application::showSettings);
    auto *pauseResume = new QAction(menu);
    auto updatePauseResumeAction = [pauseResume] {
        pauseResume->setText(FolderMan::instance()->scheduler()->isRunning() ? tr("Pause synchronizations") : tr("Resume synchronizations"));
    };
    connect(pauseResume, &QAction::triggered, FolderMan::instance()->scheduler(), [] {
        if (FolderMan::instance()->scheduler()->isRunning()) {
            if (auto *currentSync = FolderMan::instance()->scheduler()->currentSync()) {
                currentSync->slotTerminateSync(tr("Synchronization paused"));
            }
            FolderMan::instance()->scheduler()->stop();
        } else {
            FolderMan::instance()->scheduler()->start();
        }
    });
    connect(FolderMan::instance()->scheduler(), &SyncScheduler::isRunningChanged, pauseResume, updatePauseResumeAction);
    menu->addAction(pauseResume);
    updatePauseResumeAction();

    if (ocApp()->debugMode()) {
        menu->addSeparator();
        auto *debugMenu = menu->addMenu(QStringLiteral("Debug actions"));
        debugMenu->addAction(QStringLiteral("Crash if asserts enabled - OC_ENSURE"), ocApp(), [] {
            if (OC_ENSURE(false)) {
                Q_UNREACHABLE();
            }
        });
        debugMenu->addAction(QStringLiteral("Crash if asserts enabled - Q_ASSERT"), ocApp(), [] { Q_ASSERT(false); });
        debugMenu->addAction(QStringLiteral("Crash now - Utility::crash()"), ocApp(), [] { Utility::crash(); });
        debugMenu->addAction(QStringLiteral("Crash now - OC_ENFORCE()"), ocApp(), [] { OC_ENFORCE(false); });
        debugMenu->addAction(QStringLiteral("Crash now - qFatal"), ocApp(), [] { qFatal("la Qt fatale"); });
        debugMenu->addAction(QStringLiteral("Restart now"), ocApp(), [] { RestartManager::requestRestart(); });
        debugMenu->addSeparator();
        auto captivePortalCheckbox = debugMenu->addAction(QStringLiteral("Behind Captive Portal"));
        captivePortalCheckbox->setCheckable(true);
        captivePortalCheckbox->setChecked(NetworkInformation::instance()->isForcedCaptivePortal());
        connect(captivePortalCheckbox, &QAction::triggered, this, [](bool checked) { NetworkInformation::instance()->setForcedCaptivePortal(checked); });
        connect(NetworkInformation::instance(), &NetworkInformation::isBehindCaptivePortalChanged, captivePortalCheckbox,
            [captivePortalCheckbox] { captivePortalCheckbox->setChecked(NetworkInformation::instance()->isForcedCaptivePortal()); });
    }

    menu->addSeparator();

    if (!Theme::instance()->helpUrl().isEmpty()) {
        menu->addAction(tr("Help"), this, [] { QDesktopServices::openUrl(Theme::instance()->helpUrl()); });
    }

    menu->addAction(tr("About"), ocApp(), &Application::showAbout);

    // this action will be hidden on mac and be part of the application menu
    menu->addAction(tr("About Qt"), qApp, &QApplication::aboutQt)->setMenuRole(QAction::AboutQtRole);

    menu->addAction(tr("Quit"), ocApp(), &QApplication::quit);

    setContextMenu(menu);
}

} // namespace OCC
