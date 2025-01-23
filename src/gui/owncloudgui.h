/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#pragma once
#include "account.h"
#include "gui/opencloudguilib.h"
#include "progressdispatcher.h"
#include "systray.h"

#include <QObject>
#include <QPointer>
#include <QAction>
#include <QMenu>
#include <QSize>
#include <QTimer>

namespace OCC {

namespace Wizard {
    class SetupWizardController;
}
class Folder;

class AboutDialog;
class SettingsDialog;
class ShareDialog;
class Application;
class LogBrowser;

/**
 * @brief The ownCloudGui class
 * @ingroup gui
 */
class OPENCLOUD_GUI_EXPORT ownCloudGui : public QObject
{
    Q_OBJECT
public:
    explicit ownCloudGui(Application *parent = nullptr);
    ~ownCloudGui() override;

    bool checkAccountExists(bool openSettings);

    /**
     * Raises our main Window to the front with the raiseWidget in focus.
     * If raiseWidget is a dialog and not visible yet, ->open will be called.
     * For normal widgets we call showNormal.
     */
    static void raise();

    /// Whether the tray menu is visible
    bool contextMenuVisible() const;

    void hideAndShowTray();

    SettingsDialog *settingsDialog() const;

    void runNewAccountWizard();

Q_SIGNALS:
    void setupProxy();

public Q_SLOTS:
    void setupContextMenu();
    void updateContextMenu();
    void updateContextMenuNeeded();
    void slotContextMenuAboutToShow();
    void slotContextMenuAboutToHide();
    void slotComputeOverallSyncStatus();
    void slotShowTrayMessage(const QString &title, const QString &msg, const QIcon &icon = {});
    void slotShowOptionalTrayMessage(const QString &title, const QString &msg, const QIcon &icon = {});
    void slotFolderOpenAction(Folder *f);
    void slotRebuildRecentMenus();
    void slotUpdateProgress(Folder *folder, const ProgressInfo &progress);
    void slotFoldersChanged();
    void slotShowSettings();
    void slotShowSyncProtocol();
    void slotShutdown();
    void slotSyncStateChange(Folder *);
    void slotTrayClicked(QSystemTrayIcon::ActivationReason reason);
    void slotToggleLogBrowser();
    void slotOpenSettingsDialog();
    void slotHelp();
    void slotAbout();
    void slotOpenPath(const QString &path);
    void slotAccountStateChanged();
    void slotTrayMessageIfServerUnsupported(Account *account);

private:
    void setPauseOnAllFoldersHelper(const QList<AccountStatePtr> &accounts, bool pause);
    void setupActions();
    void addAccountContextMenu(AccountStatePtr accountState, QMenu *menu);

    Systray *_tray;
    SettingsDialog *_settingsDialog;
    // tray's menu
    QScopedPointer<QMenu> _contextMenu;

    // Manually tracking whether the context menu is visible via aboutToShow
    // and aboutToHide. Unfortunately aboutToHide isn't reliable everywhere
    // so this only gets used with _workaroundManualVisibility (when the tray's
    // isVisible() is unreliable)
    bool _contextMenuVisibleManual = false;

    QMenu *_recentActionsMenu;
    QVector<QMenu *> _accountMenus;
    bool _workaroundShowAndHideTray = false;
    bool _workaroundNoAboutToShowUpdate = false;
    bool _workaroundFakeDoubleClick = false;
    bool _workaroundManualVisibility = false;
    QTimer _delayedTrayUpdateTimer;
    QPointer<ShareDialog> _shareDialog;

    QAction *_actionStatus;

    QList<QAction *> _recentItemsActions;
    Application *_app;

    // keeping a pointer on those dialogs allows us to make sure they will be shown only once
    QPointer<Wizard::SetupWizardController> _wizardController;
    QPointer<AboutDialog> _aboutDialog;
};

} // namespace OCC
