/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "application.h"

#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"
#include "common/version.h"
#include "common/vfs.h"
#include "configfile.h"
#include "folder.h"
#include "folderman.h"
#include "gui/aboutdialog.h"
#include "gui/accountsettings.h"
#include "gui/fetchserversettings.h"
#include "gui/folderwizard/folderwizard.h"
#include "gui/newwizard/setupwizardcontroller.h"
#include "gui/notifications/systemnotification.h"
#include "gui/notifications/systemnotificationmanager.h"
#include "gui/systray.h"
#include "libsync/graphapi/spacesmanager.h"
#include "resources/fonticon.h"
#include "settingsdialog.h"
#include "socketapi/socketapi.h"
#include "theme.h"

#ifdef WITH_AUTO_UPDATER
#include "updater/ocupdater.h"
#endif

#if defined(Q_OS_WIN)
#include "gui/navigationpanehelper.h"
#include <qt_windows.h>
#endif

#include <QApplication>
#include <QDesktopServices>
#include <QMenuBar>

using namespace Qt::Literals::StringLiterals;
using namespace OCC;

Q_LOGGING_CATEGORY(lcApplication, "gui.application", QtInfoMsg)

namespace {

void setUpInitialSyncFolder(AccountStatePtr accountStatePtr, bool useVfs)
{
    // saves a bit of duplicate code
    auto addFolder = [accountStatePtr, useVfs](const QString &localFolder, const QUrl &davUrl, const QString &spaceId = {}, const QString &displayName = {}) {
        auto def = FolderDefinition{accountStatePtr->account()->uuid(), davUrl, spaceId, displayName};
        def.setLocalPath(localFolder);
        return FolderMan::instance()->addFolderFromWizard(accountStatePtr, std::move(def), useVfs);
    };

    auto finalize = [accountStatePtr] {
        accountStatePtr->checkConnectivity();
        FolderMan::instance()->setSyncEnabled(true);
        FolderMan::instance()->scheduleAllFolders();
    };

    QObject::connect(
        accountStatePtr->account()->spacesManager(), &GraphApi::SpacesManager::ready, accountStatePtr,
        [accountStatePtr, addFolder, finalize] {
            auto spaces = accountStatePtr->account()->spacesManager()->spaces();
            // we do not want to set up folder sync connections for disabled spaces (#10173)
            spaces.erase(std::remove_if(spaces.begin(), spaces.end(), [](auto *space) { return space->disabled(); }), spaces.end());

            if (!spaces.isEmpty()) {
                const QString localDir(accountStatePtr->account()->defaultSyncRoot());
                FileSystem::setFolderMinimumPermissions(localDir);
                Utility::setupFavLink(localDir);
                for (const auto *space : spaces) {
                    const QString name = space->displayName();
                    const QString folderName = FolderMan::instance()->findGoodPathForNewSyncFolder(
                        localDir, name, FolderMan::NewFolderType::SpacesFolder, accountStatePtr->account()->uuid());
                    auto folder = addFolder(folderName, QUrl(space->drive().getRoot().getWebDavUrl()), space->drive().getRoot().getId(), name);
                    folder->setPriority(space->priority());
                }
                finalize();
            }
        },
        Qt::SingleShotConnection);
    accountStatePtr->account()->spacesManager()->checkReady();
}
}

QString Application::displayLanguage() const
{
    return _displayLanguage;
}

Application *Application::_instance = nullptr;

Application::Application(const QString &displayLanguage, bool debugMode)
    : _debugMode(debugMode)
    , _displayLanguage(displayLanguage)
{
    // ensure the singleton works
    {
        _instance = this;
        _settingsDialog = new SettingsDialog();
        _systray = new Systray(this);
        _systemNotificationManager = new SystemNotificationManager(this);
    }
    qCInfo(lcApplication) << u"Plugin search paths:" << qApp->libraryPaths();

    // Check vfs plugins
    if (VfsPluginManager::instance().bestAvailableVfsMode() == Vfs::Off) {
        qCWarning(lcApplication) << u"Theme wants to show vfs mode, but no vfs plugins are available";
    }
    if (VfsPluginManager::instance().isVfsPluginAvailable(Vfs::WindowsCfApi))
        qCInfo(lcApplication) << u"VFS windows plugin is available";

    ConfigFile cfg;

    // this should be called once during application startup to make sure we don't miss any messages
    cfg.configureHttpLogging();

    // The timeout is initialized with an environment variable, if not, override with the value from the config
    if (AbstractNetworkJob::httpTimeout == AbstractNetworkJob::DefaultHttpTimeout) {
        AbstractNetworkJob::httpTimeout = cfg.timeout();
    }

    qApp->setQuitOnLastWindowClosed(false);

    connect(AccountManager::instance(), &AccountManager::accountAdded, this, &Application::slotAccountStateAdded);
    for (const auto &ai : AccountManager::instance()->accounts()) {
        slotAccountStateAdded(ai);
    }
    connect(_systemNotificationManager, &SystemNotificationManager::unknownNotificationClicked, this, &Application::showSettings);
    connect(
        _systemNotificationManager, &SystemNotificationManager::notificationFinished, this, [this](SystemNotification *, SystemNotification::Result result) {
            if (result == SystemNotification::Result::Clicked) {
                showSettings();
            }
        });

#ifdef WITH_AUTO_UPDATER
    // Update checks
    UpdaterScheduler *updaterScheduler = new UpdaterScheduler(this, this);
    // the updater scheduler takes care of connecting its GUI bits to other components
    (void)updaterScheduler;
#endif

    // Cleanup at Quit.
    connect(qApp, &QCoreApplication::aboutToQuit, this, &Application::slotCleanup);

#ifdef Q_OS_MAC
    // add About to the global menu
    QMenuBar *menuBar = new QMenuBar(nullptr);
    // the menu name is not displayed
    auto *menu = menuBar->addMenu(QString());
    // the actual name is provided by mac
    menu->addAction(QStringLiteral("About"), this, &Application::showAbout)->setMenuRole(QAction::AboutRole);
#endif
#ifdef Q_OS_WIN
    // update the existing sidebar entries
    NavigationPaneHelper::removeLegacyCloudStorageRegistry();
#endif
}

