/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "cfapiwrapper.h"
#include "libsync/account.h"
#include "libsync/common/syncjournalfilerecord.h"
#include "vfs/hydrationjob.h"

#include <QNetworkReply>

class QLocalServer;
class QLocalSocket;

namespace OCC {
class GETFileJob;
class SyncJournalDb;
class VfsCfApi;
}

using namespace OCC;

// TODO: check checksums
class HydrationJob : public QObject
{
    Q_OBJECT
public:
    enum class Status : uint8_t {
        Success = 0,
        Error,
        Cancelled,
    };
    Q_ENUM(Status)

    explicit HydrationJob(const CfApiWrapper::CallBackContext &context);

    ~HydrationJob() override;

    int64_t requestId() const;

    QString localFilePathAbs() const;

    QString remotePathRel() const;
    void setRemoteFilePathRel(const QString &path);

    Status status() const;

    const CfApiWrapper::CallBackContext context() const;

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

    void handleNewConnection();
    void handleNewConnectionForEncryptedFile();

    void startServerAndWaitForConnections();

    QUrl _remoteSyncRootPath;
    QString _localRoot;

    CfApiWrapper::CallBackContext _context;
    QString _remoteFilePathRel;

    QLocalServer *_transferDataServer = nullptr;
    QLocalServer *_signalServer = nullptr;
    QLocalSocket *_signalSocket = nullptr;
    OCC::HydrationJob *_hydrationJob;
    Status _status = Status::Success;
    QString _errorString;
    QString _remoteParentPath;
};

} // namespace OCC
