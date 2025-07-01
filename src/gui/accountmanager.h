/*
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include "account.h"
#include "accountstate.h"

#include <QtQmlIntegration/QtQmlIntegration>


class QJSEngine;
class QQmlEngine;
namespace OCC {

/**
   @brief The AccountManager class
   @ingroup gui
*/
class OPENCLOUD_GUI_EXPORT AccountManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<AccountState *> accounts READ accountsRaw() NOTIFY accountsChanged)
    QML_SINGLETON
    QML_ELEMENT
public:
    static AccountManager *instance();

    static AccountManager *create(QQmlEngine *qmlEngine, QJSEngine *);
    ~AccountManager() override {}

    /**
     * Saves the accounts
     */
    void save();

    /**
     * Creates account objects from settings.
     *
     * Returns false if there was an error reading the settings,
     * but note that settings not existing is not an error.
     */
    bool restore();

    /**
     * Add this account in the list of saved accounts.
     * Typically called from the wizard
     */
    AccountStatePtr addAccount(const AccountPtr &newAccount);

    /**
     * remove all accounts
     */
    void shutdown();

    /**
     * Return a list of all accounts.
     */
    const QList<AccountStatePtr> accounts() { return _accounts.values(); }

    /**
     * Return the account state pointer for an account identified by its display name
     */
    AccountStatePtr account(const QUuid uuid);

    /**
     * Delete the AccountState
     */
    void deleteAccount(AccountStatePtr account);


    /**
     * Creates an account and sets up some basic handlers.
     * Does *not* add the account to the account manager just yet.
     */
    static AccountPtr createAccount(const QUuid &uuid);

    /**
     * Returns a sorted list of displayNames
     */
    QStringList accountNames() const;

private:
    // expose raw pointers to qml
    QList<AccountState *> accountsRaw() const;

    // Adds an account to the tracked list, emitting accountAdded()
    AccountStatePtr addAccountState(std::unique_ptr<AccountState> &&accountState);

Q_SIGNALS:
    void accountAdded(AccountStatePtr account);
    void accountRemoved(AccountStatePtr account);
    void accountsChanged();

private:
    AccountManager() {}

    QMap<QUuid, AccountStatePtr> _accounts;
    /// Account ids from settings that weren't read
    QSet<QString> _additionalBlockedAccountIds;
};
}