Application::~Application()
{
    // Make sure all folders are gone, otherwise removing the
    // accounts will remove the associated folders from the settings.
    FolderMan::instance()->unloadAndDeleteAllFolders();
}

void Application::slotAccountStateAdded(AccountStatePtr accountState) const
{
    connect(accountState->account().data(), &Account::serverVersionChanged, ocApp(), [account = accountState->account().data()] {
        if (account->serverSupportLevel() != Account::ServerSupportLevel::Supported) {
            ocApp()->systemNotificationManager()->notify({tr("Unsupported Server Version"),
                tr("The server on account %1 runs an unsupported version %2. "
                   "Using this client with unsupported server versions is untested and "
                   "potentially dangerous. Proceed at your own risk.")
                    .arg(account->displayNameWithHost(), account->capabilities().status().versionString()),
                Resources::FontIcon(u'')});
        }
    });

    // Hook up the folder manager slots to the account state's Q_SIGNALS:
    connect(accountState.data(), &AccountState::isConnectedChanged, FolderMan::instance(), &FolderMan::slotIsConnectedChanged);
    connect(accountState->account().data(), &Account::serverVersionChanged, FolderMan::instance(),
        [account = accountState->account().data()] { FolderMan::instance()->slotServerVersionChanged(account); });
    accountState->checkConnectivity();
}

void Application::slotCleanup()
{
    ConfigFile().saveGeometry(_settingsDialog);
    delete _settingsDialog;

    // by now the credentials are supposed to be persisted
    // don't start async credentials jobs during shutdown
    AccountManager::instance()->save();

    FolderMan::instance()->unloadAndDeleteAllFolders();

    // Remove the account from the account manager so it can be deleted.
    AccountManager::instance()->shutdown();
}

AccountStatePtr Application::addNewAccount(AccountPtr newAccount)
{
    auto *accountMan = AccountManager::instance();

    // first things first: we need to add the new account
    auto accountStatePtr = accountMan->addAccount(newAccount);

    // if one account is configured: enable autostart
    bool shouldSetAutoStart = (accountMan->accounts().size() == 1);
#ifdef Q_OS_MAC
    // Don't auto start when not being 'installed'
    shouldSetAutoStart = shouldSetAutoStart && QCoreApplication::applicationDirPath().startsWith(QLatin1String("/Applications/"));
#endif
    if (shouldSetAutoStart) {
        Utility::setLaunchOnStartup(Theme::instance()->appName(), Theme::instance()->appNameGUI(), true);
    }

    // showing the UI to show the user that the account has been added successfully
    showSettings();

    return accountStatePtr;
}

void Application::showSettings()
{
    auto window = ocApp()->settingsDialog();
    window->show();
    window->raise();
    window->activateWindow();

#if defined(Q_OS_WIN)
    // Windows disallows raising a Window when you're not the active application.
    // Use a common hack to attach to the active application
    const auto activeProcessId = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    if (activeProcessId != qApp->applicationPid()) {
        const auto threadId = GetCurrentThreadId();
        // don't step here with a debugger...
        if (AttachThreadInput(threadId, activeProcessId, true)) {
            const auto hwnd = reinterpret_cast<HWND>(window->winId());
            SetForegroundWindow(hwnd);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            AttachThreadInput(threadId, activeProcessId, false);
        }
    }
#endif
}

SettingsDialog *Application::settingsDialog() const
{
    return _settingsDialog;
}

void Application::showAbout()
{
    if (!_aboutDialog) {
        _aboutDialog = new AboutDialog(_settingsDialog);
        _aboutDialog->setAttribute(Qt::WA_DeleteOnClose);
        _settingsDialog->addModalWidget(_aboutDialog);
    }
}

SystemNotificationManager *Application::systemNotificationManager() const
{
    return _systemNotificationManager;
}

