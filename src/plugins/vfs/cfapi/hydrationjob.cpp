/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hydrationjob.h"

#include "common/syncjournaldb.h"
#include "plugins/vfs/cfapi/vfs_cfapi.h"
#include "propagatedownload.h"

#include "filesystem.h"

#include <QLocalServer>
#include <QLocalSocket>

using namespace Qt::Literals::StringLiterals;

Q_LOGGING_CATEGORY(lcHydration, "sync.vfs.hydrationjob", QtDebugMsg)

OCC::HydrationJob::HydrationJob(VfsCfApi *parent)
    : QObject(parent)
    , _parent(parent)
{
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

QString OCC::HydrationJob::requestId() const
{
    return _requestId;
}

void OCC::HydrationJob::setRequestId(const QString &requestId)
{
    _requestId = requestId;
}

QString OCC::HydrationJob::localFilePathAbs() const
{
    return _localFilePathAbs;
}

void OCC::HydrationJob::setLocalFilePathAbs(const QString &folderPath)
{
    _localFilePathAbs = folderPath;
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
    Q_ASSERT(!_requestId.isEmpty() && !_localFilePathAbs.isEmpty());

    Q_ASSERT(_localRoot.endsWith('/'_L1));
    Q_ASSERT(!_localFilePathAbs.startsWith('/'_L1));

    const auto startServer = [this](const QString &serverName) -> QLocalServer * {
        const auto server = new QLocalServer(this);
        const auto listenResult = server->listen(serverName);
        if (!listenResult) {
            qCCritical(lcHydration) << "Couldn't get server to listen" << serverName << _localRoot << _localFilePathAbs;
            if (!_isCancelled) {
                emitFinished(Status::Error);
            }
            return nullptr;
        }
        qCInfo(lcHydration) << "Server ready, waiting for connections" << serverName << _localRoot << _localFilePathAbs;
        return server;
    };

    // Start cancellation server
    _signalServer = startServer(_requestId + u":cancellation"_s);
    Q_ASSERT(_signalServer);
    if (!_signalServer) {
        return;
    }
    connect(_signalServer, &QLocalServer::newConnection, this, &HydrationJob::onCancellationServerNewConnection);

    // Start transfer data server
    _transferDataServer = startServer(_requestId);
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

    qCInfo(lcHydration) << "Got new connection on cancellation server" << _requestId << _localFilePathAbs;
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
    if (_isCancelled) {
        // Remove placeholder file because there might be already pumped
        // some data into it
        QFile::remove(_localFilePathAbs);
        // Create a new placeholder file
        const auto item = SyncFileItem::fromSyncJournalFileRecord(_record);
        vfs->createPlaceholder(*item);
        return;
    }

    switch (_status) {
    case Status::Success:
        _record._type = ItemTypeFile;
        break;
    case Status::Error:
        [[fallthrough]];
    case Status::Cancelled:
        _record._type = CSyncEnums::ItemTypeVirtualFile;
        break;
    };
    const auto inode = _record._inode;
    FileSystem::getInode(_localFilePathAbs, &_record._inode);

    // we don't expect an inode change
    Q_ASSERT(inode == _record._inode);

    const auto result = _journal->setFileRecord(_record);
    if (!result) {
        qCWarning(lcHydration) << "Error when setting the file record to the database" << _record._path << result.error();
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
        if (size != _record._fileSize) {
            _errorCode = QNetworkReply::UnknownContentError;
            _errorString = u"Unexpected file size transfered. Expected %1 received %2"_s.arg(QString::number(_record._fileSize), QString::number(size));
            // assume that the local and the remote metadate are out of sync
            Q_EMIT _parent->needSync();
        }
    }
    if (!_errorString.isEmpty()) {
        qCInfo(lcHydration) << "GETFileJob finished" << _requestId << _localFilePathAbs << _errorCode << _statusCode << _errorString;
    } else {
        qCInfo(lcHydration) << "GETFileJob finished" << _requestId << _localFilePathAbs;
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
    qCInfo(lcHydration) << "Got new connection starting GETFileJob" << _requestId << _localFilePathAbs;
    _transferDataSocket = _transferDataServer->nextPendingConnection();
    _job = new GETFileJob(_account, _remoteSyncRootPath, _remoteFilePathRel, _transferDataSocket, {}, {}, 0, this);
    _job->setExpectedContentLength(_record._fileSize);
    connect(_job, &GETFileJob::finishedSignal, this, &HydrationJob::onGetFinished);
    _job->start();
}
