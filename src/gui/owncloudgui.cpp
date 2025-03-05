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

#include "owncloudgui.h"
#include "aboutdialog.h"
#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"
#include "application.h"
#include "common/restartmanager.h"
#include "configfile.h"
#include "folderman.h"
#include "folderwizard/folderwizard.h"
#include "gui/accountsettings.h"
#include "gui/commonstrings.h"
#include "gui/networkinformation.h"
#include "libsync/theme.h"
#include "logbrowser.h"
#include "logger.h"
#include "openfilemanager.h"
#include "settingsdialog.h"
#include "setupwizardcontroller.h"

#include "libsync/graphapi/space.h"
#include "libsync/graphapi/spacesmanager.h"

#include "resources/resources.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QHBoxLayout>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace std::chrono_literals;

namespace {

using namespace OCC;

void setUpInitialSyncFolder(AccountStatePtr accountStatePtr, bool useVfs)
{
    auto folderMan = FolderMan::instance();

    // saves a bit of duplicate code
    auto addFolder = [folderMan, accountStatePtr, useVfs](
                         const QString &localFolder, const QUrl &davUrl, const QString &spaceId = {}, const QString &displayName = {}) {
        auto def = FolderDefinition{accountStatePtr->account()->uuid(), davUrl, spaceId, displayName};
        def.setLocalPath(localFolder);
        return folderMan->addFolderFromWizard(accountStatePtr, std::move(def), useVfs);
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
                Folder::prepareFolder(localDir);
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

namespace OCC {

ownCloudGui::ownCloudGui(Application *parent)
    : QObject(parent)
    , _tray(new Systray(this))
    , _settingsDialog(new SettingsDialog(this))
    , _app(parent)
{
    connect(_tray, &QSystemTrayIcon::activated,
        this, &ownCloudGui::slotTrayClicked);

    // init systray
    slotComputeOverallSyncStatus();
    setContextMenu();
    _tray->show();
}

ownCloudGui::~ownCloudGui()
{
    delete _settingsDialog;
}

void ownCloudGui::slotTrayClicked(QSystemTrayIcon::ActivationReason reason)
{
    // Left click
    if (reason == QSystemTrayIcon::Trigger) {
        slotShowSettings();
    }
}

void ownCloudGui::slotFoldersChanged()
{
    slotComputeOverallSyncStatus();
}

void ownCloudGui::slotOpenPath(const QString &path)
{
    showInFileManager(path);
}

void ownCloudGui::slotAccountStateChanged()
{
    slotComputeOverallSyncStatus();
}

void ownCloudGui::slotTrayMessageIfServerUnsupported(Account *account)
{
    if (account->serverSupportLevel() != Account::ServerSupportLevel::Supported) {
        slotShowTrayMessage(tr("Unsupported Server Version"),
            tr("The server on account %1 runs an unsupported version %2. "
               "Using this client with unsupported server versions is untested and "
               "potentially dangerous. Proceed at your own risk.")
                .arg(account->displayNameWithHost(), account->capabilities().status().versionString()));
    }
}

void ownCloudGui::slotComputeOverallSyncStatus()
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
        _tray->setIcon(getIconFromStatus(SyncResult::Status::Offline));
#ifdef Q_OS_WIN
        // Windows has a 128-char tray tooltip length limit.
        QStringList accountNames;
        for (const auto &a : std::as_const(problemAccounts)) {
            accountNames.append(a->account()->displayNameWithHost());
        }
        _tray->setToolTip(tr("Disconnected from %1").arg(accountNames.join(QLatin1String(", "))));
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
        _tray->setToolTip(messages.join(QLatin1String("\n\n")));
#endif
        return;
    }

    if (allSignedOut) {
        _tray->setIcon(getIconFromStatus(SyncResult::Status::Offline));
        _tray->setToolTip(tr("Please sign in"));
        return;
    } else if (allPaused) {
        _tray->setIcon(getIconFromStatus(SyncResult::Paused));
        _tray->setToolTip(tr("Account synchronization is disabled"));
        return;
    }

    // display the info of the least successful sync (eg. do not just display the result of the latest sync)
    QString trayMessage;

    auto trayOverallStatusResult = FolderMan::trayOverallStatus(map);
    const QIcon statusIcon = getIcon(trayOverallStatusResult.overallStatus());
    _tray->setIcon(statusIcon);

    // create the tray blob message, check if we have an defined state
#ifdef Q_OS_WIN
    // Windows has a 128-char tray tooltip length limit.
    trayMessage = FolderMan::instance()->trayTooltipStatusString(trayOverallStatusResult.overallStatus(), false);
#else
    QStringList allStatusStrings;
    for (auto *folder : map) {
        QString folderMessage = FolderMan::trayTooltipStatusString(folder->syncResult(), folder->isSyncPaused());
        allStatusStrings += tr("Folder %1: %2").arg(folder->shortGuiLocalPath(), folderMessage);
    }
    trayMessage = allStatusStrings.join(QLatin1String("\n"));
#endif
    _tray->setToolTip(trayMessage);
}


SettingsDialog *ownCloudGui::settingsDialog() const
{
    return _settingsDialog;
}

void ownCloudGui::hideAndShowTray()
{
    _tray->hide();
    _tray->show();
}

void ownCloudGui::setContextMenu()
{
    Q_ASSERT(!_tray->contextMenu());
    auto *menu = new QMenu(Theme::instance()->appNameGUI());

    menu->addAction(Theme::instance()->applicationIcon(), tr("Show %1").arg(Theme::instance()->appNameGUI()), this, &ownCloudGui::slotShowSettings);
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

    if (_app->debugMode()) {
        menu->addSeparator();
        auto *debugMenu = menu->addMenu(QStringLiteral("Debug actions"));
        debugMenu->addAction(QStringLiteral("Crash if asserts enabled - OC_ENSURE"), _app, [] {
            if (OC_ENSURE(false)) {
                Q_UNREACHABLE();
            }
        });
        debugMenu->addAction(QStringLiteral("Crash if asserts enabled - Q_ASSERT"), _app, [] { Q_ASSERT(false); });
        debugMenu->addAction(QStringLiteral("Crash now - Utility::crash()"), _app, [] { Utility::crash(); });
        debugMenu->addAction(QStringLiteral("Crash now - OC_ENFORCE()"), _app, [] { OC_ENFORCE(false); });
        debugMenu->addAction(QStringLiteral("Crash now - qFatal"), _app, [] { qFatal("la Qt fatale"); });
        debugMenu->addAction(QStringLiteral("Restart now"), _app, [] { RestartManager::requestRestart(); });
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

    menu->addAction(tr("About"), this, &ownCloudGui::slotAbout);

    // this action will be hidden on mac and be part of the application menu
    menu->addAction(tr("About Qt"), qApp, &QApplication::aboutQt)->setMenuRole(QAction::AboutQtRole);

    menu->addAction(tr("Quit"), _app, &QApplication::quit);

    _tray->setContextMenu(menu);
}

void ownCloudGui::slotShowTrayMessage(const QString &title, const QString &msg, const QIcon &icon)
{
    _tray->showMessage(title, msg, icon.isNull() ? Resources::getCoreIcon(QStringLiteral("states/information")) : icon);
}

void ownCloudGui::slotShowOptionalTrayMessage(const QString &title, const QString &msg, const QIcon &icon)
{
    ConfigFile cfg;
    if (cfg.optionalDesktopNotifications()) {
        slotShowTrayMessage(title, msg, icon);
    }
}

void ownCloudGui::runNewAccountWizard()
{
    if (_wizardController.isNull()) {
        // passing the settings dialog as parent makes sure the wizard will be shown above it
        // as the settingsDialog's lifetime spans across the entire application but the dialog will live much shorter,
        // we have to clean it up manually when finished() is emitted
        _wizardController = new Wizard::SetupWizardController(settingsDialog());

        // while the wizard is shown, new syncs are disabled
        FolderMan::instance()->setSyncEnabled(false);

        connect(_wizardController, &Wizard::SetupWizardController::finished, ocApp(),
            [this](AccountPtr newAccount, Wizard::SyncMode syncMode, const QVariantMap &dynamicRegistrationData) {
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

                    // ensure we are connected and fetch the capabilities
                    auto validator = new ConnectionValidator(accountStatePtr->account(), accountStatePtr->account().data());

                    QObject::connect(validator, &ConnectionValidator::connectionResult, accountStatePtr.data(),
                        [accountStatePtr, syncMode, dynamicRegistrationData](ConnectionValidator::Status status, const QStringList &) {
                            switch (status) {
                            // a server we no longer support but that might work
                            case ConnectionValidator::ServerVersionMismatch:
                                [[fallthrough]];
                            case ConnectionValidator::Connected: {
                                // saving once after adding makes sure the account is stored in the config in a working state
                                // this is needed to ensure a consistent state in the config file upon unexpected terminations of the client
                                // (for instance, when running from a debugger and stopping the process from there)
                                AccountManager::instance()->save();

                                // only now, we can store the dynamic registration data in the keychain
                                if (!dynamicRegistrationData.isEmpty()) {
                                    OAuth::saveDynamicRegistrationDataForAccount(accountStatePtr->account(), dynamicRegistrationData);
                                }

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

                                    auto *folderWizard = new FolderWizard(accountStatePtr, ocApp()->gui()->settingsDialog());
                                    folderWizard->setAttribute(Qt::WA_DeleteOnClose);

                                    // TODO: duplication of AccountSettings
                                    // adapted from AccountSettings::slotFolderWizardAccepted()
                                    connect(folderWizard, &QDialog::accepted, accountStatePtr.data(), [accountStatePtr, folderWizard]() {
                                        FolderMan *folderMan = FolderMan::instance();

                                        qCInfo(lcApplication) << "Folder wizard completed";
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
                                        qCInfo(lcApplication) << "Folder wizard cancelled";
                                        FolderMan::instance()->setSyncEnabled(true);
                                        accountStatePtr->setSettingUp(false);
                                    });

                                    ocApp()
                                        ->gui()
                                        ->settingsDialog()
                                        ->accountSettings(accountStatePtr->account().get())
                                        ->addModalLegacyDialog(folderWizard, AccountSettings::ModalWidgetSizePolicy::Expanding);
                                    break;
                                }
                                case OCC::Wizard::SyncMode::Invalid:
                                    Q_UNREACHABLE();
                                }
                            }
                            case ConnectionValidator::ClientUnsupported:
                                break;
                            default:
                                Q_UNREACHABLE();
                            }
                        });


                    validator->checkServer();
                } else {
                    FolderMan::instance()->setSyncEnabled(true);
                }

                // make sure the wizard is cleaned up eventually
                _wizardController->deleteLater();
            });

        // all we have to do is show the dialog...
        ocApp()->gui()->settingsDialog()->addModalWidget(_wizardController->window());
    }
}

