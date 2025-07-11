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

#include "propagateremotemove.h"
#include "propagatorjobs.h"
#include "owncloudpropagator_p.h"
#include "account.h"
#include "common/syncjournalfilerecord.h"
#include "filesystem.h"
#include "common/asserts.h"
#include <QFile>
#include <QStringList>
#include <QDir>

namespace OCC {

Q_LOGGING_CATEGORY(lcMoveJob, "sync.networkjob.move", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPropagateRemoteMove, "sync.propagator.remotemove", QtInfoMsg)

MoveJob::MoveJob(AccountPtr account, const QUrl &url, const QString &path, const QString &destination,
    const HeaderMap &extraHeaders, QObject *parent)
    : AbstractNetworkJob(account, url, path, parent)
    , _destination(destination)
    , _extraHeaders(extraHeaders)
{
}

void MoveJob::start()
{
    QNetworkRequest req;
    req.setRawHeader("Destination", QUrl::toPercentEncoding(_destination, "/"));
    for (auto it = _extraHeaders.constBegin(); it != _extraHeaders.constEnd(); ++it) {
        req.setRawHeader(it.key(), it.value());
    }
    sendRequest("MOVE", req);
    AbstractNetworkJob::start();
}


void MoveJob::finished()
{
    qCInfo(lcMoveJob) << "MOVE of" << reply()->request().url() << "FINISHED WITH STATUS"
                      << replyStatusString();
}

void PropagateRemoteMove::start()
{
    if (propagator()->_abortRequested)
        return;

    QString origin = propagator()->adjustRenamedPath(_item->localName());
    qCDebug(lcPropagateRemoteMove) << origin << _item->_renameTarget;
    if (origin == _item->_renameTarget) {
        // The parent has been renamed already so there is nothing more to do.
        finalize();
        return;
    }

    QString remoteSource = propagator()->fullRemotePath(origin);
    QString remoteDestination = QDir::cleanPath(propagator()->webDavUrl().path() + propagator()->fullRemotePath(_item->_renameTarget));

    auto itype = _item->_type;
    OC_ASSERT(itype != ItemTypeVirtualFileDownload && itype != ItemTypeVirtualFileDehydration);
    _job = new MoveJob(propagator()->account(), propagator()->webDavUrl(), remoteSource, remoteDestination, {}, this);
    connect(_job.data(), &MoveJob::finishedSignal, this, &PropagateRemoteMove::slotMoveJobFinished);
    propagator()->_activeJobList.append(this);
    _job->start();
}

void PropagateRemoteMove::abort(PropagatorJob::AbortType abortType)
{
    if (_job) {
        _job->abort();
    }
    if (abortType == AbortType::Asynchronous) {
        Q_EMIT abortFinished();
    }
}

void PropagateRemoteMove::slotMoveJobFinished()
{
    propagator()->_activeJobList.removeOne(this);

    OC_ASSERT(_job);

    QNetworkReply::NetworkError err = _job->reply()->error();
    _item->_httpErrorCode = _job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    _item->_responseTimeStamp = _job->responseTimestamp();
    _item->_requestId = _job->requestId();

    if (err != QNetworkReply::NoError) {
        SyncFileItem::Status status = classifyError(err, _item->_httpErrorCode,
            &propagator()->_anotherSyncNeeded);
        done(status, _job->errorString());
        return;
    }

    if (_item->_httpErrorCode != 201) {
        // Normally we expect "201 Created"
        // If it is not the case, it might be because of a proxy or gateway intercepting the request, so we must
        // throw an error.
        done(SyncFileItem::NormalError,
            tr("Wrong HTTP code returned by server. Expected 201, but received \"%1 %2\".")
                .arg(_item->_httpErrorCode)
                .arg(_job->reply()->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString()));
        return;
    }

    finalize();
}

void PropagateRemoteMove::finalize()
{
    // Retrieve old db data.
    // if reading from db failed still continue hoping that deleteFileRecord
    // reopens the db successfully.
    // The db is only queried to transfer the content checksum from the old
    // to the new record. It is not a problem to skip it here.
    SyncJournalFileRecord oldRecord;
    propagator()->_journal->getFileRecord(_item->_originalFile, &oldRecord);

    // Delete old db data.
    propagator()->_journal->deleteFileRecord(_item->_originalFile);

    SyncFileItem newItem(*_item);
    newItem._type = _item->_type;
    if (oldRecord.isValid()) {
        newItem._checksumHeader = oldRecord._checksumHeader;
        if (newItem._size != oldRecord._fileSize) {
            qCWarning(lcPropagateRemoteMove) << "File sizes differ on server vs sync journal: " << newItem._size << oldRecord._fileSize;

            // the server might have claimed a different size, we take the old one from the DB
            newItem._size = oldRecord._fileSize;
        }
    }
    const auto result = propagator()->updateMetadata(newItem);
    if (!result) {
        done(SyncFileItem::FatalError, tr("Error updating metadata: %1").arg(result.error()));
        return;
    } else if (result.get() == Vfs::ConvertToPlaceholderResult::Locked) {
        done(SyncFileItem::SoftError, tr("The file %1 is currently in use").arg(newItem.localName()));
        return;
    }

    if (_item->isDirectory()) {
        propagator()->_renamedDirectories.insert(_item->localName(), _item->_renameTarget);
        if (!adjustSelectiveSync(propagator()->_journal, _item->localName(), _item->_renameTarget)) {
            done(SyncFileItem::FatalError, tr("Error writing metadata to the database"));
            return;
        }
    }

    propagator()->_journal->commit(QStringLiteral("Remote Rename"));
    done(SyncFileItem::Success);
}

bool PropagateRemoteMove::adjustSelectiveSync(SyncJournalDb *journal, const QString &from_, const QString &to_)
{
    bool ok;
    // We only care about preserving the blacklist.   The white list should anyway be empty.
    // And the undecided list will be repopulated on the next sync, if there is anything too big.
    auto list = journal->getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok);
    if (!ok)
        return false;

    bool changed = false;
    OC_ASSERT(!from_.endsWith(QLatin1String("/")));
    OC_ASSERT(!to_.endsWith(QLatin1String("/")));
    QString from = from_ + QLatin1String("/");
    QString to = to_ + QLatin1String("/");

    QSet<QString> out;
    out.reserve(list.size());
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->startsWith(from)) {
            out.insert(QString(*it).replace(0, from.size(), to));
            changed = true;
        } else {
            out.insert(*it);
        }
    }

    if (changed) {
        journal->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, out);
    }
    return true;
}
}
