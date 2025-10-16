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

#include "accountmanager.h"
#include "account.h"
#include "configfile.h"
#include "creds/credentialmanager.h"
#include "guiutility.h"
#include <creds/httpcredentialsgui.h>
#include <theme.h>

#ifdef Q_OS_WIN
#include "common/utility_win.h"
#include "gui/navigationpanehelper.h"
#endif

#include <QSettings>

using namespace Qt::Literals::StringLiterals;

namespace {
auto urlC()
{
    return QStringLiteral("url");
}

auto defaultSyncRootC()
{
    return QStringLiteral("default_sync_root");
}

const QString davUserDisplyNameC()
{
    return QStringLiteral("display-name");
}

const QString userUUIDC()
{
    return QStringLiteral("uuid");
}

auto caCertsKeyC()
{
    return QStringLiteral("CaCertificates");
}

auto accountsC()
{
    return QStringLiteral("Accounts");
}

auto capabilitesC()
{
    return QStringLiteral("capabilities");
}
}


namespace OCC {

Q_LOGGING_CATEGORY(lcAccountManager, "gui.account.manager", QtInfoMsg)

AccountManager *AccountManager::instance()
{
    static AccountManager instance;
    return &instance;
}

AccountManager *AccountManager::create(QQmlEngine *qmlEngine, QJSEngine *)
{
    Q_ASSERT(qmlEngine->thread() == AccountManager::instance()->thread());
    QJSEngine::setObjectOwnership(AccountManager::instance(), QJSEngine::CppOwnership);
    return instance();
}

bool AccountManager::restore()
{
    auto settings = ConfigFile::makeQSettings();
    if (settings.status() != QSettings::NoError) {
        qCWarning(lcAccountManager) << u"Could not read settings from" << settings.fileName() << settings.status();
        return false;
    }

    const auto size = settings.beginReadArray(accountsC());
    for (auto i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        auto urlConfig = settings.value(urlC());
        if (!urlConfig.isValid()) {
            // No URL probably means a corrupted entry in the account settings
            qCWarning(lcAccountManager) << u"No URL for account " << settings.group();
            continue;
        }

        auto acc = createAccount(settings.value(userUUIDC(), QVariant::fromValue(QUuid::createUuid())).toUuid());

        acc->setUrl(urlConfig.toUrl());

        acc->_displayName = settings.value(davUserDisplyNameC()).toString();

        auto capabilities = settings.value(capabilitesC()).value<QVariantMap>();
        capabilities.insert(u"cached"_s, true); // mark as cached capabilities, this will trigger a capabilitiesChanged signal once we got real capabilities

        acc->setCapabilities({acc->url(), capabilities});
        acc->setDefaultSyncRoot(settings.value(defaultSyncRootC()).toString());

        acc->setCredentials(new HttpCredentialsGui);

        // now the server cert
        const auto certs = QSslCertificate::fromData(settings.value(caCertsKeyC()).toByteArray());
        qCInfo(lcAccountManager) << u"Restored: " << certs.count() << u" unknown certs.";
        acc->setApprovedCerts(certs);

        if (auto accState = AccountState::loadFromSettings(acc, settings)) {
            addAccountState(std::move(accState));
        }
    }
    settings.endArray();

    return true;
}

void AccountManager::save()
{
    auto settings = ConfigFile::makeQSettings();
    settings.remove(accountsC());
    settings.beginWriteArray(accountsC(), _accounts.size());

    int i = 0;
    for (const auto &accountState : std::as_const(_accounts)) {
        settings.setArrayIndex(i++);
        auto account = accountState->account();
        qCDebug(lcAccountManager) << u"Saving account" << account->url().toString();
        settings.setValue(urlC(), account->_url.toString());
        settings.setValue(davUserDisplyNameC(), account->_displayName);
        settings.setValue(userUUIDC(), account->uuid());
        if (account->hasCapabilities()) {
            settings.setValue(capabilitesC(), account->capabilities().raw());
        }
        if (account->hasDefaultSyncRoot()) {
            settings.setValue(defaultSyncRootC(), account->defaultSyncRoot());
        }

        // Save accepted certificates.
        qCInfo(lcAccountManager) << u"Saving " << account->approvedCerts().count() << u" unknown certs.";
        const auto approvedCerts = account->approvedCerts();
        QByteArray certs;
        for (const auto &cert : approvedCerts) {
            certs += cert.toPem() + '\n';
        }
        if (!certs.isEmpty()) {
            settings.setValue(caCertsKeyC(), certs);
        }

        // save the account state
        accountState->writeToSettings(settings);
    }
    settings.endArray();

    qCInfo(lcAccountManager) << u"Saved all account settings";
}

QStringList AccountManager::accountNames() const
{
    QStringList accounts;
    accounts.reserve(AccountManager::instance()->accounts().size());
    for (const auto &a : AccountManager::instance()->accounts()) {
        accounts << a->account()->displayNameWithHost();
    }
    std::sort(accounts.begin(), accounts.end());
    return accounts;
}

QList<AccountState *> AccountManager::accountsRaw() const
{
    QList<AccountState *> out;
    out.reserve(_accounts.size());
    for (auto &x : _accounts.values()) {
        out.append(x);
    }
    return out;
}

AccountStatePtr AccountManager::account(const QUuid uuid)
{
    const auto acc = Utility::optionalFind(_accounts, uuid);
    if (OC_ENSURE(acc.has_value())) {
        return acc->value();
    }
    return {};
}

AccountStatePtr AccountManager::addAccount(const AccountPtr &newAccount)
{
    return addAccountState(AccountState::fromNewAccount(newAccount));
}

void AccountManager::deleteAccount(AccountStatePtr account)
{
    auto it = std::find(_accounts.begin(), _accounts.end(), account);
    if (it == _accounts.end()) {
        return;
    }
    // The argument keeps a strong reference to the AccountState, so we can safely remove other
    // AccountStatePtr occurrences:
    _accounts.erase(it);

    if (account->account()->hasDefaultSyncRoot()) {
        Utility::unmarkDirectoryAsSyncRoot(account->account()->defaultSyncRoot());
    }

    // Forget account credentials, cookies
    account->account()->credentials()->forgetSensitiveData();
    account->account()->credentialManager()->clear();

    Q_EMIT accountRemoved(account);
    Q_EMIT accountsChanged();
    account->deleteLater();
    save();
}

AccountPtr AccountManager::createAccount(const QUuid &uuid)
{
    AccountPtr acc = Account::create(uuid);
    return acc;
}

void AccountManager::shutdown()
{
    const auto accounts = std::move(_accounts);
    for (const auto &acc : accounts) {
        Q_EMIT accountRemoved(acc);
    }
    qDeleteAll(accounts);
}

AccountStatePtr AccountManager::addAccountState(std::unique_ptr<AccountState> &&accountState)
{
    auto *rawAccount = accountState->account().data();
    connect(rawAccount, &Account::wantsAccountSaved, this, &AccountManager::save);

    AccountStatePtr statePtr = accountState.release();
    _accounts.insert(statePtr->account()->uuid(), statePtr);
    Q_EMIT accountAdded(statePtr);
    Q_EMIT accountsChanged();
    return statePtr;
}
}
