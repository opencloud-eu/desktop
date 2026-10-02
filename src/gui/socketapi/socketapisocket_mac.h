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
 * Unix domain socket wrapper classes for macOS FinderSyncExt communication.
 * Based on Nextcloud Desktop Client approach.
 */

#ifndef SOCKETAPISOCKET_OSX_H
#define SOCKETAPISOCKET_OSX_H

#include <QAbstractSocket>
#include <QIODevice>

class SocketApiServerPrivate;
class SocketApiSocketPrivate;

class SocketApiSocket : public QIODevice
{
    Q_OBJECT
public:
    SocketApiSocket(QObject *parent, SocketApiSocketPrivate *p);
    ~SocketApiSocket() override;

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;
    bool canReadLine() const override;

Q_SIGNALS:
    void disconnected();

public:
    // Accessor for internal use
    SocketApiSocketPrivate *socketPrivate() { return d_ptr.data(); }

private:
    // Use Qt's p-impl system to hide implementation details
    Q_DECLARE_PRIVATE(SocketApiSocket)
    QScopedPointer<SocketApiSocketPrivate> d_ptr;
    friend class SocketApiServerPrivate;
};

class SocketApiServer : public QObject
{
    Q_OBJECT
public:
    SocketApiServer();
    ~SocketApiServer() override;

    void close();
    bool listen(const QString &name);
    SocketApiSocket *nextPendingConnection();

    static bool removeServer(const QString &path);

Q_SIGNALS:
    void newConnection();

private:
    Q_DECLARE_PRIVATE(SocketApiServer)
    QScopedPointer<SocketApiServerPrivate> d_ptr;
};

#endif // SOCKETAPISOCKET_OSX_H
