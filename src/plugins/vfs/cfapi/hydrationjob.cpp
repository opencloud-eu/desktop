/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "plugins/vfs/cfapi/hydrationjob.h"

#include "plugins/vfs/cfapi/cfapiwrapper.h"
#include "plugins/vfs/cfapi/vfs_cfapi.h"

#include "libsync/common/syncjournaldb.h"
#include "libsync/filesystem.h"
#include "libsync/networkjobs/getfilejob.h"
#include "libsync/vfs/hydrationjob.h"

#include <QLocalServer>
#include <QLocalSocket>

using namespace Qt::Literals::StringLiterals;

Q_LOGGING_CATEGORY(lcHydration, "sync.vfs.hydrationjob", QtDebugMsg)

OCC::HydrationJob::HydrationJob(const CfApiWrapper::CallBackContext &context)
    : QObject(context.vfs)
    , _context(context)
{
    Q_ASSERT(QFileInfo(context.path).isAbsolute());
}

OCC::HydrationJob::~HydrationJob() = default;


int64_t OCC::HydrationJob::requestId() const
{
    return _context.requestId;
}

QString OCC::HydrationJob::localFilePathAbs() const
{
    return _context.path;
}

QString OCC::HydrationJob::remotePathRel() const {return record}

HydrationJob::Status HydrationJob::status() const
{
    return _status;
}

const CfApiWrapper::CallBackContext HydrationJob::context() const
{
    return _context;
}

QString HydrationJob::errorString() const
{
    return _errorString;
}

void HydrationJob::start()
{
    Q_ASSERT(_account);
    Q_ASSERT(_journal);
    Q_ASSERT(!_remoteSyncRootPath.isEmpty() && !_localRoot.isEmpty());
    Q_ASSERT(!_context.fileId.isEmpty());
    Q_ASSERT(_localRoot.endsWith('/'_L1));

    const auto startServer = [this](const QString &serverName) -> QLocalServer * {
        const auto server = new QLocalServer(this);
        const auto listenResult = server->listen(serverName);
        if (!listenResult) {
            qCCritical(lcHydration) << u"Couldn't get server to listen" << serverName << _localRoot << _context;
            if (!_isCancelled) {
                emitFinished(Status::Error);
            }
            return nullptr;
        }
        qCInfo(lcHydration) << u"Server ready, waiting for connections" << serverName << _localRoot << _context;
        return server;
    };

    // Start cancellation server
    _signalServer = startServer(_context.requestHexId() + u":cancellation"_s);
    Q_ASSERT(_signalServer);
    if (!_signalServer) {
        return;
    }
    connect(_signalServer, &QLocalServer::newConnection, this, &HydrationJob::onCancellationServerNewConnection);

    // Start transfer data server
    _transferDataServer = startServer(_context.requestHexId());
    Q_ASSERT(_transferDataServer);
    if (!_transferDataServer) {
        return;
    }
    connect(_transferDataServer, &QLocalServer::newConnection, this, &HydrationJob::onNewConnection);
}

void HydrationJob::cancel()
{
    _isCancelled = true;
    if (_job) {
        _job->abort();
    }

    if (_signalSocket) {
        _signalSocket->write("cancelled");
        _signalSocket->close();
    }

    if (_transferDataSocket) {
        _transferDataSocket->close();
    }
    emitFinished(Status::Cancelled);
}

void HydrationJob::emitFinished(Status status)
{
    _status = status;
    if (_signalSocket) {
        _signalSocket->close();
    }

    if (status == Status::Success) {
        connect(_transferDataSocket, &QLocalSocket::disconnected, this, [=, this] {
            _transferDataSocket->close();
            Q_EMIT finished(this);
        });
        _transferDataSocket->disconnectFromServer();
        return;
    }

    // TODO: displlay error to explroer user

    if (_transferDataSocket) {
        _transferDataSocket->close();
    }

    Q_EMIT finished(this);
}

void HydrationJob::onCancellationServerNewConnection()
{
    Q_ASSERT(!_signalSocket);

    qCInfo(lcHydration) << u"Got new connection on cancellation server" << _context;
    _signalSocket = _signalServer->nextPendingConnection();
}

void HydrationJob::onNewConnection()
{
    Q_ASSERT(!_transferDataSocket);
    Q_ASSERT(!_job);
    handleNewConnection();
}

void HydrationJob::finalize(VfsCfApi *vfs)
{
    auto item = SyncFileItem::fromSyncJournalFileRecord(_record);
    if (_isCancelled) {
        // Remove placeholder file because there might be already pumped
        // some data into it
        QFile::remove(localFilePathAbs());
        // Create a new placeholder file
        vfs->createPlaceholder(*item);
        return;
    }

    switch (_status) {
    case Status::Success:
        item->_type = ItemTypeFile;
        break;
    case Status::Error:
        [[fallthrough]];
    case Status::Cancelled:
        item->_type = ItemTypeVirtualFile;
        break;
    };
    if (QFileInfo::exists(localFilePathAbs())) {
        FileSystem::getInode(FileSystem::toFilesystemPath(localFilePathAbs()), &item->_inode);
        const auto result = _journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
        if (!result) {
            qCWarning(lcHydration) << u"Error when setting the file record to the database" << _context << result.error();
        }
    } else {
        qCWarning(lcHydration) << u"Hydration succeeded but the file appears to be moved" << _context;
    }
}

void HydrationJob::handleNewConnection()
{
    qCInfo(lcHydration) << u"Got new connection starting GETFileJob" << _context;
    _hydrationJob = new OCC::HydrationJob(context().vfs, _remoteFilePathRel, std::unique_ptr(_transferDataServer->nextPendingConnection()), this);
    _hydrationJob->start();
    connect(_hydrationJob, &OCC::HydrationJob::finished, this, [this] { emitFinished(Status::Success); });
    connect(_hydrationJob, &OCC::HydrationJob::error, this, [this](const QString &error) {
        _errorString = error;
        qCWarning(lcHydration) << u"HydrationJob error" << _context << error;
        emitFinished(Status::Error);
    });
}
