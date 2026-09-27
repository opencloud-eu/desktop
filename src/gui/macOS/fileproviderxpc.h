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

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <functional>
#include <memory>

#include "gui/accountstate.h"
#include "libsync/common/result.h"

namespace OCC {
namespace Mac {

    /**
     * @brief Manages XPC communication with FileProvider extension processes.
     *
     * This class establishes connections to FileProvider extensions via their
     * exposed NSFileProviderServiceSource services. It allows the main app to
     * send account credentials and configuration to the extensions.
     */
    class OPENCLOUD_GUI_EXPORT FileProviderXPC : public QObject
    {
        Q_OBJECT

    public:
        explicit FileProviderXPC(QObject *parent = nullptr, const QString &appGroupIdentifier = {});
        ~FileProviderXPC() override;
        static Result<void, QString> recordAccountRemoval(const QString &domainIdentifier, const QString &appGroupIdentifier = {});
        QString appGroupIdentifier() const { return _appGroupIdentifier; }

        /**
         * @brief Check if a FileProvider domain is reachable via XPC.
         */
        bool fileProviderDomainReachable(const QString &domainIdentifier);

    public Q_SLOTS:
        /**
         * @brief Connect to all registered FileProvider domain services.
         */
        void connectToFileProviderDomains();

        /**
         * @brief Send authentication to all connected FileProvider domains.
         */
        void authenticateFileProviderDomains();

        /**
         * @brief Send authentication to a specific FileProvider domain.
         */
        void authenticateFileProviderDomain(QString domainIdentifier);

        /**
         * @brief Remove authentication from a specific FileProvider domain.
         */
        void unauthenticateFileProviderDomain(const QString &domainIdentifier);

    public:
        void clearAccountConfiguration(const QString &domainIdentifier, std::function<void(bool)> completion);
        void closeConnection(const QString &domainIdentifier);
        void refreshSyncStatus();

    Q_SIGNALS:
        void domainConnected(const QString &domainIdentifier);
        void domainReconnecting(const QString &domainIdentifier);
        void domainStatusChanged(const QString &domainIdentifier, const QString &error);
        void syncStatusReceived(const QString &domainIdentifier, const QJsonObject &status, const QString &error);

    private Q_SLOTS:
        void slotAccountStateChanged(AccountState::State state);
        void reconnectAfterInvalidation(const QString &domainIdentifier);
        void refreshCredentials();

    private:
        void clearConnections();
        void authenticateAccountDomains(const QUuid &account);
        void finishClearingAccount(const QString &domainIdentifier, bool success);
        QHash<QString, QList<std::function<void(bool)>>> _cleanupCallbacks;

        QHash<QString, int> _authenticationRetries;
        QHash<QString, std::shared_ptr<bool>> _configurationRequests;
        QHash<QString, std::shared_ptr<bool>> _statusRequests;
        QSet<QString> _statusDomains;
        QString _appGroupIdentifier;
        QHash<QString, void *> _connections;
        // The native service and manager own the extension request's lifetime.
        QHash<QString, void *> _serviceLeases;
        QHash<QString, void *> _managerLeases;
        QSet<QString> _pendingDomains;
        QHash<QString, std::shared_ptr<bool>> _connectionRequests;
        bool _discoveryPending = false;
        // Keys are FileProvider domain identifiers, values are NSObject<ClientCommunicationProtocol>*
        QHash<QString, void *> _clientCommServices;
        bool _reconnectPending = false;
        QTimer *_credentialRefreshTimer = nullptr;
    };

} // namespace Mac
} // namespace OCC
