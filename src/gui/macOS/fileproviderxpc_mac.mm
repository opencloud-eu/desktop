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

#include "gui/guiutility.h"
#include "libsync/theme.h"
#include "macOS/fileprovider.h"
#include "macOS/fileproviderdomainidentity.h"
#include "macOS/fileproviderdomainmanager.h"
#include "macOS/fileproviderxpc.h"
#include <QDir>
#include <QFileInfo>

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QTimer>

#include "common/utility.h"
#include "gui/accountmanager.h"
#include "libsync/account.h"
#include "libsync/creds/abstractcredentials.h"
#include "libsync/creds/httpcredentials.h"
#include "libsync/graphapi/spacesmanager.h"
#include "libsync/graphapi/space.h"

#import <Foundation/Foundation.h>
#import <FileProvider/FileProvider.h>

// Import the protocol header from the extension
#import "ClientCommunicationProtocol.h"

namespace {
    constexpr int64_t semaphoreWaitDelta = 3000000000; // 3 seconds
    NSString *const clientCommunicationServiceName = @"eu.opencloud.desktop.ClientCommunicationService";
}

static NSString *credentialSuiteIdentifier(const QString &appGroupIdentifier)
{
    NSURL *extensionURL = [NSBundle.mainBundle.builtInPlugInsURL URLByAppendingPathComponent:@"FileProviderExt.appex"];
    NSString *suite = appGroupIdentifier.isEmpty() ? [[NSBundle bundleWithURL:extensionURL] objectForInfoDictionaryKey:@"AppGroupIdentifier"]
                                                   : appGroupIdentifier.toNSString();
    if (!suite.length) {
        const auto directory = QFileInfo(OCC::Utility::socketApiSocketPath()).dir();
        suite = (directory.absolutePath().contains(QStringLiteral("/Group Containers/")) ? directory.dirName() : OCC::Theme::instance()->orgDomainName())
                    .toNSString();
    }
    return suite;
}

