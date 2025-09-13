// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "hydrationjob.h"

#include "libsync/common/syncjournaldb.h"
#include "libsync/networkjobs/getfilejob.h"
#include "vfs.h"

using namespace OCC;

HydrationJob::HydrationJob(Vfs *vfs, const QByteArray &fileId, std::unique_ptr<QIODevice> &&device, QObject *parent)
    : QObject(parent)
    , _vfs(vfs)
    , _device(std::move(device))
{
    vfs->params().journal->getFileRecordsByFileId(fileId, [this](const SyncJournalFileRecord &record) {
        Q_ASSERT(_record.isValid());
        _record = record;
    });
    Q_ASSERT(_record.isValid());
}

void HydrationJob::start()
{
    _job = new GETFileJob(_vfs->params().account, _vfs->params().baseUrl(), _record.path(), _device.get(), {}, {}, 0, this);
    _job->setExpectedContentLength(_record.size());
    connect(_job, &GETFileJob::finishedSignal, this, [this] {
        QString errorMsg;
        if (_job->reply()->error() != 0 || (_job->httpStatusCode() != 200 && _job->httpStatusCode() != 204)) {
            errorMsg = _job->reply()->errorString();
        }

        if (_job->contentLength() != -1) {
            const auto size = _job->resumeStart() + _job->contentLength();
            if (size != _record.size()) {
                errorMsg = tr("Unexpected file size transferred. Expected %1 received %2").arg(QString::number(_record.size()), QString::number(size));
                // assume that the local and the remote metadate are out of sync
                Q_EMIT _vfs->needSync();
            }
        }
        if (_job->aborted()) {
            errorMsg = tr("Aborted.");
        }

        if (!errorMsg.isEmpty()) {
            Q_EMIT error(errorMsg);
            return;
        }

        Q_EMIT finished();
    });
    _job->start();
}

void HydrationJob::abort()
{
    _job->abort();
}
