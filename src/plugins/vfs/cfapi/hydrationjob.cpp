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

Q_LOGGING_CATEGORY(lcHydration, "nextcloud.sync.vfs.hydrationjob", QtInfoMsg)

OCC::HydrationJob::HydrationJob(QObject *parent)
    : QObject(parent)
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

QString OCC::HydrationJob::localPath() const
{
    return _localPath;
}

void OCC::HydrationJob::setLocalPath(const QString &localPath)
{
    _localPath = localPath;
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

QString OCC::HydrationJob::folderPath() const
{
    return _folderPath;
}

void OCC::HydrationJob::setFolderPath(const QString &folderPath)
{
    _folderPath = folderPath;
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
    Q_ASSERT(!_remoteSyncRootPath.isEmpty() && !_localPath.isEmpty());
    Q_ASSERT(!_requestId.isEmpty() && !_folderPath.isEmpty());

    Q_ASSERT(_localPath.endsWith('/'_L1));
    Q_ASSERT(!_folderPath.startsWith('/'_L1));

    const auto startServer = [this](const QString &serverName) -> QLocalServer * {
        const auto server = new QLocalServer(this);
        const auto listenResult = server->listen(serverName);
        if (!listenResult) {
            qCCritical(lcHydration) << "Couldn't get server to listen" << serverName << _localPath << _folderPath;
            if (!_isCancelled) {
                emitFinished(Error);
            }
            return nullptr;
        }
        qCInfo(lcHydration) << "Server ready, waiting for connections" << serverName << _localPath << _folderPath;
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
    emitFinished(Cancelled);
}

void OCC::HydrationJob::emitFinished(Status status)
{
    _status = status;
    if (_signalSocket) {
        _signalSocket->close();
    }

    if (status == Success) {
        connect(_transferDataSocket, &QLocalSocket::disconnected, this, [=, this] {
            _transferDataSocket->close();
            Q_EMIT finished(this);
        });
        _transferDataSocket->disconnectFromServer();
        return;
    }

    if (_transferDataSocket) {
        _transferDataSocket->close();
    }

    Q_EMIT finished(this);
}

void OCC::HydrationJob::onCancellationServerNewConnection()
{
    Q_ASSERT(!_signalSocket);

    qCInfo(lcHydration) << "Got new connection on cancellation server" << _requestId << _folderPath;
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
    // Mark the file as hydrated in the sync journal
    SyncJournalFileRecord record;
    if (!_journal->getFileRecord(_folderPath, &record)) {
        qCWarning(lcHydration) << "could not get file from local DB" << _folderPath;
        return;
    }
    Q_ASSERT(record.isValid());
    if (!record.isValid()) {
        qCWarning(lcHydration) << "Couldn't find record to update after hydration" << _requestId << _folderPath;
        // emitFinished(Error);
        return;
    }

    if (_isCancelled) {
        // Remove placeholder file because there might be already pumped
        // some data into it
        QFile::remove(_localPath + _folderPath);
        // Create a new placeholder file
        const auto item = SyncFileItem::fromSyncJournalFileRecord(record);
        vfs->createPlaceholder(*item);
        return;
    }

    switch (_status) {
    case Success:
        record._type = ItemTypeFile;
        break;
    case Error:
    case Cancelled:
        record._type = CSyncEnums::ItemTypeVirtualFile;
        break;
    };

    // store the actual size of a file that has been decrypted as we will need its actual size when dehydrating it if requested
    record._fileSize = FileSystem::getSize(QFileInfo{localPath() + folderPath()});

    const auto result = _journal->setFileRecord(record);
    if (!result) {
        qCWarning(lcHydration) << "Error when setting the file record to the database" << record._path << result.error();
    }
}

void OCC::HydrationJob::onGetFinished()
{
    _errorCode = _job->reply()->error();
    _statusCode = _job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (_errorCode != 0 || (_statusCode != 200 && _statusCode != 204)) {
        _errorString = _job->reply()->errorString();
    }

    if (!_errorString.isEmpty()) {
        qCInfo(lcHydration) << "GETFileJob finished" << _requestId << _folderPath << _errorCode << _statusCode << _errorString;
    } else {
        qCInfo(lcHydration) << "GETFileJob finished" << _requestId << _folderPath;
    }
    // GETFileJob deletes itself after this signal was handled
    _job = nullptr;
    if (_isCancelled) {
        _errorCode = 0;
        _statusCode = 0;
        _errorString.clear();
        return;
    }

    if (_errorCode) {
        emitFinished(Error);
        return;
    }
    emitFinished(Success);
}

void OCC::HydrationJob::handleNewConnection()
{
    qCInfo(lcHydration) << "Got new connection starting GETFileJob" << _requestId << _folderPath;
    _transferDataSocket = _transferDataServer->nextPendingConnection();
    _job = new GETFileJob(_account, _remoteSyncRootPath, _folderPath, _transferDataSocket, {}, {}, 0, this);
    connect(_job, &GETFileJob::finishedSignal, this, &HydrationJob::onGetFinished);
    _job->start();
}
