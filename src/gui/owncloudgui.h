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
#include "systray.h"

#include <QMenu>
#include <QObject>
#include <QPointer>

namespace OCC {

namespace Wizard {
    class SetupWizardController;
}
class Folder;

class AboutDialog;
class SettingsDialog;
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

    /**
     * Raises our main Window to the front with the raiseWidget in focus.
     * If raiseWidget is a dialog and not visible yet, ->open will be called.
     * For normal widgets we call showNormal.
     */
    static void raise();

    void hideAndShowTray();

    SettingsDialog *settingsDialog() const;

    void runNewAccountWizard();

Q_SIGNALS:
    void setupProxy();

public Q_SLOTS:
    void slotComputeOverallSyncStatus();
    void slotShowTrayMessage(const QString &title, const QString &msg, const QIcon &icon = {});
    void slotShowOptionalTrayMessage(const QString &title, const QString &msg, const QIcon &icon = {});
    void slotFoldersChanged();
    void slotShowSettings();
    void slotShowSyncProtocol();
    void slotShutdown();
    void slotTrayClicked(QSystemTrayIcon::ActivationReason reason);
    void slotToggleLogBrowser();
    void slotOpenSettingsDialog();
    void slotAbout();
    void slotOpenPath(const QString &path);
    void slotAccountStateChanged();
    void slotTrayMessageIfServerUnsupported(Account *account);

private:
    void setPauseOnAllFoldersHelper(const QList<AccountStatePtr> &accounts, bool pause);

    void updateContextMenu();

    Systray *_tray;
    SettingsDialog *_settingsDialog;


    Application *_app;


    // keeping a pointer on those dialogs allows us to make sure they will be shown only once
    QPointer<Wizard::SetupWizardController> _wizardController;
    QPointer<AboutDialog> _aboutDialog;
};

} // namespace OCC