void Application::runNewAccountWizard()
{
    // passing the settings dialog as parent makes sure the wizard will be shown above it
    // as the settingsDialog's lifetime spans across the entire application but the dialog will live much shorter,
    // we have to clean it up manually when finished() is emitted
    auto *wizardController = new Wizard::SetupWizardController(this->settingsDialog());

    // while the wizard is shown, new syncs are disabled
    FolderMan::instance()->setSyncEnabled(false);

    connect(
        wizardController, &Wizard::SetupWizardController::finished, this, [wizardController, this](const AccountPtr &newAccount, Wizard::SyncMode syncMode) {
            // note: while the wizard is shown, we disable the folder synchronization
            // previously we could perform this just here, but now we have to postpone this depending on whether selective sync was chosen
            // see also #9497

            // when the dialog is closed before it has finished, there won't be a new account to set up
            // the wizard controller signalizes this by passing a null pointer
            if (!newAccount.isNull()) {
                // finally, call the slot that finalizes the setup
                auto accountStatePtr = ocApp()->addNewAccount(newAccount);
                accountStatePtr->setSettingUp(true);

                _settingsDialog->setCurrentAccount(accountStatePtr->account().data());

                // fetch server settings
                auto fetchServerSettings = new FetchServerSettingsJob(accountStatePtr->account(), accountStatePtr->account().data());

                connect(fetchServerSettings, &FetchServerSettingsJob::finishedSignal, accountStatePtr.data(), [accountStatePtr, syncMode, this] {
                    // saving once after adding makes sure the account is stored in the config in a working state
                    // this is needed to ensure a consistent state in the config file upon unexpected terminations of the client
                    // (for instance, when running from a debugger and stopping the process from there)
                    AccountManager::instance()->save();

                    // the account is now ready, emulate a normal account loading and Q_EMIT that the credentials are ready
                    Q_EMIT accountStatePtr->account()->credentialsFetched();

                    switch (syncMode) {
                    case Wizard::SyncMode::SyncEverything:
                    case Wizard::SyncMode::UseVfs: {
                        bool useVfs = syncMode == Wizard::SyncMode::UseVfs;
                        setUpInitialSyncFolder(accountStatePtr, useVfs);
                        accountStatePtr->setSettingUp(false);
                        break;
                    }
                    case Wizard::SyncMode::ConfigureUsingFolderWizard: {
                        Q_ASSERT(!accountStatePtr->account()->hasDefaultSyncRoot());

                        auto *folderWizard = new FolderWizard(accountStatePtr, ocApp()->settingsDialog());
                        folderWizard->setAttribute(Qt::WA_DeleteOnClose);

                        // TODO: duplication of AccountSettings
                        // adapted from AccountSettings::slotFolderWizardAccepted()
                        connect(folderWizard, &QDialog::accepted, accountStatePtr.data(), [accountStatePtr, folderWizard]() {
                            FolderMan *folderMan = FolderMan::instance();

                            qCInfo(lcApplication) << u"Folder wizard completed";
                            const auto config = folderWizard->result();

                            auto folder = folderMan->addFolderFromFolderWizardResult(accountStatePtr, config);

                            if (!config.selectiveSyncBlackList.isEmpty() && OC_ENSURE(folder && !config.useVirtualFiles)) {
                                folder->journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, config.selectiveSyncBlackList);

                                // The user already accepted the selective sync dialog. everything is in the white list
                                folder->journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncWhiteList, {QLatin1String("/")});
                            }

                            folderMan->setSyncEnabled(true);
                            folderMan->scheduleAllFolders();
                            accountStatePtr->setSettingUp(false);
                        });

                        connect(folderWizard, &QDialog::rejected, accountStatePtr.data(), [accountStatePtr]() {
                            qCInfo(lcApplication) << u"Folder wizard cancelled";
                            FolderMan::instance()->setSyncEnabled(true);
                            accountStatePtr->setSettingUp(false);
                        });

                        _settingsDialog->accountSettings(accountStatePtr->account().get())
                            ->addModalLegacyDialog(folderWizard, AccountSettings::ModalWidgetSizePolicy::Expanding);
                        break;
                    }
                    case OCC::Wizard::SyncMode::Invalid:
                        Q_UNREACHABLE();
                    }
                });
                fetchServerSettings->start();
            } else {
                FolderMan::instance()->setSyncEnabled(true);
            }

            // make sure the wizard is cleaned up eventually
            wizardController->deleteLater();
        });

    // all we have to do is show the dialog...
    settingsDialog()->addModalWidget(wizardController->window());
}

QSystemTrayIcon *Application::systemTrayIcon() const
{
    return _systray;
}

bool Application::debugMode()
{
    return _debugMode;
}

std::unique_ptr<Application> Application::createInstance(const QString &displayLanguage, bool debugMode)
{
    Q_ASSERT(!_instance);
    // _instance will be set in the constructor
    new Application(displayLanguage, debugMode);
    return std::unique_ptr<Application>(_instance);
}
