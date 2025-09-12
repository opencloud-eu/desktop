/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
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

#include "common/checksumalgorithms.h"
#include "networkjobs.h"
#include "owncloudpropagator.h"

#include <QBuffer>
#include <QFile>

namespace OCC {

/**
 * @brief The PropagateDownloadFile class
 * @ingroup libsync
 *
 * This is the flow:

\code{.unparsed}
  start()
    +
    | deleteExistingFolder() if enabled
    |
    +--> mtime and size identical?
    |    then compute the local checksum
    |                               done?+> conflictChecksumComputed()
    |                                                                +
    |                         checksum differs?                      |
    +-> startDownload() <--------------------------------------------+
    +            +                           |                       |
    no           no                          |                       |
    +            +                           |                       |
    |            v                           |                       |
    +-> startFullDownload()                  |                       |
              +                              |                       |
              +-> run a GETFileJob           |                       | checksum identical?
                                             |                       |
          done?+> slotGetFinished() <--------+                       |
                    +                                                |
                    +-> validate checksum header                     |
                                                                     |
          done?+> transmissionChecksumValidated()                    |
                    +                                                |
                    +-> compute the content checksum                 |
                                                                     |
          done?+> contentChecksumComputed()                          |
                    +                                                |
                    +-> downloadFinished()                           |
                           +                                         |
        +------------------+                                         |
        |                                                            |
        +-> updateMetadata() <---------------------------------------+

\endcode
 */
class PropagateDownloadFile : public PropagateItemJob
{
    Q_OBJECT
    QString _expectedEtagForResume;

public:
    PropagateDownloadFile(OwncloudPropagator *propagator, const SyncFileItemPtr &item)
        : PropagateItemJob(propagator, item)
        , _resumeStart(0)
        , _downloadProgress(0)
        , _deleteExisting(false)
    {
    }
    void start() override;
    qint64 committedDiskSpace() const override;

    // We think it might finish quickly because it is a small file.
    bool isLikelyFinishedQuickly() override { return _item->_size < propagator()->smallFileSize(); }

    /**
     * Whether an existing folder with the same name may be deleted before
     * the download.
     *
     * If it's a non-empty folder, it'll be renamed to a conflict-style name
     * to preserve any non-synced content that may be inside.
     *
     * Default: false.
     */
    void setDeleteExistingFolder(bool enabled);

private Q_SLOTS:
    /// Called when ComputeChecksum on the local file finishes,
    /// maybe the local and remote checksums are identical?
    void conflictChecksumComputed(CheckSums::Algorithm checksumType, const QByteArray &checksum);
    /// Called to start downloading the remote file
    void startDownload();
    void startFullDownload();
    /// Called when the GETJob finishes
    void slotGetFinished();
    /// Called when the download's checksum header was validated
    void transmissionChecksumValidated(CheckSums::Algorithm checksumType, const QByteArray &checksum);
    /// Called when the download's checksum computation is done
    void contentChecksumComputed(CheckSums::Algorithm checksumType, const QByteArray &checksum);
    void downloadFinished();
    /// Called when it's time to update the db metadata
    void updateMetadata(bool isConflict);

    void abort(PropagatorJob::AbortType abortType) override;
    void slotDownloadProgress(qint64, qint64);
    void slotChecksumFail(const QString &errMsg);

private:
    void deleteExistingFolder();

    qint64 _resumeStart;
    qint64 _downloadProgress;
    QPointer<GETFileJob> _job;
    QFile _tmpFile;
    bool _deleteExisting;
    ConflictRecord _conflictRecord;

    QElapsedTimer _stopwatch;
};
}
