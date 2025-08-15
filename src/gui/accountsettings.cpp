/*
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


#include "accountsettings.h"
#include "ui_accountsettings.h"


#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"
#include "application.h"
#include "common/restartmanager.h"
#include "commonstrings.h"
#include "configfile.h"
#include "folderman.h"
#include "folderstatusmodel.h"
#include "folderwizard/folderwizard.h"
#include "gui/accountmodalwidget.h"
#include "gui/models/models.h"
#include "gui/networkinformation.h"
#include "gui/notifications/systemnotificationmanager.h"
#include "gui/qmlutils.h"
#include "gui/selectivesyncwidget.h"
#include "gui/spaces/spaceimageprovider.h"
#include "gui/updateurldialog.h"
#include "libsync/graphapi/spacesmanager.h"
#include "libsync/syncresult.h"
#include "networkjobs/jsonjob.h"
#include "resources/fonticon.h"
#include "scheduling/syncscheduler.h"
#include "settingsdialog.h"

#include <QSortFilterProxyModel>
#include <QtQuickWidgets/QtQuickWidgets>

using namespace std::chrono_literals;

namespace {
constexpr auto modalWidgetStretchedMarginC = 50;
}

namespace OCC {

Q_LOGGING_CATEGORY(lcAccountSettings, "gui.account.settings", QtInfoMsg)

AccountSettings::AccountSettings(const AccountStatePtr &accountState, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::AccountSettings)
    , _wasDisabledBefore(false)
    , _accountState(accountState)
{
    ui->setupUi(this);

    _model = new FolderStatusModel(this);
    _model->setAccountState(_accountState);

    auto weightedModel = new QSortFilterProxyModel(this);
    weightedModel->setSourceModel(_model);
    weightedModel->setSortRole(static_cast<int>(FolderStatusModel::Roles::Priority));
    weightedModel->sort(0, Qt::DescendingOrder);

    _sortModel = weightedModel;

    ui->quickWidget->engine()->addImageProvider(QStringLiteral("space"), new Spaces::SpaceImageProvider(_accountState->account()));
    ui->quickWidget->setOCContext(QUrl(QStringLiteral("qrc:/qt/qml/eu/OpenCloud/gui/qml/FolderDelegate.qml")), this);

    connect(FolderMan::instance(), &FolderMan::folderListChanged, _model, &FolderStatusModel::resetFolders);


    connect(_accountState->account().data(), &Account::requestUrlUpdate, this, [this](const QUrl &newUrl) {
        if (_updateUrlDialog) {
            return;
        }
        auto *updateUrlDialog = UpdateUrlDialog::fromAccount(_accountState->account(), newUrl, ocApp()->settingsDialog());
        _updateUrlDialog = updateUrlDialog;

        connect(updateUrlDialog, &UpdateUrlDialog::accepted, this, [newUrl, this]() {
            _accountState->account()->setUrl(newUrl);
            Q_EMIT _accountState->account()->wantsAccountSaved(_accountState->account().data());
            // reload the spaces
            RestartManager::requestRestart();
        });
        auto *modalWidget = new AccountModalWidget(updateUrlDialog->windowTitle(), updateUrlDialog, ocApp()->settingsDialog());
        connect(updateUrlDialog, &UpdateUrlDialog::accepted, modalWidget, &AccountModalWidget::accept);
        connect(updateUrlDialog, &UpdateUrlDialog::rejected, modalWidget, &AccountModalWidget::reject);
        addModalWidget(modalWidget);
    });

    connect(_accountState.data(), &AccountState::stateChanged, this, &AccountSettings::slotAccountStateChanged);
    slotAccountStateChanged();

    connect(_accountState.get(), &AccountState::isSettingUpChanged, this, [this] {
        if (_accountState->isSettingUp()) {
            ui->spinner->startAnimation();
            ui->stackedWidget->setCurrentWidget(ui->loadingPage);
        } else {
            ui->spinner->stopAnimation();
            ui->stackedWidget->setCurrentWidget(ui->quickWidget);
        }
    });
    ui->stackedWidget->setCurrentWidget(ui->quickWidget);

    auto *notificationsPollTimer = new QTimer(_accountState);
    notificationsPollTimer->setInterval(1min);
    notificationsPollTimer->start();
    connect(notificationsPollTimer, &QTimer::timeout, this, &AccountSettings::updateNotifications);
}

void AccountSettings::slotToggleSignInState()
{
    if (_accountState->isSignedOut()) {
        _accountState->signIn();
    } else {
        _accountState->signOutByUi();
    }
}

void AccountSettings::markNotificationsRead()
{
    if (!_notifications.isEmpty()) {
        auto *job = Notification::dismissAllNotifications(_accountState->account(), _notifications, this);
        connect(job, &JsonApiJob::finishedSignal, this, &AccountSettings::updateNotifications);
        job->start();
    }
}

void AccountSettings::showSelectiveSyncDialog(Folder *folder)
{
    auto *selectiveSync = new SelectiveSyncWidget(_accountState->account(), this);
    selectiveSync->setDavUrl(folder->webDavUrl());
    bool ok;
    selectiveSync->setFolderInfo(folder->displayName(), folder->journalDb()->getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok));
    Q_ASSERT(ok);

    auto *modalWidget = new AccountModalWidget(tr("Choose what to sync"), selectiveSync, this);
    modalWidget->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    connect(modalWidget, &AccountModalWidget::accepted, this, [selectiveSync, folder, this] {
        folder->journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, selectiveSync->createBlackList());
        doForceSyncCurrentFolder(folder);
    });
    addModalWidget(modalWidget);
}

void AccountSettings::slotAddFolder()
{
    FolderMan::instance()->setSyncEnabled(false); // do not start more syncs.

    FolderWizard *folderWizard = new FolderWizard(_accountState, this);
    folderWizard->setAttribute(Qt::WA_DeleteOnClose);

    connect(folderWizard, &QDialog::accepted, this, &AccountSettings::slotFolderWizardAccepted);
    connect(folderWizard, &QDialog::rejected, this, [] {
        qCInfo(lcAccountSettings) << u"Folder wizard cancelled";
        FolderMan::instance()->setSyncEnabled(true);
    });

    addModalLegacyDialog(folderWizard, AccountSettings::ModalWidgetSizePolicy::Expanding);
}


void AccountSettings::slotFolderWizardAccepted()
{
    FolderWizard *folderWizard = qobject_cast<FolderWizard *>(sender());
    qCInfo(lcAccountSettings) << u"Folder wizard completed";

    const auto config = folderWizard->result();

    auto folder = FolderMan::instance()->addFolderFromFolderWizardResult(_accountState, config);

    if (!config.selectiveSyncBlackList.isEmpty() && OC_ENSURE(folder && !config.useVirtualFiles)) {
        folder->journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, config.selectiveSyncBlackList);

        // The user already accepted the selective sync dialog. everything is in the white list
        folder->journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncWhiteList, {QLatin1String("/")});
    }
    FolderMan::instance()->setSyncEnabled(true);
    FolderMan::instance()->scheduleAllFolders();
}

void AccountSettings::slotRemoveCurrentFolder(Folder *folder)
{
    // TODO: move to qml
    qCInfo(lcAccountSettings) << u"Remove Folder " << folder->path();
    auto messageBox = new QMessageBox(QMessageBox::Question, tr("Confirm removal of Space"),
        tr("<p>Do you really want to stop syncing the Space »%1«?</p>"
           "<p><b>Note:</b> This will <b>not</b> delete any files.</p>")
            .arg(folder->displayName()),
        QMessageBox::NoButton, ocApp()->settingsDialog());
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton *yesButton = messageBox->addButton(tr("Remove Space"), QMessageBox::YesRole);
    messageBox->addButton(tr("Cancel"), QMessageBox::NoRole);
    connect(messageBox, &QMessageBox::finished, this, [messageBox, yesButton, folder, this] {
        if (messageBox->clickedButton() == yesButton) {
            FolderMan::instance()->removeFolder(folder);
            QTimer::singleShot(0, this, &AccountSettings::slotSpacesUpdated);
        }
    });
    messageBox->open();
}

void AccountSettings::slotEnableVfsCurrentFolder(Folder *folder)
{
    if (OC_ENSURE(VfsPluginManager::instance().bestAvailableVfsMode() == Vfs::WindowsCfApi)) {
        if (!folder) {
            return;
        }
        qCInfo(lcAccountSettings) << u"Enabling vfs support for folder" << folder->path();

        // Change the folder vfs mode and load the plugin
        folder->setVirtualFilesEnabled(true);

        // don't schedule the folder, it might not be ready yet.
        // it will schedule its self once set up
    }
}

void AccountSettings::slotDisableVfsCurrentFolder(Folder *folder)
{
    auto msgBox = new QMessageBox(
        QMessageBox::Question,
        tr("Disable virtual file support?"),
        tr("This action will disable virtual file support. As a consequence contents of folders that "
           "are currently marked as 'available online only' will be downloaded."
           "\n\n"
           "The only advantage of disabling virtual file support is that the selective sync feature "
           "will become available again."
           "\n\n"
           "This action will abort any currently running synchronization."));
    auto acceptButton = msgBox->addButton(tr("Disable support"), QMessageBox::AcceptRole);
    msgBox->addButton(tr("Cancel"), QMessageBox::RejectRole);
    connect(msgBox, &QMessageBox::finished, msgBox, [msgBox, folder, acceptButton] {
        msgBox->deleteLater();
        if (msgBox->clickedButton() != acceptButton || !folder) {
            return;
        }

        qCInfo(lcAccountSettings) << u"Disabling vfs support for folder" << folder->path();

        // Also wipes virtual files, schedules remote discovery
        folder->setVirtualFilesEnabled(false);
    });
    msgBox->open();
}

void AccountSettings::showConnectionLabel(const QString &message, SyncResult::Status status, QStringList errors)
{
    if (errors.isEmpty()) {
        _connectionLabel = message;
    } else {
        errors.prepend(message);
        const QString msg = errors.join(QLatin1String("\n"));
        qCDebug(lcAccountSettings) << msg;
        _connectionLabel = msg;
    }
    _accountStateIconGlype = SyncResult(status).glype();
    Q_EMIT connectionLabelChanged();
}

void AccountSettings::slotEnableCurrentFolder(Folder *folder, bool terminate)
{
    Q_ASSERT(folder);
    qCInfo(lcAccountSettings) << u"Application: enable folder with alias " << folder->path();
    bool currentlyPaused = false;

    // this sets the folder status to disabled but does not interrupt it.
    currentlyPaused = folder->isSyncPaused();
    if (!currentlyPaused && !terminate) {
        // check if a sync is still running and if so, ask if we should terminate.
        if (folder->isSyncRunning()) { // its still running
            auto msgbox = new QMessageBox(QMessageBox::Question, tr("Sync Running"), tr("The sync operation is running.<br/>Do you want to stop it?"),
                QMessageBox::Yes | QMessageBox::No, this);
            msgbox->setAttribute(Qt::WA_DeleteOnClose);
            msgbox->setDefaultButton(QMessageBox::Yes);
            connect(msgbox, &QMessageBox::accepted, this, [folder = QPointer<Folder>(folder), this] {
                if (folder) {
                    slotEnableCurrentFolder(folder, true);
                }
            });
            msgbox->open();
            return;
        }
    }

    // message box can return at any time while the thread keeps running,
    // so better check again after the user has responded.
    if (folder->isSyncRunning() && terminate) {
        folder->slotTerminateSync(tr("Sync paused by user"));
    }
    folder->slotNextSyncFullLocalDiscovery(); // ensure we don't forget about local errors
    folder->setSyncPaused(!currentlyPaused);

    // keep state for the icon setting.
    if (currentlyPaused)
        _wasDisabledBefore = true;

    _model->slotUpdateFolderState(folder);
}

void AccountSettings::slotForceSyncCurrentFolder(Folder *folder)
{
    if (NetworkInformation::instance()->isMetered() && ConfigFile().pauseSyncWhenMetered()) {
        auto messageBox = new QMessageBox(QMessageBox::Question, tr("Internet connection is metered"),
            tr("Synchronization is paused because the Internet connection is a metered connection"
               "<p>Do you really want to force a Synchronization now?"),
            QMessageBox::Yes | QMessageBox::No, ocApp()->settingsDialog());
        messageBox->setAttribute(Qt::WA_DeleteOnClose);
        connect(messageBox, &QMessageBox::accepted, this, [folder = QPointer<Folder>(folder), this] {
            if (folder) {
                doForceSyncCurrentFolder(folder);
            }
        });
        ocApp()->showSettings();
        messageBox->open();
    } else {
        doForceSyncCurrentFolder(folder);
    }
}

void AccountSettings::doForceSyncCurrentFolder(Folder *selectedFolder)
{
    // Prevent new sync starts
    FolderMan::instance()->scheduler()->stop();

    // Terminate and reschedule any running sync
    for (auto *folder : FolderMan::instance()->folders()) {
        if (folder->isSyncRunning()) {
            folder->slotTerminateSync(tr("User triggered force sync"));
            FolderMan::instance()->scheduler()->enqueueFolder(folder);
        }
    }

    selectedFolder->slotWipeErrorBlacklist(); // issue #6757
    selectedFolder->slotNextSyncFullLocalDiscovery(); // ensure we don't forget about local errors

    // Insert the selected folder at the front of the queue
    FolderMan::instance()->scheduler()->enqueueFolder(selectedFolder, SyncScheduler::Priority::High);

    // Restart scheduler
    FolderMan::instance()->scheduler()->start();
}
void AccountSettings::updateNotifications()
{
    if (_accountState->isConnected()) {
        auto *job = Notification::createNotificationsJob(_accountState->account(), this);
        connect(job, &JsonApiJob::finishedSignal, this, [job, this] {
            const auto oldNotifications = _notifications;
            _notifications = Notification::getNotifications(job);
            auto newNotifications = _notifications;
            newNotifications.subtract(oldNotifications);
            for (const auto &notification : newNotifications) {
                SystemNotificationRequest notificationRequest(notification.title, notification.message, Resources::FontIcon(u''));
                notificationRequest.setButtons({tr("Mark as read")});
                if (auto *n = ocApp()->systemNotificationManager()->notify(std::move(notificationRequest))) {
                    connect(n, &SystemNotification::buttonClicked, this, [notification, this](const QString &) {
                        // we only have one button so we don't need to check which was clicked
                        auto *job = Notification::dismissAllNotifications(_accountState->account(), {notification}, this);
                        connect(job, &JsonApiJob::finishedSignal, this, &AccountSettings::updateNotifications);
                        job->start();
                    });
                }
            }
            Q_EMIT notificationsChanged();
        });
        job->start();
    }
}

void AccountSettings::slotAccountStateChanged()
{
    const AccountState::State state = _accountState->state();
    const AccountPtr account = _accountState->account();
    qCDebug(lcAccountSettings) << u"Account state changed to" << state << u"for account" << account;

    FolderMan *folderMan = FolderMan::instance();
    for (auto *folder : folderMan->folders()) {
        _model->slotUpdateFolderState(folder);
    }

    switch (state) {
    case AccountState::Connected: {
        QStringList errors;
        auto icon = SyncResult::Success;
        if (account->serverSupportLevel() != Account::ServerSupportLevel::Supported) {
            errors << tr("The server version %1 is unsupported! Proceed at your own risk.").arg(account->capabilities().status().versionString());
            icon = SyncResult::Problem;
        }
        showConnectionLabel(tr("Connected"), icon, errors);
        connect(
            accountsState()->account()->spacesManager(), &GraphApi::SpacesManager::updated, this, &AccountSettings::slotSpacesUpdated, Qt::UniqueConnection);
        slotSpacesUpdated();
        updateNotifications();
        break;
    }
    case AccountState::ServiceUnavailable:
        showConnectionLabel(tr("Server is temporarily unavailable"), SyncResult::Offline);
        break;
    case AccountState::MaintenanceMode:
        showConnectionLabel(tr("Server is currently in maintenance mode"), SyncResult::Offline);
        break;
    case AccountState::SignedOut:
        showConnectionLabel(tr("Signed out"), SyncResult::Offline);
        break;
    case AccountState::AskingCredentials: {
        showConnectionLabel(tr("Updating credentials..."), SyncResult::Undefined);
        break;
    }
    case AccountState::Connecting:
        if (NetworkInformation::instance()->isBehindCaptivePortal()) {
            showConnectionLabel(tr("Captive portal prevents connections to the server."), SyncResult::Offline);
        } else if (NetworkInformation::instance()->isMetered() && ConfigFile().pauseSyncWhenMetered()) {
            showConnectionLabel(tr("Sync is paused due to metered internet connection"), SyncResult::Offline);
        } else {
            showConnectionLabel(tr("Connecting..."), SyncResult::Undefined);
        }
        break;
    case AccountState::ConfigurationError:
        showConnectionLabel(tr("Server configuration error"), SyncResult::Problem, _accountState->connectionErrors());
        break;
    case AccountState::NetworkError:
        // don't display the error to the user, https://github.com/owncloud/client/issues/9790
        [[fallthrough]];
    case AccountState::Disconnected:
        showConnectionLabel(tr("Disconnected"), SyncResult::Offline);
        break;
    }
}

void AccountSettings::slotSpacesUpdated()
{
    auto spaces = accountsState()->account()->spacesManager()->spaces();
    auto unsycnedSpaces = std::set<GraphApi::Space *>(spaces.cbegin(), spaces.cend());
    for (const auto &f : std::as_const(FolderMan::instance()->folders())) {
        unsycnedSpaces.erase(f->space());
    }

    if (_unsyncedSpaces != unsycnedSpaces.size()) {
        _unsyncedSpaces = static_cast<uint>(unsycnedSpaces.size());
        Q_EMIT unsyncedSpacesChanged();
    }
    uint syncedSpaces = spaces.size() - _unsyncedSpaces;
    if (_syncedSpaces != syncedSpaces) {
        _syncedSpaces = syncedSpaces;
        Q_EMIT syncedSpacesChanged();
    }
}

AccountSettings::~AccountSettings()
{
    _goingDown = true;
    delete ui;
}

void AccountSettings::addModalLegacyDialog(QWidget *widget, ModalWidgetSizePolicy sizePolicy)
{
    if (!widget->testAttribute(Qt::WA_DeleteOnClose)) { // DEBUG CODE! See https://github.com/owncloud/client/issues/11673
        // Early check to see if the attribute gets unset before the second/real check below
        qCWarning(lcAccountSettings) << u"Missing WA_DeleteOnClose! (1)" << widget->metaObject() << widget;
    }

    // create a widget filling the stacked widget
    // this widget contains a wrapping group box with widget as content
    auto *outerWidget = new QWidget;
    auto *groupBox = new QGroupBox;

    switch (sizePolicy) {
    case ModalWidgetSizePolicy::Expanding: {
        auto *outerLayout = new QHBoxLayout(outerWidget);
        outerLayout->setContentsMargins(modalWidgetStretchedMarginC, modalWidgetStretchedMarginC, modalWidgetStretchedMarginC, modalWidgetStretchedMarginC);
        outerLayout->addWidget(groupBox);
        auto *layout = new QHBoxLayout(groupBox);
        layout->addWidget(widget);
    } break;
    case ModalWidgetSizePolicy::Minimum: {
        auto *outerLayout = new QGridLayout(outerWidget);
        outerLayout->addWidget(groupBox, 0, 0, Qt::AlignCenter);
        auto *layout = new QHBoxLayout(groupBox);
        layout->addWidget(widget);
    } break;
    }
    groupBox->setTitle(widget->windowTitle());

    ui->stackedWidget->addWidget(outerWidget);
    ui->stackedWidget->setCurrentWidget(outerWidget);

    // the widget is supposed to behave like a dialog and we connect to its destuction
    if (!widget->testAttribute(Qt::WA_DeleteOnClose)) { // DEBUG CODE! See https://github.com/owncloud/client/issues/11673
        qCWarning(lcAccountSettings) << u"Missing WA_DeleteOnClose! (2)" << widget->metaObject() << widget;
    }
    Q_ASSERT(widget->testAttribute(Qt::WA_DeleteOnClose));
    connect(widget, &QWidget::destroyed, this, [this, outerWidget] {
        outerWidget->deleteLater();
        if (!_goingDown) {
            ocApp()->settingsDialog()->ceaseModality(_accountState->account().get());
        }
    });
    widget->setVisible(true);
    ocApp()->settingsDialog()->requestModality(_accountState->account().get());
}

void AccountSettings::addModalWidget(AccountModalWidget *widget)
{
    ui->stackedWidget->addWidget(widget);
    ui->stackedWidget->setCurrentWidget(widget);

    connect(widget, &AccountModalWidget::finished, this, [widget, this] {
        widget->deleteLater();
        ocApp()->settingsDialog()->ceaseModality(_accountState->account().get());
    });
    ocApp()->settingsDialog()->requestModality(_accountState->account().get());
}

uint AccountSettings::unsyncedSpaces() const
{
    return _unsyncedSpaces;
}

uint AccountSettings::syncedSpaces() const
{
    return _syncedSpaces;
}

auto AccountSettings::model() const
{
    return _sortModel;
}

QString AccountSettings::connectionLabel()
{
    return _connectionLabel;
}

QChar AccountSettings::accountStateIconGlype()
{
    return _accountStateIconGlype;
}

const QSet<Notification> &AccountSettings::notifications() const
{
    return _notifications;
}

void AccountSettings::slotDeleteAccount()
{
    // Deleting the account potentially deletes 'this', so
    // the QMessageBox should be destroyed before that happens.
    auto messageBox = new QMessageBox(QMessageBox::Question, tr("Confirm Account Removal"),
        tr("<p>Do you really want to remove the connection to the account %1«?</p>"
           "<p><b>Note:</b> This will <b>not</b> delete any files.</p>")
            .arg(_accountState->account()->displayNameWithHost()),
        QMessageBox::NoButton, this);
    auto yesButton = messageBox->addButton(tr("Remove connection"), QMessageBox::YesRole);
    messageBox->addButton(tr("Cancel"), QMessageBox::NoRole);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    connect(messageBox, &QMessageBox::finished, this, [this, messageBox, yesButton]{
        if (messageBox->clickedButton() == yesButton) {
            auto manager = AccountManager::instance();
            manager->deleteAccount(_accountState);
        }
    });
    messageBox->open();
}

} // namespace OCC