static bool persistCredentialState(const QString &appGroupIdentifier, NSDictionary<NSString *, id> *values)
{
    NSString *identifier = credentialSuiteIdentifier(appGroupIdentifier);
    if (!identifier.length) {
        return false;
    }
    const auto suite = (__bridge CFStringRef)identifier;
    // NSUserDefaults.synchronize also visits AnyUser/ByHost domains. In a macOS
    // app group it can fail there even after our current-user values were saved.
    // Flush and verify exactly the domain shared with the extension instead.
    CFPreferencesSetMultiple((__bridge CFDictionaryRef)values, nullptr, suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    if (!CFPreferencesSynchronize(suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost)) {
        return false;
    }
    for (NSString *key in values) {
        const auto stored = CFPreferencesCopyValue((__bridge CFStringRef)key, suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        const bool matches = stored && [(__bridge id)stored isEqual:values[key]];
        if (stored) {
            CFRelease(stored);
        }
        if (!matches) {
            return false;
        }
    }
    return true;
}

namespace OCC {
namespace Mac {

Q_LOGGING_CATEGORY(lcFileProviderXPC, "gui.fileprovider.xpc", QtInfoMsg)

FileProviderXPC::FileProviderXPC(QObject *parent, const QString &appGroupIdentifier)
    : QObject(parent)
    , _appGroupIdentifier(appGroupIdentifier)
{
    // Periodically re-send credentials to the extension so it always has a fresh
    // OAuth token.  Tokens typically expire in 5-15 minutes; re-sending every
    // 4 minutes keeps the extension authenticated.
    _credentialRefreshTimer = new QTimer(this);
    _credentialRefreshTimer->setInterval(std::chrono::minutes(4));
    connect(_credentialRefreshTimer, &QTimer::timeout, this, &FileProviderXPC::refreshCredentials);
    _credentialRefreshTimer->start();
    auto *statusTimer = new QTimer(this);
    statusTimer->setInterval(std::chrono::seconds(2));
    connect(statusTimer, &QTimer::timeout, this, &FileProviderXPC::refreshSyncStatus);
    statusTimer->start();
    auto watchAccount = [this](const AccountStatePtr &state) {
        connect(state.data(), &AccountState::stateChanged, this, &FileProviderXPC::slotAccountStateChanged, Qt::UniqueConnection);
        const auto accountId = state->account()->uuid();
        connect(state->account().data(), &Account::credentialsFetched, this, [this, accountId] { authenticateAccountDomains(accountId); });
        connect(state->account()->spacesManager(), &GraphApi::SpacesManager::updated, this, [this, accountId] { authenticateAccountDomains(accountId); });
    };
    connect(AccountManager::instance(), &AccountManager::accountAdded, this, watchAccount);
    for (const auto &state : AccountManager::instance()->accounts()) {
        watchAccount(state);
    }
}

FileProviderXPC::~FileProviderXPC()
{
    clearConnections();
}

void FileProviderXPC::clearConnections()
{
    for (void *ptr : std::as_const(_connections)) {
        NSXPCConnection *connection = (__bridge_transfer NSXPCConnection *)ptr;
        connection.invalidationHandler = nil;
        connection.interruptionHandler = nil;
        [connection invalidate];
    }
    _connections.clear();
    for (void *ptr : std::as_const(_clientCommServices)) {
        (void)(__bridge_transfer id)ptr;
    }
    _clientCommServices.clear();
    for (void *ptr : std::as_const(_serviceLeases)) {
        (void)(__bridge_transfer NSFileProviderService *)ptr;
    }
    _serviceLeases.clear();
    for (void *ptr : std::as_const(_managerLeases)) {
        (void)(__bridge_transfer NSFileProviderManager *)ptr;
    }
    _managerLeases.clear();
    _configurationRequests.clear();
    _statusRequests.clear();
    _statusDomains.clear();
    _authenticationRetries.clear();
    _connectionRequests.clear();
    _pendingDomains.clear();
}

void FileProviderXPC::connectToFileProviderDomains()
{
    if (_discoveryPending) {
        return;
    }
    _discoveryPending = true;
    const QPointer<FileProviderXPC> guard(this);
    [NSFileProviderManager getDomainsWithCompletionHandler:^(NSArray<NSFileProviderDomain *> *domains, NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!guard) {
                return;
            }
            guard->_discoveryPending = false;
            if (error) {
                qCWarning(lcFileProviderXPC) << "Could not discover FileProvider domains:" << QString::fromNSString(error.localizedDescription);
                Q_EMIT guard->domainStatusChanged(QStringLiteral("discovery"), QString::fromNSString(error.localizedDescription));
                QTimer::singleShot(std::chrono::seconds(3), guard, &FileProviderXPC::connectToFileProviderDomains);
                return;
            }
            Q_EMIT guard->domainStatusChanged(QStringLiteral("discovery"), {});
            for (NSFileProviderDomain *domain in domains) {
                const auto domainId = QString::fromNSString(domain.identifier);
                if (guard->_clientCommServices.contains(domainId) || guard->_pendingDomains.contains(domainId)) {
                    continue;
                }
                NSFileProviderManager *manager = [NSFileProviderManager managerForDomain:domain];
                if (!manager) {
                    Q_EMIT guard->domainStatusChanged(domainId, tr("Could not open the on-demand domain."));
                    continue;
                }
                guard->_pendingDomains.insert(domainId);
                const auto request = std::make_shared<bool>(false);
                guard->_connectionRequests.insert(domainId, request);
                QTimer::singleShot(std::chrono::seconds(10), guard, [guard, domainId, request] {
                    if (guard && guard->_connectionRequests.value(domainId) == request && guard->_pendingDomains.contains(domainId)) {
                        guard->closeConnection(domainId);
                        Q_EMIT guard->domainStatusChanged(domainId, tr("Timed out connecting to the on-demand provider."));
                    }
                });
                auto failed = ^(NSError *failure) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        if (guard && guard->_connectionRequests.value(domainId) == request) {
                            if (guard->_connections.contains(domainId) && [failure.domain isEqualToString:NSCocoaErrorDomain]
                                && (failure.code == NSXPCConnectionInterrupted || failure.code == NSXPCConnectionInvalid)) {
                                guard->reconnectAfterInvalidation(domainId);
                                return;
                            }
                            guard->closeConnection(domainId);
                            qCWarning(lcFileProviderXPC)
                                << "FileProvider connection failed for" << domainId << QString::fromNSString(failure.localizedDescription);
                            Q_EMIT guard->domainStatusChanged(
                                domainId, tr("Could not connect to the on-demand provider: %1").arg(QString::fromNSString(failure.localizedDescription)));
                            const auto account = FileProviderDomainManager::accountStateFromDomainIdentifier(domainId);
                            if (account && !account->isSignedOut()) {
                                QTimer::singleShot(std::chrono::seconds(3), guard, &FileProviderXPC::connectToFileProviderDomains);
                            }
                        }
                    });
                };
                auto serviceReady = ^(NSFileProviderService *service, NSError *serviceError) {
                    if (serviceError || !service) {
                        failed(serviceError);
                        return;
                    }
                    [service getFileProviderConnectionWithCompletionHandler:^(NSXPCConnection *connection, NSError *connectionError) {
                        if (connectionError || !connection) {
                            failed(connectionError);
                            return;
                        }
                        connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(ClientCommunicationProtocol)];
                        connection.invalidationHandler = ^{
                            dispatch_async(dispatch_get_main_queue(), ^{
                                if (guard && guard->_connectionRequests.value(domainId) == request) {
                                    guard->_pendingDomains.remove(domainId);
                                    guard->reconnectAfterInvalidation(domainId);
                                }
                            });
                        };
                        [connection resume];
                        id<ClientCommunicationProtocol> proxy = [connection remoteObjectProxyWithErrorHandler:failed];
                        [proxy getFileProviderDomainIdentifierWithCompletionHandler:^(NSString *extensionDomainId, NSError *idError) {
                            dispatch_async(dispatch_get_main_queue(), ^{
                                if (!guard || guard->_connectionRequests.value(domainId) != request) {
                                    connection.invalidationHandler = nil;
                                    [connection invalidate];
                                    return;
                                }
                                guard->_pendingDomains.remove(domainId);
                                // Bind a service to the domain requested from the system before sending credentials.
                                if (idError || QString::fromNSString(extensionDomainId) != domainId) {
                                    Q_EMIT guard->domainStatusChanged(domainId, tr("The on-demand provider returned an unexpected domain identity."));
                                    connection.invalidationHandler = nil;
                                    [connection invalidate];
                                    return;
                                }
                                if (guard->_clientCommServices.contains(domainId)) {
                                    connection.invalidationHandler = nil;
                                    [connection invalidate];
                                    return;
                                }
                                guard->_connections.insert(domainId, (__bridge_retained void *)connection);
                                guard->_serviceLeases.insert(domainId, (__bridge_retained void *)service);
                                guard->_managerLeases.insert(domainId, (__bridge_retained void *)manager);
                                guard->_clientCommServices.insert(domainId, (__bridge_retained void *)proxy);
                                Q_EMIT guard->domainConnected(domainId);
                                guard->authenticateFileProviderDomain(domainId);
                            });
                        }];
                    }];
                };
                if (@available(macOS 13.0, *)) {
                    [manager getServiceWithName:clientCommunicationServiceName
                                 itemIdentifier:NSFileProviderRootContainerItemIdentifier
                              completionHandler:serviceReady];
                } else {
                    [manager getUserVisibleURLForItemIdentifier:NSFileProviderRootContainerItemIdentifier
                                              completionHandler:^(NSURL *url, NSError *urlError) {
                        if (urlError || !url) {
                            failed(urlError);
                            return;
                        }
                        [NSFileManager.defaultManager
                            getFileProviderServicesForItemAtURL:url
                                              completionHandler:^(NSDictionary<NSFileProviderServiceName, NSFileProviderService *> *services,
                                                  NSError *servicesError) { serviceReady(services[clientCommunicationServiceName], servicesError); }];
                    }];
                }
            }
        });
    }];
}

