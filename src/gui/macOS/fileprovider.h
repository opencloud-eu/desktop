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

#include "fileprovidersyncstatus.h"
#include "gui/opencloudguilib.h"
#include "libsync/common/result.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUuid>
#include <memory>

class TestFileProviderLifecycle;

namespace OCC {

class Application;

namespace Mac {

    class FileProviderDomainManager;
    class FileProviderXPC;

    /**
     * @brief Main coordinator for macOS FileProvider integration.
     *
     * This singleton class manages the FileProvider domain manager and XPC
     * communication with the FileProvider extension. It should be started
     * after the AccountManager has loaded accounts.
     */
    class OPENCLOUD_GUI_EXPORT FileProvider : public QObject
    {
        Q_OBJECT

    public:
        static FileProvider *instance();
        ~FileProvider() override;

        /**
         * @brief Check if FileProvider is available on this system.
         */
        static bool fileProviderAvailable();

        /// Disconnect existing domains before traditional folder sync can start.
        static Result<void, QString> prepareForFolderSync();

        bool ready() const;
        QString error() const;
        QString syncStatusText(const QUuid &account = {}) const;

    Q_SIGNALS:
        void statusChanged();

    public:
        /**
         * @brief Get the domain manager.
         */
        FileProviderDomainManager *domainManager() const;

        /**
         * @brief Get the XPC client for extension communication.
         */
        FileProviderXPC *xpc() const;

    private Q_SLOTS:
        void configureXPC();

    private:
        Q_DISABLE_COPY_MOVE(FileProvider)
        QHash<QString, QString> _errors;
        QSet<QString> _registeredDomains;
        QSet<QString> _authenticatedDomains;
        QHash<QString, FileProviderSyncStatus> _syncStatus;
        static FileProvider *_instance;
        explicit FileProvider(QObject *parent = nullptr, const QString &appGroupIdentifier = {});

        std::unique_ptr<FileProviderDomainManager> _domainManager;
        std::unique_ptr<FileProviderXPC> _xpc;

        friend class OCC::Application;
        friend class ::TestFileProviderLifecycle;
    };

} // namespace Mac
} // namespace OCC