void ownCloudGui::slotShowSettings()
{
    raise();
}

void ownCloudGui::slotShowSyncProtocol()
{
    slotShowSettings();
    _settingsDialog->setCurrentPage(SettingsDialog::SettingsPage::Activity);
}


void ownCloudGui::slotShutdown()
{
    // explicitly close windows. This is somewhat of a hack to ensure
    // that saving the geometries happens ASAP during a OS shutdown

    // those do delete on close
    _settingsDialog->close();
}

void ownCloudGui::slotToggleLogBrowser()
{
    auto logBrowser = new LogBrowser(settingsDialog());
    logBrowser->setAttribute(Qt::WA_DeleteOnClose);
    ownCloudGui::raise();
    logBrowser->open();
}

void ownCloudGui::raise()
{
    auto window = ocApp()->gui()->settingsDialog();
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
        if (AttachThreadInput(threadId, activeProcessId, true))
        {
            const auto hwnd = reinterpret_cast<HWND>(window->winId());
            SetForegroundWindow(hwnd);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            AttachThreadInput(threadId, activeProcessId, false);
        }
    }
#endif
}

void ownCloudGui::slotAbout()
{
    if(!_aboutDialog) {
        _aboutDialog = new AboutDialog(_settingsDialog);
        _aboutDialog->setAttribute(Qt::WA_DeleteOnClose);
        ocApp()->gui()->settingsDialog()->addModalWidget(_aboutDialog);
    }
}


} // end namespace
