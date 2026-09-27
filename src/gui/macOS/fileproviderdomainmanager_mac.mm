/*
 * Copyright (C) 2025 OpenCloud GmbH
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

#include "gui/accountmanager.h"
#include "libsync/account.h"
#include "libsync/graphapi/spacesmanager.h"
#include "libsync/theme.h"
#include "macOS/fileproviderdomainhistory.h"
#include "macOS/fileproviderdomainidentity.h"
#include "macOS/fileproviderdomainmanager.h"
#include "macOS/fileproviderxpc.h"
#include <QEventLoop>
#include <QFutureWatcher>
#include <QPromise>

#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <QUuid>

#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>

namespace OCC {
namespace Mac {

Q_LOGGING_CATEGORY(lcFileProviderDomainManager, "gui.fileprovider.domainmanager", QtInfoMsg)

// Helper to get domain identifier from account (uses account UUID)
static QString domainIdentifierFromAccount(const Account *account)
{
    if (!account) {
        return {};
    }
    return account->uuid().toString(QUuid::WithoutBraces);
}

static QString domainDisplayNameFromAccount(const Account *account)
{
    if (!account) {
        return {};
    }
    return account->davDisplayName().isEmpty()
           ? account->url().host()
           : QStringLiteral("%1 @ %2").arg(account->davDisplayName(), account->url().host());
}

// Private implementation class
class API_AVAILABLE(macos(11.0)) FileProviderDomainManager::MacImplementation
{
public:
    explicit MacImplementation(FileProviderDomainManager *owner)
        : _owner(owner)
    {
    }
    ~MacImplementation()
    {
        _registeredDomains.clear();
    }

    void findExistingFileProviderDomains(FileProviderDomainManager *owner)
    {
        const QPointer<FileProviderDomainManager> guard(owner);
        [NSFileProviderManager getDomainsWithCompletionHandler:^(NSArray<NSFileProviderDomain *> *domains, NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!guard) {
                    return;
                }
                if (error) {
                    qCWarning(lcFileProviderDomainManager)
                        << "Could not get existing file provider domains:" << QString::fromNSString(error.localizedDescription);
                    Q_EMIT guard->nativeError(QStringLiteral("discovery"), QString::fromNSString(error.localizedDescription));
                    QTimer::singleShot(std::chrono::seconds(3), guard, &FileProviderDomainManager::setupFileProviderDomains);
                    return;
                }
                Q_EMIT guard->nativeError(QStringLiteral("discovery"), {});
                for (NSFileProviderDomain *domain in domains) {
                    const auto domainId = QString::fromNSString(domain.identifier);
                    const auto account = FileProviderDomainManager::accountStateFromDomainIdentifier(domainId);
                    guard->d->_registeredDomains.insert(domainId, domain);
                    Q_EMIT guard->nativeError(domainId, {});
                    Q_EMIT guard->domainRegistered(domainId);
                    if (!account || account->isSignedOut()) {
                        // Keep orphan domains disabled until their extension has
                        // acknowledged removing persisted account credentials.
                        guard->d->disconnectDomain(domainId, FileProviderDomainManager::tr("This account has been removed."));
                    }
                }
                rememberFileProviderDomains(guard->d->_registeredDomains.keys());
                guard->updateFileProviderDomains();
            });
        }];
    }

    NSFileProviderDomain *domainForAccount(const AccountState *accountState)
    {
        if (!accountState || !accountState->account()) {
            return nil;
        }

        QString domainId = domainIdentifierFromAccount(accountState->account().get());
        return _registeredDomains.value(domainId, nil);
    }

    QString domainIdentifierForAccount(const AccountState *accountState) const
    {
        if (!accountState || !accountState->account()) {
            return {};
        }
        return domainIdentifierFromAccount(accountState->account().get());
    }

    void addFileProviderDomain(const AccountState *accountState, const GraphApi::Space *space, FileProviderDomainManager *owner)
    {
        if (!accountState || !accountState->account()) {
            return;
        }
        const auto account = accountState->account();
        const bool personal = space->drive().getDriveType() == QLatin1String("personal");
        const QString domainId = fileProviderDomainIdentifier(account->uuid(), space->id(), personal);
        const auto davPath = space->webdavUrl().path();
        const auto spacesPrefix = davPath.lastIndexOf(QLatin1String("/dav/spaces/"));
        auto spaceComponent = spacesPrefix < 0 ? QString() : davPath.mid(spacesPrefix + 12);
        if (spaceComponent.endsWith(QLatin1Char('/'))) {
            spaceComponent.chop(1);
        }
        // Shares is a virtual aggregation of other spaces. Its DAV root is
        // browsable, but it has no independent recycle bin or search scope.
        const bool supportsSearch =
            space->drive().getDriveType() != QLatin1String("virtual") && !spaceComponent.isEmpty() && !spaceComponent.contains(QLatin1Char('/'));
        if (_registeredDomains.contains(domainId)) {
            if (accountState->isSignedOut()) {
                disconnectDomain(domainId, FileProviderDomainManager::tr("You have been signed out."));
                return;
            }
            NSFileProviderDomain *existing = _registeredDomains.value(domainId);
            bool capabilitiesCurrent = true;
            if (@available(macOS 26.0, *)) {
                capabilitiesCurrent = existing.supportsStringSearchRequest == supportsSearch;
            }
            if (@available(macOS 13.0, *)) {
                capabilitiesCurrent &= existing.supportsSyncingTrash == supportsSearch;
            }
            if (capabilitiesCurrent) {
                reconnectDomain(domainId, owner);
                return;
            }
        }
        if (_pendingDomains.contains(domainId) || accountState->isSignedOut()) {
            return;
        }
        rememberActiveFileProviderDomain(domainId);
        _pendingDomains.insert(domainId);
        QTimer::singleShot(std::chrono::seconds(10), owner, [owner, domainId] {
            if (owner->d->_pendingDomains.remove(domainId)) {
                Q_EMIT owner->nativeError(domainId, FileProviderDomainManager::tr("Timed out registering on-demand files."));
            }
        });
        NSFileProviderDomain *domain = [[NSFileProviderDomain alloc]
            initWithIdentifier:domainId.toNSString()
                   displayName:(personal ? domainDisplayNameFromAccount(account.get())
                                         : QStringLiteral("%1 - %2").arg(space->displayName(), domainDisplayNameFromAccount(account.get())))
                                   .toNSString()];
        domain.hidden = NO;
        if (@available(macOS 26.0, *)) {
            domain.supportsStringSearchRequest = supportsSearch;
        }
        if (@available(macOS 13.0, *)) {
            domain.supportsSyncingTrash = supportsSearch;
        }
        const QPointer<FileProviderDomainManager> guard(owner);
        [NSFileProviderManager addDomain:domain
                       completionHandler:^(NSError *error) {
                           dispatch_async(dispatch_get_main_queue(), ^{
                               if (!guard) {
                                   return;
                               }
                               guard->d->_pendingDomains.remove(domainId);
                               if (error) {
                                   qCWarning(lcFileProviderDomainManager)
                                       << "Error adding domain:" << domainId << QString::fromNSString(error.localizedDescription);
                                   Q_EMIT guard->nativeError(domainId, QString::fromNSString(error.localizedDescription));
                                   QTimer::singleShot(std::chrono::seconds(3), guard, &FileProviderDomainManager::setupFileProviderDomains);
                                   return;
                               }
                               // An account can be removed while macOS is still adding its domain.
                               const auto currentAccount = FileProviderDomainManager::accountStateFromDomainIdentifier(domainId);
                               if (!currentAccount || !FileProviderDomainManager::spaceFromDomainIdentifier(domainId)) {
                                   guard->d->_registeredDomains.insert(domainId, domain);
                                   Q_EMIT guard->domainRegistered(domainId);
                                   guard->clearAccountConfiguration(domainId, true);
                                   Q_EMIT guard->domainSetupComplete();
                                   return;
                               }
                               guard->d->_registeredDomains.insert(domainId, domain);
                               Q_EMIT guard->nativeError(domainId, {});
                               Q_EMIT guard->domainRegistered(domainId);
                               if (currentAccount->isSignedOut()) {
                                   guard->d->disconnectDomain(domainId, FileProviderDomainManager::tr("You have been signed out."));
                               }
                               Q_EMIT guard->domainSetupComplete();
                           });
                       }];
    }

    void removeFileProviderDomain(QString domainId, FileProviderDomainManager *owner)
    {
        qCInfo(lcFileProviderDomainManager) << "Removing file provider domain:" << domainId;

        NSFileProviderDomain *domain = _registeredDomains.value(domainId);
        if (!domain) {
            qCWarning(lcFileProviderDomainManager) << "Domain not found:" << domainId;
            return;
        }

        const QPointer<FileProviderDomainManager> guard(owner);
        [NSFileProviderManager removeDomain:domain
                          completionHandler:^(NSError *error) {
                              dispatch_async(dispatch_get_main_queue(), ^{
                                  if (!guard) {
                                      return;
                                  }
                                  if (error) {
                                      qCWarning(lcFileProviderDomainManager)
                                          << "Error removing domain:" << domainId << QString::fromNSString(error.localizedDescription);
                                      Q_EMIT guard->nativeError(domainId, QString::fromNSString(error.localizedDescription));
                                      guard->d->disconnectDomain(domainId, FileProviderDomainManager::tr("This account has been removed."));
                                  } else {
                                      guard->d->_registeredDomains.remove(domainId);
                                      guard->d->_connectedDomains.remove(domainId);
                                      guard->d->_reconnectingDomains.remove(domainId);
                                      rememberFileProviderDomains(guard->d->_registeredDomains.keys());
                                      Q_EMIT guard->nativeError(domainId, {});
                                      Q_EMIT guard->domainRemoved(domainId);
                                  }
                              });
                          }];
    }

    void disconnectDomain(const AccountState *accountState, const QString &reason)
    {
        disconnectDomain(domainIdentifierFromAccount(accountState->account().get()), reason);
    }

    void disconnectDomain(QString domainId, const QString &reason)
    {
        NSFileProviderDomain *domain = _registeredDomains.value(domainId);
        if (!domain) {
            return;
        }
        _connectedDomains.remove(domainId);
        NSFileProviderManager *manager = [NSFileProviderManager managerForDomain:domain];
        const QPointer<FileProviderDomainManager> guard(_owner);
        if (!manager) {
            Q_EMIT _owner->nativeError(domainId, FileProviderDomainManager::tr("Could not open the on-demand domain to disconnect it."));
            return;
        }
        [manager disconnectWithReason:reason.toNSString()
                              options:0
                    completionHandler:^(NSError *error) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                            if (guard) {
                                Q_EMIT guard->nativeError(domainId, error ? QString::fromNSString(error.localizedDescription) : QString());
                            }
                        });
                    }];
    }

    void reconnectDomain(QString domainId, FileProviderDomainManager *owner)
    {
        if (_connectedDomains.contains(domainId) || _reconnectingDomains.contains(domainId)) {
            return;
        }
        NSFileProviderDomain *domain = _registeredDomains.value(domainId);
        NSFileProviderManager *manager = [NSFileProviderManager managerForDomain:domain];
        const QPointer<FileProviderDomainManager> guard(owner);
        if (!manager) {
            Q_EMIT owner->nativeError(domainId, FileProviderDomainManager::tr("Could not open the on-demand domain to reconnect it."));
            return;
        }
        rememberActiveFileProviderDomain(domainId);
        _reconnectingDomains.insert(domainId);
        [manager reconnectWithCompletionHandler:^(NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!guard) {
                    return;
                }
                guard->d->_reconnectingDomains.remove(domainId);
                const auto account = FileProviderDomainManager::accountStateFromDomainIdentifier(domainId);
                if (!account || account->isSignedOut()) {
                    guard->d->disconnectDomain(domainId, FileProviderDomainManager::tr("You have been signed out."));
                    return;
                }
                if (!error) {
                    guard->d->_connectedDomains.insert(domainId);
                }
                Q_EMIT guard->nativeError(domainId, error ? QString::fromNSString(error.localizedDescription) : QString());
            });
        }];
    }

    QStringList registeredDomainIds() const
    {
        return _registeredDomains.keys();
    }


private:
    FileProviderDomainManager *_owner;
    // Keys are domain identifiers (account UUIDs and encoded space IDs).
    QHash<QString, NSFileProviderDomain *> _registeredDomains;
    QSet<QString> _pendingDomains;
    QSet<QString> _connectedDomains;
    QSet<QString> _reconnectingDomains;
};

// FileProviderDomainManager implementation

FileProviderDomainManager::FileProviderDomainManager(QObject *parent, FileProviderXPC *xpc)
    : QObject(parent)
    , _xpc(xpc)
{
    if (_xpc) {
        connect(_xpc, &FileProviderXPC::domainConnected, this, [this](const QString &domainId) {
            const auto account = accountStateFromDomainIdentifier(domainId);
            const bool spaceRemoved = account && account->account()->spacesManager()->isReady() && !spaceFromDomainIdentifier(domainId);
            if (!account || account->isSignedOut() || spaceRemoved) {
                clearAccountConfiguration(domainId, !account || spaceRemoved);
            }
        });
    }
    if (@available(macOS 11.0, *)) {
        d = std::make_unique<MacImplementation>(this);
    } else {
        qCWarning(lcFileProviderDomainManager) << "FileProvider requires macOS 11.0 or later";
    }
}

FileProviderDomainManager::~FileProviderDomainManager() = default;

void FileProviderDomainManager::start()
{
    if (!d) {
        return;
    }

    qCInfo(lcFileProviderDomainManager) << "Starting FileProvider domain manager";
    // Connect to account manager signals
    connect(AccountManager::instance(), &AccountManager::accountAdded,
            this, [this](AccountStatePtr accountState) {
                addFileProviderDomainForAccount(accountState.data());
            });

    connect(AccountManager::instance(), &AccountManager::accountDeleted, this,
        [this](AccountStatePtr accountState) { removeFileProviderDomainForAccount(accountState.data()); });
    setupFileProviderDomains();
}

void FileProviderDomainManager::setupFileProviderDomains()
{
    if (!d) {
        return;
    }

    d->findExistingFileProviderDomains(this);
}

void FileProviderDomainManager::updateFileProviderDomains()
{
    if (!d) {
        return;
    }

    const auto accounts = AccountManager::instance()->accounts();
    qCDebug(lcFileProviderDomainManager) << "Updating file provider domains";

    // Existing domains need the same sign-out handlers as newly created ones.
    for (const auto &accountState : accounts) {
        addFileProviderDomainForAccount(accountState.data());
    }

    Q_EMIT domainSetupComplete();
}

void FileProviderDomainManager::addFileProviderDomainForAccount(const AccountState *accountState)
{
    if (!d || !accountState) {
        return;
    }

    connect(accountState, &AccountState::stateChanged, this, &FileProviderDomainManager::slotAccountStateChanged, Qt::UniqueConnection);
    const auto accountId = domainIdentifierFromAccount(accountState->account().get());
    if (!_watchedAccounts.contains(accountId)) {
        _watchedAccounts.insert(accountId);
        const QPointer<const AccountState> guard(accountState);
        connect(accountState->account()->spacesManager(), &GraphApi::SpacesManager::updated, this, [this, guard] {
            if (guard) {
                reconcileSpaces(guard);
            }
        });
    }
    reconcileSpaces(accountState);
}

void FileProviderDomainManager::removeFileProviderDomainForAccount(const AccountState *accountState)
{
    if (!d || !accountState) {
        return;
    }

    const auto accountId = accountState->account()->uuid();
    _watchedAccounts.remove(accountId.toString(QUuid::WithoutBraces));
    for (const auto &domainId : d->registeredDomainIds()) {
        const auto identity = fileProviderDomainIdentity(domainId);
        if (identity && identity->account == accountId) {
            clearAccountConfiguration(domainId, true);
        }
    }
}

void FileProviderDomainManager::clearAccountConfiguration(const QString &domainId, bool removeDomain)
{
    const QPointer<FileProviderDomainManager> guard(this);
    auto complete = [guard, domainId, removeDomain](bool success) {
        if (!guard) {
            return;
        }
        const auto account = accountStateFromDomainIdentifier(domainId);
        // Signing back in while the reply was pending supersedes a sign-out.
        if (account && ((!removeDomain && !account->isSignedOut()) || (removeDomain && spaceFromDomainIdentifier(domainId)))) {
            return;
        }
        if (removeDomain && success) {
            guard->d->removeFileProviderDomain(domainId, guard);
            if (guard->_xpc) {
                guard->_xpc->closeConnection(domainId);
            }
        } else {
            // An unavailable extension must not keep synchronizing. Preserve its
            // domain on cleanup failure, and do not claim its credentials were erased.
            guard->d->disconnectDomain(domainId, tr("You have been signed out."));
        }
    };
    if (_xpc) {
        _xpc->clearAccountConfiguration(domainId, std::move(complete));
    } else {
        complete(false);
    }
}

void FileProviderDomainManager::slotAccountStateChanged(AccountState::State state)
{
    if (!d) {
        return;
    }

    auto *accountState = qobject_cast<AccountState *>(sender());
    if (!accountState) {
        return;
    }

    qCDebug(lcFileProviderDomainManager) << "Account state changed:" << state
                                         << "for" << accountState->account()->davDisplayName();

    switch (state) {
    case AccountState::SignedOut:
        for (const auto &domainId : d->registeredDomainIds()) {
            const auto identity = fileProviderDomainIdentity(domainId);
            if (identity && identity->account == accountState->account()->uuid()) {
                clearAccountConfiguration(domainId, false);
            }
        }
        break;
    case AccountState::Disconnected:
        // Don't disconnect on transient state. Network hiccups cause
        // Disconnected→Connecting→Connected transitions; calling disconnectDomain
        // each time makes the system mark the extension as temporarily unavailable,
        // and if reconnect fails the domain stays disabled.
        break;
    case AccountState::Connected:
        reconcileSpaces(accountState);
        break;
    case AccountState::Connecting:
        // Do nothing while connecting
        break;
    }
}

void FileProviderDomainManager::reconcileSpaces(const AccountState *accountState)
{
    if (!d || !accountState || accountState->isSignedOut()) {
        return;
    }
    const auto spaces = accountState->account()->spacesManager()->spaces();
    QSet<QString> desired;
    for (const auto *space : spaces) {
        if (space->disabled() || !space->webdavUrl().isValid() || space->webdavUrl().isEmpty()) {
            continue;
        }
        desired.insert(fileProviderDomainIdentifier(accountState->account()->uuid(), space->id(), space->drive().getDriveType() == QLatin1String("personal")));
        d->addFileProviderDomain(accountState, space, this);
    }
    // An empty list may be a discovery failure. Keep existing locations disabled
    // or available from cache until we have an authoritative spaces response.
    if (accountState->account()->spacesManager()->isReady()) {
        for (const auto &domainId : d->registeredDomainIds()) {
            const auto identity = fileProviderDomainIdentity(domainId);
            if (identity && identity->account == accountState->account()->uuid() && !desired.contains(domainId)) {
                clearAccountConfiguration(domainId, true);
            }
        }
    }
}

AccountStatePtr FileProviderDomainManager::accountStateFromDomainIdentifier(const QString &domainIdentifier)
{
    const auto identity = fileProviderDomainIdentity(domainIdentifier);
    if (!identity) {
        return {};
    }
    for (const auto &account : AccountManager::instance()->accounts()) {
        if (account->account()->uuid() == identity->account) {
            return account;
        }
    }
    return {};
}

GraphApi::Space *FileProviderDomainManager::spaceFromDomainIdentifier(const QString &domainIdentifier)
{
    const auto identity = fileProviderDomainIdentity(domainIdentifier);
    const auto account = accountStateFromDomainIdentifier(domainIdentifier);
    if (!identity || !account) {
        return nullptr;
    }
    for (auto *space : account->account()->spacesManager()->spaces()) {
        if (!space->disabled()
            && (identity->spaceId.isEmpty() ? space->drive().getDriveType() == QLatin1String("personal") : space->id() == identity->spaceId)) {
            return space;
        }
    }
    return nullptr;
}

QString FileProviderDomainManager::domainIdentifierForAccount(const AccountState *accountState) const
{
    if (!d) {
        return {};
    }
    return d->domainIdentifierForAccount(accountState);
}

void *FileProviderDomainManager::domainForAccount(const AccountState *accountState) const
{
    if (!d) {
        return nullptr;
    }
    return (__bridge void *)d->domainForAccount(accountState);
}

Result<void, QString> FileProviderDomainManager::removeAllDomains()
{
    if (!d) {
        return tr("FileProvider is unavailable on this macOS version.");
    }
    auto promise = std::make_shared<QPromise<QString>>();
    promise->start();
    QFutureWatcher<QString> watcher;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    watcher.setFuture(promise->future());
    const auto appGroup = _xpc ? _xpc->appGroupIdentifier() : QString();
    [NSFileProviderManager getDomainsWithCompletionHandler:^(NSArray<NSFileProviderDomain *> *domains, NSError *error) {
        if (error) {
            promise->addResult(QString::fromNSString(error.localizedDescription));
            promise->finish();
            return;
        }
        QStringList domainIds;
        for (NSFileProviderDomain *domain in domains) {
            domainIds.append(QString::fromNSString(domain.identifier));
        }
        rememberFileProviderDomains(domainIds);
        dispatch_group_t group = dispatch_group_create();
        for (NSFileProviderDomain *domain in domains) {
            if (!fileProviderDomainIdentity(QString::fromNSString(domain.identifier))) {
                continue;
            }
            const auto recorded = FileProviderXPC::recordAccountRemoval(QString::fromNSString(domain.identifier), appGroup);
            if (!recorded) {
                promise->addResult(recorded.error());
                continue;
            }
            dispatch_group_enter(group);
            [NSFileProviderManager removeDomain:domain
                              completionHandler:^(NSError *removeError) {
                                  if (removeError) {
                                      promise->addResult(QString::fromNSString(removeError.localizedDescription));
                                  }
                                  dispatch_group_leave(group);
                              }];
        }
        dispatch_group_notify(group, dispatch_get_main_queue(), ^{ promise->finish(); });
    }];
    timeout.start(std::chrono::seconds(30));
    loop.exec();
    if (!watcher.isFinished()) {
        return tr("Timed out while removing FileProvider domains.");
    }
    const auto errors = watcher.future().results();
    if (!errors.isEmpty()) {
        return QStringList(errors).join(QLatin1Char('\n'));
    }
    rememberFileProviderDomains({});
    return {};
}

} // namespace Mac
} // namespace OCC