void FileProviderXPC::authenticateFileProviderDomains()
{
    qCInfo(lcFileProviderXPC) << "Authenticating all file provider domains...";

    for (const auto &domainId : _clientCommServices.keys()) {
        authenticateFileProviderDomain(domainId);
    }
}

void FileProviderXPC::authenticateFileProviderDomain(QString domainIdentifier)
{
    qCInfo(lcFileProviderXPC) << "Authenticating domain:" << domainIdentifier;
    if (_cleanupCallbacks.contains(domainIdentifier)) {
        return;
    }

    // Find the account for this domain
    const auto accountState = FileProviderDomainManager::accountStateFromDomainIdentifier(domainIdentifier);
    if (!accountState) {
        qCWarning(lcFileProviderXPC) << "No account found for domain:" << domainIdentifier;
        Q_EMIT domainConnected(domainIdentifier);
        return;
    }

    // Always connect to account state changes so we retry when token becomes available
    connect(accountState.data(), &AccountState::stateChanged,
            this, &FileProviderXPC::slotAccountStateChanged, Qt::UniqueConnection);

    if (accountState->isSignedOut()) {
        unauthenticateFileProviderDomain(domainIdentifier);
        return;
    }

    const auto account = accountState->account();
    if (!account) {
        qCWarning(lcFileProviderXPC) << "Account is null for domain:" << domainIdentifier;
        return;
    }

    const auto credentials = account->credentials();
    if (!credentials) {
        qCWarning(lcFileProviderXPC) << "Credentials are null for domain:" << domainIdentifier;
        return;
    }

    // Get user info
    NSString *user = account->davDisplayName().toNSString();
    NSString *userId = account->uuid().toString(QUuid::WithoutBraces).toNSString();
    NSString *serverUrl = account->url().toString().toNSString();

    // Get password/token - for OAuth, get the access token from HttpCredentials
    NSString *password = @"";
    if (auto *httpCreds = qobject_cast<HttpCredentials *>(credentials)) {
        QString accessToken = httpCreds->accessToken();
        if (!accessToken.isEmpty()) {
            password = accessToken.toNSString();
            qCDebug(lcFileProviderXPC) << "Using access token for authentication";
        } else {
            qCInfo(lcFileProviderXPC) << "Access token not yet available for domain:" << domainIdentifier;
            return;
        }
    } else {
        qCWarning(lcFileProviderXPC) << "Credentials are not HttpCredentials";
        return;
    }

    const auto *space = FileProviderDomainManager::spaceFromDomainIdentifier(domainIdentifier);
    if (!space || space->webdavUrl().isEmpty()) {
        Q_EMIT domainStatusChanged(domainIdentifier, tr("Waiting for space discovery."));
        return;
    }
    const auto webdavUrl = space->webdavUrl();
    // Use the advertised space origin as well as its path, including gateways
    // whose WebDAV endpoint differs from the account's login URL.
    serverUrl = webdavUrl.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString().toNSString();
    NSString *davPath = webdavUrl.path().toNSString();

    // Current code only reaches here for HttpCredentials with a valid OAuth access token
    NSString *authType = @"bearer";

    // Get the service proxy
    void *servicePtr = _clientCommServices.value(domainIdentifier);
    if (!servicePtr) {
        qCWarning(lcFileProviderXPC) << "No service connection for domain:" << domainIdentifier;
        return;
    }

    NSObject<ClientCommunicationProtocol> *service = (__bridge NSObject<ClientCommunicationProtocol> *)servicePtr;

    NSString *generation = QUuid::createUuid().toString(QUuid::WithoutBraces).toNSString();
    if (!persistCredentialState(_appGroupIdentifier, @{[@"fp_config_generation_" stringByAppendingString:domainIdentifier.toNSString()] : generation})) {
        qCWarning(lcFileProviderXPC) << "Could not persist the credential generation for domain:" << domainIdentifier;
        Q_EMIT domainStatusChanged(domainIdentifier, tr("Could not persist the on-demand credential generation."));
        return;
    }
    qCInfo(lcFileProviderXPC) << "Sending credentials to domain:" << domainIdentifier;
    const QPointer<FileProviderXPC> guard(this);
    const auto completed = std::make_shared<bool>(false);
    _configurationRequests.insert(domainIdentifier, completed);
    [service configureAccountWithUser:user
                               userId:userId
                            serverUrl:serverUrl
                             password:password
                              davPath:davPath
                             authType:authType
                           generation:generation
                    completionHandler:^(NSError *error) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                            *completed = true;
                            if (guard && guard->_configurationRequests.value(domainIdentifier) == completed) {
                                guard->_configurationRequests.remove(domainIdentifier);
                                const auto account = FileProviderDomainManager::accountStateFromDomainIdentifier(domainIdentifier);
                                if (account && !account->isSignedOut()) {
                                    if (error) {
                                        qCWarning(lcFileProviderXPC) << "Provider rejected account configuration for domain:" << domainIdentifier
                                                                     << "error domain:" << QString::fromNSString(error.domain) << "code:" << error.code;
                                    }
                                    Q_EMIT guard->domainStatusChanged(domainIdentifier, error ? QString::fromNSString(error.localizedDescription) : QString());
                                    if (!error) {
                                        guard->_authenticationRetries.remove(domainIdentifier);
                                        guard->_statusDomains.insert(domainIdentifier);
                                        guard->refreshSyncStatus();
                                    } else if (guard->_authenticationRetries.value(domainIdentifier) < 3) {
                                        ++guard->_authenticationRetries[domainIdentifier];
                                        QTimer::singleShot(std::chrono::seconds(1), guard, [guard, domainIdentifier] {
                                            if (guard && guard->_authenticationRetries.contains(domainIdentifier)) {
                                                guard->authenticateFileProviderDomain(domainIdentifier);
                                            }
                                        });
                                    }
                                }
                            }
                        });
                    }];
    QTimer::singleShot(std::chrono::seconds(10), this, [this, domainIdentifier, completed] {
        if (!*completed && _configurationRequests.value(domainIdentifier) == completed) {
            Q_EMIT domainStatusChanged(domainIdentifier, tr("The on-demand provider did not acknowledge authentication."));
        }
    });
}

