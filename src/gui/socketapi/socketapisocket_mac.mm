/*
 * Copyright (C) by Jocelyn Turcotte <jturcotte@woboq.com>
 * Copyright (C) 2025 OpenCloud GmbH
 * Copyright (C) 2022 Nextcloud GmbH and Nextcloud contributors
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
 *
 * Unix domain socket implementation for macOS FinderSyncExt communication.
 * Based on Nextcloud Desktop Client approach:
 * https://github.com/nextcloud/desktop
 *
 * The previous XPC-based approach failed because NSXPCListenerEndpoint cannot be
 * serialized to a file (Apple restricts it to XPC-only transport).
 */

#include "socketapisocket_mac.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QFile>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSocketApiMac, "gui.socketapi.mac", QtInfoMsg)

// ============================================================================
// SocketApiSocketPrivate - wraps a QLocalSocket for each connected client
// ============================================================================

class SocketApiSocketPrivate
{
public:
    SocketApiSocket *q_ptr = nullptr;
    QLocalSocket *localSocket = nullptr;
    QByteArray inBuffer;
    bool isRemoteDisconnected = false;

    explicit SocketApiSocketPrivate(QLocalSocket *socket)
        : localSocket(socket)
    {
    }

    // The public wrapper owns the socket as a QObject child.

    void disconnectRemote()
    {
        if (isRemoteDisconnected)
            return;
        isRemoteDisconnected = true;
        if (localSocket) {
            localSocket->disconnectFromServer();
        }
    }
};

// ============================================================================
// SocketApiServerPrivate - wraps QLocalServer
// ============================================================================

class SocketApiServerPrivate
{
public:
    SocketApiServer *q_ptr = nullptr;
    QLocalServer *localServer = nullptr;
    QList<SocketApiSocket *> pendingConnections;
    QString socketPath;

    SocketApiServerPrivate()
        : localServer(new QLocalServer())
    {
    }

    ~SocketApiServerPrivate()
    {
        if (localServer) {
            localServer->close();
            delete localServer;
            localServer = nullptr;
        }
    }
};

// ============================================================================
// SocketApiSocket implementation
// ============================================================================

SocketApiSocket::SocketApiSocket(QObject *parent, SocketApiSocketPrivate *p)
    : QIODevice(parent)
    , d_ptr(p)
{
    Q_D(SocketApiSocket);
    d->q_ptr = this;
    open(ReadWrite);

    // Connect signals from the underlying QLocalSocket
    if (d->localSocket) {
        d->localSocket->setParent(this);
        connect(d->localSocket, &QLocalSocket::readyRead, this, [this]() {
            Q_D(SocketApiSocket);
            // Read all available data into our buffer
            d->inBuffer.append(d->localSocket->readAll());
            Q_EMIT readyRead();
        });

        connect(d->localSocket, &QLocalSocket::disconnected, this, [this]() {
            Q_D(SocketApiSocket);
            d->isRemoteDisconnected = true;
            Q_EMIT disconnected();
        });

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(d->localSocket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
            Q_D(SocketApiSocket);
            qCWarning(lcSocketApiMac) << "Socket error:" << error << d->localSocket->errorString();
            d->isRemoteDisconnected = true;
            Q_EMIT disconnected();
        });
#else
        connect(d->localSocket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
                this, [this](QLocalSocket::LocalSocketError error) {
            Q_D(SocketApiSocket);
            qCWarning(lcSocketApiMac) << "Socket error:" << error << d->localSocket->errorString();
            d->isRemoteDisconnected = true;
            Q_EMIT disconnected();
        });
#endif
    }
}

SocketApiSocket::~SocketApiSocket()
{
    Q_D(SocketApiSocket);
    // Stop callbacks before d_ptr is destroyed. QObject then destroys the socket.
    disconnect(d->localSocket, nullptr, this, nullptr);
    d->localSocket->disconnectFromServer();
}

qint64 SocketApiSocket::readData(char *data, qint64 maxlen)
{
    Q_D(SocketApiSocket);
    qint64 len = std::min(maxlen, static_cast<qint64>(d->inBuffer.size()));
    if (len < 0 || len > std::numeric_limits<int>::max()) {
        return -1;
    }

    memcpy(data, d->inBuffer.constData(), static_cast<size_t>(len));
    d->inBuffer.remove(0, static_cast<int>(len));
    return len;
}

qint64 SocketApiSocket::writeData(const char *data, qint64 len)
{
    Q_D(SocketApiSocket);
    if (d->isRemoteDisconnected || !d->localSocket) {
        return -1;
    }

    return d->localSocket->write(data, len);
}

qint64 SocketApiSocket::bytesAvailable() const
{
    Q_D(const SocketApiSocket);
    return d->inBuffer.size() + QIODevice::bytesAvailable();
}

bool SocketApiSocket::canReadLine() const
{
    Q_D(const SocketApiSocket);
    return d->inBuffer.indexOf('\n', static_cast<int>(pos())) != -1 || QIODevice::canReadLine();
}

// ============================================================================
// SocketApiServer implementation
// ============================================================================

SocketApiServer::SocketApiServer()
    : d_ptr(new SocketApiServerPrivate)
{
    Q_D(SocketApiServer);
    d->q_ptr = this;

    // Connect new connection signal
    connect(d->localServer, &QLocalServer::newConnection, this, [this]() {
        Q_D(SocketApiServer);
        while (d->localServer->hasPendingConnections()) {
            QLocalSocket *clientSocket = d->localServer->nextPendingConnection();
            if (clientSocket) {
                qCInfo(lcSocketApiMac) << "New client connection from FinderSyncExt";

                SocketApiSocketPrivate *socketPrivate = new SocketApiSocketPrivate(clientSocket);
                SocketApiSocket *socket = new SocketApiSocket(this, socketPrivate);
                d->pendingConnections.append(socket);
                Q_EMIT newConnection();
            }
        }
    });
}

SocketApiServer::~SocketApiServer()
{
}

void SocketApiServer::close()
{
    Q_D(SocketApiServer);
    if (d->localServer) {
        d->localServer->close();
    }

    // Remove the socket file
    if (!d->socketPath.isEmpty()) {
        QFile::remove(d->socketPath);
    }
}

bool SocketApiServer::listen(const QString &name)
{
    Q_D(SocketApiServer);
    d->socketPath = name;  // On macOS, 'name' is actually the full socket path from socketApiSocketPath()

    qCInfo(lcSocketApiMac) << "Starting Unix socket server at:" << d->socketPath;

    // Remove any existing socket file (stale from previous run)
    if (QFile::exists(d->socketPath)) {
        qCInfo(lcSocketApiMac) << "Removing stale socket file";
        QFile::remove(d->socketPath);
    }

    // Finder extensions run as the same user as the app.
    d->localServer->setSocketOptions(QLocalServer::UserAccessOption);
    // Start listening
    if (!d->localServer->listen(d->socketPath)) {
        qCWarning(lcSocketApiMac) << "Failed to start socket server:" << d->localServer->errorString();
        return false;
    }

    qCInfo(lcSocketApiMac) << "Socket server listening at:" << d->localServer->fullServerName();
    return true;
}

SocketApiSocket *SocketApiServer::nextPendingConnection()
{
    Q_D(SocketApiServer);
    if (d->pendingConnections.isEmpty()) {
        return nullptr;
    }
    return d->pendingConnections.takeFirst();
}

bool SocketApiServer::removeServer(const QString &path)
{
    if (QFile::exists(path)) {
        return QFile::remove(path);
    }
    return true;
}
