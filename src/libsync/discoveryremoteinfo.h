// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/common/remotepermissions.h"

#include <QSharedData>

namespace OCC {

class RemoteInfoData;

/**
 * Represent all the meta-data about a file in the server
 */
class OPENCLOUD_SYNC_EXPORT RemoteInfo
{
public:
    RemoteInfo();
    RemoteInfo(const QString &fileName, const QMap<QString, QString> &map);
    ~RemoteInfo();
    RemoteInfo(const RemoteInfo &other);
    RemoteInfo &operator=(const RemoteInfo &other);

    void swap(RemoteInfo &other) noexcept { d.swap(other.d); }
    QT_MOVE_ASSIGNMENT_OPERATOR_IMPL_VIA_MOVE_AND_SWAP(RemoteInfo);

    /** FileName of the entry (this does not contain any directory or path, just the plain name */
    QString name() const;
    bool isDirectory() const;

    QString etag() const;
    QByteArray fileId() const;
    QByteArray checksumHeader() const;

    RemotePermissions remotePerm() const;
    time_t modtime() const;

    int64_t size() const;

    QString error() const;

    bool isValid() const;

private:
    QExplicitlySharedDataPointer<RemoteInfoData> d;
};

}

Q_DECLARE_SHARED(OCC::RemoteInfo);
