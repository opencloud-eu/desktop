/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
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

#include "gui/opencloudguilib.h"

#include "common/asserts.h"
#include "libsync/accountfwd.h"

#include <QPointer>
#include <QSystemTrayIcon>

namespace CrashReporter {
class Handler;
}

namespace OCC {

class SettingsDialog;
class AboutDialog;
class Systray;
class SystemNotificationManager;

class OPENCLOUD_GUI_EXPORT Application : public QObject
{
    Q_OBJECT
public:
    static std::unique_ptr<Application> createInstance(const QString &displayLanguage, bool debugMode);
    ~Application();

    bool debugMode();

    QString displayLanguage() const;

    AccountStatePtr addNewAccount(AccountPtr newAccount);

    void showSettings();

    SettingsDialog *settingsDialog() const;

    void showAbout();

    SystemNotificationManager *systemNotificationManager() const;

    void runNewAccountWizard();

    QSystemTrayIcon *systemTrayIcon() const;

protected Q_SLOTS:
    void slotCleanup();
    void slotAccountStateAdded(AccountStatePtr accountState) const;

private:
    explicit Application(const QString &displayLanguage, bool debugMode);

    const bool _debugMode = false;
    SettingsDialog *_settingsDialog = nullptr;

    QString _displayLanguage;
    Systray *_systray;

    SystemNotificationManager *_systemNotificationManager = nullptr;

    // keeping a pointer on those dialogs allows us to make sure they will be shown only once
    QPointer<AboutDialog> _aboutDialog;

    static Application *_instance;
    friend Application *ocApp();
};

inline Application *ocApp()
{
    OC_ENFORCE(Application::_instance);
    return Application::_instance;
}

} // namespace OCC