void FileProviderXPC::unauthenticateFileProviderDomain(const QString &domainIdentifier)
{
    clearAccountConfiguration(domainIdentifier, {});
}

void FileProviderXPC::closeConnection(const QString &domainIdentifier)
{
    _statusRequests.remove(domainIdentifier);
    _statusDomains.remove(domainIdentifier);
    _configurationRequests.remove(domainIdentifier);
    _authenticationRetries.remove(domainIdentifier);
    _connectionRequests.remove(domainIdentifier);
    _pendingDomains.remove(domainIdentifier);
    if (void *ptr = _clientCommServices.take(domainIdentifier)) {
        (void)(__bridge_transfer id)ptr;
    }
    if (void *ptr = _connections.take(domainIdentifier)) {
        NSXPCConnection *connection = (__bridge_transfer NSXPCConnection *)ptr;
        connection.invalidationHandler = nil;
        connection.interruptionHandler = nil;
        [connection invalidate];
    }
    if (void *ptr = _serviceLeases.take(domainIdentifier)) {
        (void)(__bridge_transfer NSFileProviderService *)ptr;
    }
    if (void *ptr = _managerLeases.take(domainIdentifier)) {
        (void)(__bridge_transfer NSFileProviderManager *)ptr;
    }
}

void FileProviderXPC::authenticateAccountDomains(const QUuid &account)
{
    for (const auto &domainId : _clientCommServices.keys()) {
        const auto identity = fileProviderDomainIdentity(domainId);
        if (identity && identity->account == account) {
            authenticateFileProviderDomain(domainId);
        }
    }
}

