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

OCC::AccountPtr OCC::HydrationJob::account() const
{
    return _account;
}

void OCC::HydrationJob::setAccount(const AccountPtr &account)
{
    _account = account;
}

QUrl OCC::HydrationJob::remoteSyncRootPath() const
{
    return _remoteSyncRootPath;
}

void OCC::HydrationJob::setRemoteSyncRootPath(const QUrl &url)
{
    _remoteSyncRootPath = url;
}

QString OCC::HydrationJob::localRoot() const
{
    return _localRoot;
}

void OCC::HydrationJob::setLocalRoot(const QString &localPath)
{
    _localRoot = localPath;
}

OCC::SyncJournalDb *OCC::HydrationJob::journal() const
{
    return _journal;
}

void OCC::HydrationJob::setJournal(SyncJournalDb *journal)
{
    _journal = journal;
}

int64_t OCC::HydrationJob::requestId() const
{
    return _context.requestId;
}

QString OCC::HydrationJob::localFilePathAbs() const
{
    return _context.path;
}

QString OCC::HydrationJob::remotePathRel() const
{
    return _remoteFilePathRel;
}

void OCC::HydrationJob::setRemoteFilePathRel(const QString &path)
{
    _remoteFilePathRel = path;
}

const OCC::SyncJournalFileRecord &OCC::HydrationJob::record() const
{
    return _record;
}

void OCC::HydrationJob::setRecord(SyncJournalFileRecord &&record)
{
    _record = record;
}

OCC::HydrationJob::Status OCC::HydrationJob::status() const
{
    return _status;
}

const OCC::CfApiWrapper::CallBackContext OCC::HydrationJob::context() const
{
    return _context;
}

int OCC::HydrationJob::errorCode() const
{
    return _errorCode;
}

int OCC::HydrationJob::statusCode() const
{
    return _statusCode;
}

QString OCC::HydrationJob::errorString() const
{
    return _errorString;
}

void OCC::HydrationJob::start()
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

void OCC::HydrationJob::cancel()
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

void OCC::HydrationJob::emitFinished(Status status)
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

void OCC::HydrationJob::onCancellationServerNewConnection()
{
    Q_ASSERT(!_signalSocket);

    qCInfo(lcHydration) << u"Got new connection on cancellation server" << _context;
    _signalSocket = _signalServer->nextPendingConnection();
}

void OCC::HydrationJob::onNewConnection()
{
    Q_ASSERT(!_transferDataSocket);
    Q_ASSERT(!_job);
    handleNewConnection();
}

void OCC::HydrationJob::finalize(OCC::VfsCfApi *vfs)
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

void OCC::HydrationJob::onGetFinished()
{
    _errorCode = _job->reply()->error();
    _statusCode = _job->httpStatusCode();
    if (_errorCode != 0 || (_statusCode != 200 && _statusCode != 204)) {
        _errorString = _job->reply()->errorString();
    }

    if (_job->contentLength() != -1) {
        const auto size = _job->resumeStart() + _job->contentLength();
        if (size != _record.size()) {
            _errorCode = QNetworkReply::UnknownContentError;
            _errorString = u"Unexpected file size transfered. Expected %1 received %2"_s.arg(QString::number(_record.size()), QString::number(size));
            // assume that the local and the remote metadate are out of sync
            Q_EMIT _context.vfs->needSync();
        }
    }
    if (!_errorString.isEmpty()) {
        qCInfo(lcHydration) << u"GETFileJob finished" << _context << _errorCode << _statusCode << _errorString;
    } else {
        qCInfo(lcHydration) << u"GETFileJob finished" << _context;
    }
    if (_isCancelled) {
        _errorCode = QNetworkReply::NoError;
        _statusCode = 0;
        _errorString.clear();
        return;
    }

    if (_errorCode) {
        emitFinished(Status::Error);
        return;
    }

    emitFinished(Status::Success);
}

void OCC::HydrationJob::handleNewConnection()
{
    qCInfo(lcHydration) << u"Got new connection starting GETFileJob" << _context;
    _transferDataSocket = _transferDataServer->nextPendingConnection();
    _job = new GETFileJob(_account, _remoteSyncRootPath, _remoteFilePathRel, _transferDataSocket, {}, {}, 0, this);
    _job->setExpectedContentLength(_record.size());
    connect(_job, &GETFileJob::finishedSignal, this, &HydrationJob::onGetFinished);
    _job->start();
}
