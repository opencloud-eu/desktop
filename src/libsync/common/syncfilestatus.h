/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#ifndef SYNCFILESTATUS_H
#define SYNCFILESTATUS_H

#include <QMetaType>
#include <QObject>
#include <QString>

#include "libsync/opencloudsynclib.h"

namespace OCC {

/**
 * @brief The SyncFileStatus class
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT SyncFileStatus
{
    Q_GADGET
public:
    enum SyncFileStatusTag {
        StatusNone,
        StatusSync,
        StatusWarning,
        StatusUpToDate,
        StatusError,
        StatusExcluded,
    };
    Q_ENUM(SyncFileStatusTag);

    SyncFileStatus();
    SyncFileStatus(SyncFileStatusTag);

    void set(SyncFileStatusTag tag);
    SyncFileStatusTag tag() const;

    void setShared(bool isShared);
    bool shared() const;

    QString toSocketAPIString() const;

private:
    SyncFileStatusTag _tag;
    bool _shared;
};

inline bool operator==(const SyncFileStatus &a, const SyncFileStatus &b)
{
    return a.tag() == b.tag() && a.shared() == b.shared();
}

inline bool operator!=(const SyncFileStatus &a, const SyncFileStatus &b)
{
    return !(a == b);
}
}
OPENCLOUD_SYNC_EXPORT QDebug &operator<<(QDebug &debug, const OCC::SyncFileStatus &item);

Q_DECLARE_METATYPE(OCC::SyncFileStatus)

#endif // SYNCFILESTATUS_H
