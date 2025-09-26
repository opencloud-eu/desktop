// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "common/syncjournalfilerecord.h"
#include "networkjobs/getfilejob.h"


#include <QIODevice>
#include <QObject>

namespace OCC {
class Vfs;

class OPENCLOUD_SYNC_EXPORT HydrationJob : public QObject
{
    Q_OBJECT
public:
    HydrationJob(Vfs *vfs, const QByteArray &fileId, std::unique_ptr<QIODevice> &&device, QObject *parent);

    void start();
    void abort();

    Vfs *vfs() const;

    SyncJournalFileRecord record() const;

    QByteArray fileId() const { return _fileId; }

Q_SIGNALS:
    void finished();
    void error(const QString &error);

private:
    Vfs *_vfs;
    QByteArray _fileId;
    std::unique_ptr<QIODevice> _device;
    SyncJournalFileRecord _record;
    GETFileJob *_job = nullptr;
};
}
