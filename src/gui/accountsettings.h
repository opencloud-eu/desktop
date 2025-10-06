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

#pragma once

#include "gui/opencloudguilib.h"

#include "folder.h"
#include "gui/notifications.h"
#include "gui/qmlutils.h"
#include "progressdispatcher.h"

#include <QSortFilterProxyModel>
#include <QWidget>

class QModelIndex;
class QNetworkReply;
class QLabel;

namespace OCC {
class AccountModalWidget;

namespace Ui {
    class AccountSettings;
}

class FolderMan;

class Account;
class AccountState;
class FolderStatusModel;
class FolderStatusDelegate;

/**
 * @brief The AccountSettings class
 * @ingroup gui
 */
class OPENCLOUD_GUI_EXPORT AccountSettings : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(AccountState *accountState MEMBER _accountState CONSTANT)
    Q_PROPERTY(QSortFilterProxyModel *model MEMBER _sortModel CONSTANT)
    Q_PROPERTY(uint unsyncedSpaces READ unsyncedSpaces NOTIFY unsyncedSpacesChanged)
    Q_PROPERTY(uint syncedSpaces READ syncedSpaces NOTIFY syncedSpacesChanged)
    Q_PROPERTY(QString connectionLabel READ connectionLabel NOTIFY connectionLabelChanged)
    Q_PROPERTY(QChar accountStateIconGlype READ accountStateIconGlype NOTIFY connectionLabelChanged)
    Q_PROPERTY(QSet<Notification> notifications READ notifications NOTIFY notificationsChanged)
    OC_DECLARE_WIDGET_FOCUS
    QML_ELEMENT
    QML_UNCREATABLE("C++ only")

public:
    enum class ModalWidgetSizePolicy { Minimum = QSizePolicy::Minimum, Expanding = QSizePolicy::Expanding };
    Q_ENUM(ModalWidgetSizePolicy)

    explicit AccountSettings(const AccountStatePtr &accountState, QWidget *parent = nullptr);
    ~AccountSettings() override;

    AccountStatePtr accountsState() const { return _accountState; }

    void addModalLegacyDialog(QWidget *widget, ModalWidgetSizePolicy sizePolicy);
    void addModalWidget(AccountModalWidget *widget);

    uint unsyncedSpaces() const;
    uint syncedSpaces() const;

    auto model() const;

    QString connectionLabel();
    QChar accountStateIconGlype();

    const QSet<Notification> &notifications() const;

Q_SIGNALS:
    void showIssuesList();
    void unsyncedSpacesChanged();
    void syncedSpacesChanged();
    void connectionLabelChanged();
    void notificationsChanged();

public Q_SLOTS:
    void slotAccountStateChanged();
    void slotSpacesUpdated();

protected Q_SLOTS:
    void slotAddFolder();
    void slotEnableCurrentFolder(Folder *folder, bool terminate = false);
    void slotForceSyncCurrentFolder(Folder *folder);
    void slotRemoveCurrentFolder(Folder *folder);
    void showSelectiveSyncDialog(Folder *folder);
    void slotFolderWizardAccepted();
    void slotDeleteAccount();
    void slotToggleSignInState();
    void markNotificationsRead();

private:
    void showConnectionLabel(const QString &message, SyncResult::Status status, QStringList errors = {});

    void doForceSyncCurrentFolder(Folder *selectedFolder);

    void updateNotifications();

    Ui::AccountSettings *ui;

    FolderStatusModel *_model;
    QSortFilterProxyModel *_sortModel;
    AccountStatePtr _accountState;
    // are we already in the destructor
    bool _goingDown = false;
    uint _syncedSpaces = 0;
    uint _unsyncedSpaces = 0;
    QString _connectionLabel;
    QChar _accountStateIconGlype;

    QSet<Notification> _notifications;

    QPointer<QWidget> _updateUrlDialog;
};

} // namespace OCC
