/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "libsync/account.h"
#include "libsync/common/syncjournalfilerecord.h"

#include <QNetworkReply>

class QLocalServer;
class QLocalSocket;

namespace OCC {
class GETFileJob;
class SyncJournalDb;
class VfsCfApi;

// TODO: check checksums
class HydrationJob : public QObject
{
    Q_OBJECT
public:
    enum class Status {
        Success = 0,
        Error,
        Cancelled,
    };
    Q_ENUM(Status)

    explicit HydrationJob(VfsCfApi *parent);

    ~HydrationJob() override;

    AccountPtr account() const;
    void setAccount(const AccountPtr &account);

    [[nodiscard]] QUrl remoteSyncRootPath() const;
    void setRemoteSyncRootPath(const QUrl &path);

    QString localRoot() const;
    void setLocalRoot(const QString &localPath);

    SyncJournalDb *journal() const;
    void setJournal(SyncJournalDb *journal);

    QString requestId() const;
    void setRequestId(const QString &requestId);

    QString localFilePathAbs() const;
    void setLocalFilePathAbs(const QString &folderPath);

    QString remotePathRel() const;
    void setRemoteFilePathRel(const QString &path);

    const SyncJournalFileRecord &record() const;
    void setRecord(SyncJournalFileRecord &&record);

    Status status() const;

    [[nodiscard]] int errorCode() const;
    [[nodiscard]] int statusCode() const;
    [[nodiscard]] QString errorString() const;

    void start();
    void cancel();
    void finalize(OCC::VfsCfApi *vfs);

Q_SIGNALS:
    void finished(HydrationJob *job);

private:
    void emitFinished(Status status);

    void onNewConnection();
    void onCancellationServerNewConnection();
    void onGetFinished();

    void handleNewConnection();
    void handleNewConnectionForEncryptedFile();

    void startServerAndWaitForConnections();

    VfsCfApi *_parent;
    AccountPtr _account;
    QUrl _remoteSyncRootPath;
    QString _localRoot;
    SyncJournalDb *_journal = nullptr;
    bool _isCancelled = false;

    QString _requestId;
    QString _localFilePathAbs;
    QString _remoteFilePathRel;

    SyncJournalFileRecord _record;

    QLocalServer *_transferDataServer = nullptr;
    QLocalServer *_signalServer = nullptr;
    QLocalSocket *_transferDataSocket = nullptr;
    QLocalSocket *_signalSocket = nullptr;
    QPointer<GETFileJob> _job;
    Status _status = Status::Success;
    QNetworkReply::NetworkError _errorCode = QNetworkReply::NoError;
    int _statusCode = 0;
    QString _errorString;
    QString _remoteParentPath;
};

} // namespace OCC