Result<void, QString> FileProviderXPC::recordAccountRemoval(const QString &domainIdentifier, const QString &appGroupIdentifier)
{
    if (!persistCredentialState(
            appGroupIdentifier, @{
                [@"fp_config_generation_" stringByAppendingString:domainIdentifier.toNSString()] : QUuid::createUuid()
                    .toString(QUuid::WithoutBraces)
                    .toNSString(),
                [@"fp_removed_domain_" stringByAppendingString:domainIdentifier.toNSString()] : @YES
            })) {
        qCWarning(lcFileProviderXPC) << "Could not persist credential cleanup for domain:" << domainIdentifier;
        return tr("Could not persist credential cleanup for on-demand domain %1.").arg(domainIdentifier);
    }
    return {};
}

void FileProviderXPC::clearAccountConfiguration(const QString &domainIdentifier, std::function<void(bool)> completion)
{
    _statusRequests.remove(domainIdentifier);
    _statusDomains.remove(domainIdentifier);
    const bool pending = _cleanupCallbacks.contains(domainIdentifier);
    _configurationRequests.remove(domainIdentifier);
    _authenticationRetries.remove(domainIdentifier);
    Q_EMIT domainStatusChanged(domainIdentifier, tr("On-demand files are disconnected while account credentials are removed."));
    _cleanupCallbacks[domainIdentifier].append(std::move(completion));
    if (pending) {
        return;
    }
    const auto recorded = recordAccountRemoval(domainIdentifier, _appGroupIdentifier);
    if (!recorded) {
        Q_EMIT domainStatusChanged(domainIdentifier, recorded.error());
        finishClearingAccount(domainIdentifier, false);
        return;
    }
    void *ptr = _clientCommServices.value(domainIdentifier);
    if (!ptr) {
        finishClearingAccount(domainIdentifier, false);
        return;
    }
    const QPointer<FileProviderXPC> guard(this);
    const auto completed = std::make_shared<bool>(false);
    auto finish = [guard, domainIdentifier, completed](bool success) {
        // Both the native reply and deadline are delivered on the main queue.
        // A late reply must never complete a newer cleanup request for this domain.
        if (*completed) {
            // A delayed server-side clear may finish after the user signs back in.
            // Restore current credentials without completing another request.
            if (success && guard) {
                const auto account = FileProviderDomainManager::accountStateFromDomainIdentifier(domainIdentifier);
                if (account && !account->isSignedOut()) {
                    guard->authenticateFileProviderDomain(domainIdentifier);
                }
            }
            return;
        }
        *completed = true;
        if (guard) {
            guard->finishClearingAccount(domainIdentifier, success);
        }
    };
    NSObject<ClientCommunicationProtocol> *service = (__bridge NSObject<ClientCommunicationProtocol> *)ptr;
    const auto storedGeneration = CFPreferencesCopyValue((__bridge CFStringRef)[@"fp_config_generation_" stringByAppendingString:domainIdentifier.toNSString()],
        (__bridge CFStringRef)credentialSuiteIdentifier(_appGroupIdentifier), kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    const QString generation =
        storedGeneration && CFGetTypeID(storedGeneration) == CFStringGetTypeID() ? QString::fromNSString((__bridge NSString *)storedGeneration) : QString();
    if (storedGeneration) {
        CFRelease(storedGeneration);
    }
    if (generation.isEmpty()) {
        finish(false);
        return;
    }
    [service removeAccountConfigWithGeneration:generation.toNSString()
                             completionHandler:^(NSError *error) { dispatch_async(dispatch_get_main_queue(), ^{ finish(error == nil); }); }];
    QTimer::singleShot(std::chrono::seconds(3), this, [finish] { finish(false); });
}

void FileProviderXPC::finishClearingAccount(const QString &domainIdentifier, bool success)
{
    if (!_cleanupCallbacks.contains(domainIdentifier)) {
        return;
    }
    if (!success) {
        qCWarning(lcFileProviderXPC) << "Credential removal was not acknowledged for domain:" << domainIdentifier;
        Q_EMIT domainStatusChanged(domainIdentifier, tr("Credential cleanup is pending. On-demand files are disconnected."));
    }
    const auto callbacks = _cleanupCallbacks.take(domainIdentifier);
    for (const auto &callback : callbacks) {
        if (callback) {
            callback(success);
        }
    }
    const auto account = FileProviderDomainManager::accountStateFromDomainIdentifier(domainIdentifier);
    if (account && !account->isSignedOut() && FileProviderDomainManager::spaceFromDomainIdentifier(domainIdentifier)) {
        authenticateFileProviderDomain(domainIdentifier);
    }
}

bool FileProviderXPC::fileProviderDomainReachable(const QString &domainIdentifier)
{
    void *servicePtr = _clientCommServices.value(domainIdentifier);
    if (!servicePtr) {
        return false;
    }

    NSObject<ClientCommunicationProtocol> *service = (__bridge NSObject<ClientCommunicationProtocol> *)servicePtr;

    __block BOOL reachable = NO;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [service getFileProviderDomainIdentifierWithCompletionHandler:^(NSString *domainId, NSError *error) {
        reachable = (error == nil && domainId != nil);
        dispatch_semaphore_signal(semaphore);
    }];

    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, semaphoreWaitDelta));

    return reachable;
}

