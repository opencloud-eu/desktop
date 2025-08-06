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

#include "account.h"
#include "common/checksums.h"
#include "common/syncjournaldb.h"
#include "filesystem.h"
#include "networkjobs.h"
#include "owncloudpropagator_p.h"
#include "propagateupload.h"
#include "propagatorjobs.h"
#include "syncengine.h"

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

#include <cmath>
#include <cstring>
#include <memory>

namespace OCC {

void PropagateUploadFileV1::doStartUpload()
{
    const QString fileName = propagator()->fullLocalPath(_item->localName());
    // If the file is currently locked, we want to retry the sync
    // when it becomes available again.
    if (FileSystem::isFileLocked(fileName, FileSystem::LockMode::SharedRead)) {
        Q_EMIT propagator()->seenLockedFile(fileName, FileSystem::LockMode::SharedRead);
        abortWithError(SyncFileItem::SoftError, tr("%1 the file is currently in use").arg(QDir::toNativeSeparators(fileName)));
        return;
    }

    if (!_item->_checksumHeader.isEmpty()) {
        // Write the checksum in the database, so if the PUT is sent
        // to the server, but the connection drops before we get the etag, we can check the checksum
        // in reconcile (issue #5106)
        auto pi = _item->toUploadInfo();
        pi._chunk = 0;
        pi._transferid = 0; // We set a null transfer id because it is not chunked.
        pi._errorCount = 0;
        propagator()->_journal->setUploadInfo(_item->localName(), pi);
        propagator()->_journal->commit(QStringLiteral("Upload info"));
    }
    propagator()->reportProgress(*_item, 0);

    qint64 fileSize = _item->_size;
    auto headers = PropagateUploadFileCommon::headers();
    headers[QByteArrayLiteral("OC-Total-Length")] = QByteArray::number(fileSize);

    QString path = _item->localName();

    if (!_transmissionChecksumHeader.isEmpty()) {
        qCInfo(lcPropagateUploadV1) << propagator()->fullRemotePath(path) << _transmissionChecksumHeader;
        headers[checkSumHeaderC] = _transmissionChecksumHeader;
    }

    auto device = std::make_unique<UploadDevice>(fileName, 0, fileSize, propagator()->_bandwidthManager);
    if (!device->open(QIODevice::ReadOnly)) {
        qCWarning(lcPropagateUploadV1) << "Could not prepare upload device: " << device->errorString();
        // Soft error because this is likely caused by the user modifying his files while syncing
        abortWithError(SyncFileItem::SoftError, device->errorString());
        return;
    }

    // job takes ownership of device via a QScopedPointer. Job deletes itself when finishing
    auto devicePtr = device.get(); // for connections later
    PUTFileJob *job = new PUTFileJob(propagator()->account(), propagator()->webDavUrl(), propagator()->fullRemotePath(path), std::move(device), headers, this);
    addChildJob(job);
    connect(job, &PUTFileJob::finishedSignal, this, &PropagateUploadFileV1::slotPutFinished);
    connect(job, &PUTFileJob::uploadProgress, this, &PropagateUploadFileV1::slotUploadProgress);
    connect(job, &PUTFileJob::uploadProgress, devicePtr, &UploadDevice::slotJobUploadProgress);
    adjustLastJobTimeout(job, fileSize);
    job->start();
    propagator()->_activeJobList.append(this);
}

void PropagateUploadFileV1::slotPutFinished()
{
    PUTFileJob *job = qobject_cast<PUTFileJob *>(sender());
    Q_ASSERT(job);

    propagator()->_activeJobList.removeOne(this);

    if (_finished) {
        // We have sent the finished signal already. We don't need to handle any remaining jobs
        return;
    }

    _item->_httpErrorCode = job->httpStatusCode();
    _item->_responseTimeStamp = job->responseTimestamp();
    _item->_requestId = job->requestId();
    QNetworkReply::NetworkError err = job->reply()->error();
    if (err != QNetworkReply::NoError) {
        commonErrorHandling(job);
        return;
    }

    if (_item->_httpErrorCode == 202) {
        done(SyncFileItem::NormalError, tr("The server did ask for a removed legacy feature (polling)"));
        return;
    }

    // Check the file again post upload.
    // Two cases must be considered separately: If the upload is finished,
    // the file is on the server and has a changed ETag. In that case,
    // the etag has to be properly updated in the client journal, and because
    // of that we can bail out here with an error. But we can reschedule a
    // sync ASAP.
    // But if the upload is ongoing, because not all chunks were uploaded
    // yet, the upload can be stopped and an error can be displayed, because
    // the server hasn't registered the new file yet.
    QString etag = getEtagFromReply(job->reply());
    _finished = etag.length() > 0;

    // Check if the file still exists
    const QString fullFilePath(propagator()->fullLocalPath(_item->localName()));
    if (!FileSystem::fileExists(fullFilePath)) {
        if (!_finished) {
            abortWithError(SyncFileItem::SoftError, tr("The local file was removed during sync."));
            return;
        } else {
            propagator()->_anotherSyncNeeded = true;
        }
    }

    // Check whether the file changed since discovery.
    if (FileSystem::fileChanged(FileSystem::toFilesystemPath(fullFilePath), FileSystem::FileChangedInfo::fromSyncFileItem(_item.data()))) {
        propagator()->_anotherSyncNeeded = true;
        if (!_finished) {
            abortWithError(SyncFileItem::Message, fileChangedMessage());
            return;
        }
    }

    if (!_finished) {
        done(SyncFileItem::NormalError, tr("The server did not acknowledge the last chunk. (No e-tag was present)"));
        return;
    }
    // the following code only happens after all chunks were uploaded.

    // the file id should only be empty for new files up- or downloaded
    QByteArray fid = job->reply()->rawHeader("OC-FileID");
    if (!fid.isEmpty()) {
        if (!_item->_fileId.isEmpty() && _item->_fileId != fid) {
            qCWarning(lcPropagateUploadV1) << "File ID changed!" << _item->_fileId << fid;
        }
        _item->_fileId = fid;
    }

    _item->_etag = etag;
    finalize();
}


void PropagateUploadFileV1::slotUploadProgress(qint64 sent, qint64 total)
{
    // Completion is signaled with sent=0, total=0; avoid accidentally
    // resetting progress due to the sent being zero by ignoring it.
    // finishedSignal() is bound to be emitted soon anyway.
    // See https://bugreports.qt.io/browse/QTBUG-44782.
    if (sent == 0 && total == 0) {
        return;
    }
    propagator()->reportProgress(*_item, sent);
}

void PropagateUploadFileV1::abort(PropagatorJob::AbortType abortType)
{
    abortNetworkJobs(abortType, [abortType](AbstractNetworkJob *job) {
        if (PUTFileJob *putJob = qobject_cast<PUTFileJob *>(job)) {
            if (abortType == AbortType::Asynchronous && putJob->device()->atEnd()) {
                return false;
            }
        }
        return true;
    });
}

}
