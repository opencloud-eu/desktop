/*
 * Copyright (C) by OpenCloud GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "libsync/networkjobs.h"
#include "libsync/networkjobs/simplenetworkjob.h"
#include "libsync/owncloudpropagator.h"
#include "libsync/common/syncjournalfilerecord.h"

#include <QPointer>
#include <QList>
#include <QString>
#include <QTemporaryFile>

#include <ctime>

namespace OCC {

class ConflictAutoMerge : public QObject
{
    Q_OBJECT
public:
    explicit ConflictAutoMerge(OwncloudPropagator *propagator, const SyncFileItemPtr &item, const QString &localFile, const QString &remoteFile,
        QObject *parent = nullptr);

    bool canStart() const;
    void start();

Q_SIGNALS:
    void finished(bool merged, const QString &mergedFileName);

private:
    struct VersionInfo
    {
        QString name;
        QString etag;
        time_t mtime = 0;
    };

    bool isTextMergeCandidate(const QString &fileName) const;
    void versionsListed();
    void baseDownloaded();
    void emitNotMerged();
    void runMerge();
    QString matchingVersionName() const;

    OwncloudPropagator *_propagator = nullptr;
    SyncFileItemPtr _item;
    QString _localFile;
    QString _remoteFile;
    SyncJournalFileRecord _baseRecord;
    QList<VersionInfo> _versions;
    QPointer<SimpleNetworkJob> _versionsJob;
    QPointer<SimpleNetworkJob> _baseJob;
    QTemporaryFile _baseFile;
};

}