void FileProviderXPC::slotAccountStateChanged(AccountState::State state)
{
    auto *accountState = qobject_cast<AccountState *>(sender());
    if (!accountState) {
        return;
    }

    const QString domainId = accountState->account()->uuid().toString(QUuid::WithoutBraces);

    qCDebug(lcFileProviderXPC) << "Account state changed for domain:" << domainId << "state:" << state;

    switch (state) {
    case AccountState::SignedOut:
        // The domain manager clears credentials before disconnecting the domain.
        break;
    case AccountState::Disconnected:
        // Don't unauthenticate on transient disconnections (network hiccup,
        // token refresh). The extension keeps working with cached credentials.
        // Only SignedOut should remove credentials.
        break;
    case AccountState::Connected:
        connectToFileProviderDomains();
        authenticateAccountDomains(accountState->account()->uuid());
        break;
    case AccountState::Connecting:
        // Do nothing while connecting
        break;
    }
}

void FileProviderXPC::reconnectAfterInvalidation(const QString &domainIdentifier)
{
    // macOS can retire a service connection while the provider remains healthy.
    // Invalidate only this domain's readiness; actual retry failures still report
    // errors, and errors already received from the provider remain visible.
    closeConnection(domainIdentifier);
    Q_EMIT domainReconnecting(domainIdentifier);
    qCInfo(lcFileProviderXPC) << "XPC connection invalidated for domain:" << domainIdentifier;
    if (_reconnectPending) {
        return;
    }
    _reconnectPending = true;

    // Delay to allow the new extension process to start.
    // connectToFileProviderDomains is non-blocking and auto-authenticates
    // when connections are established.
    QTimer::singleShot(std::chrono::seconds(3), this, [this]() {
        _reconnectPending = false;
        qCInfo(lcFileProviderXPC) << "Reconnecting to FileProvider domains after invalidation";
        connectToFileProviderDomains();
    });
}

void FileProviderXPC::refreshSyncStatus()
{
    const QPointer<FileProviderXPC> guard(this);
    for (const auto &domainId : std::as_const(_statusDomains)) {
        if (_statusRequests.contains(domainId) || _cleanupCallbacks.contains(domainId)) {
            continue;
        }
        void *ptr = _clientCommServices.value(domainId);
        if (!ptr) {
            continue;
        }
        const auto request = std::make_shared<bool>(false);
        _statusRequests.insert(domainId, request);
        const QString identifier = domainId; // Own the value captured by Objective-C blocks.
        NSObject<ClientCommunicationProtocol> *service = (__bridge NSObject<ClientCommunicationProtocol> *)ptr;
        [service getSyncStatusWithCompletionHandler:^(NSDictionary<NSString *, id> *status, NSError *error) {
            // Copy the JSON bytes before leaving the XPC callback queue.
            NSError *serializationError = nil;
            NSData *data = status && [NSJSONSerialization isValidJSONObject:status]
                ? [NSJSONSerialization dataWithJSONObject:status options:0 error:&serializationError]
                : nil;
            const QByteArray bytes = data ? QByteArray(static_cast<const char *>(data.bytes), data.length) : QByteArray();
            const QString message =
                error ? QString::fromNSString(error.localizedDescription) : (!data ? tr("The on-demand provider returned an invalid sync status.") : QString());
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!guard || guard->_statusRequests.value(identifier) != request) {
                    return;
                }
                guard->_statusRequests.remove(identifier);
                Q_EMIT guard->syncStatusReceived(identifier, QJsonDocument::fromJson(bytes).object(), message);
            });
        }];
        QTimer::singleShot(std::chrono::seconds(5), this, [this, identifier, request] {
            if (_statusRequests.value(identifier) == request) {
                _statusRequests.remove(identifier);
                Q_EMIT syncStatusReceived(identifier, {}, tr("The on-demand sync status is unavailable."));
            }
        });
    }
}

void FileProviderXPC::refreshCredentials()
{
    connectToFileProviderDomains();
    qCDebug(lcFileProviderXPC) << "Periodic credential refresh for" << _clientCommServices.count() << "domains";
    authenticateFileProviderDomains();
}

} // namespace Mac
} // namespace OCC
