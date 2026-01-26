// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/abstractnetworkjob.h"
#include "libsync/accountfwd.h"
#include "libsync/bandwidthmanager.h"
#include "libsync/opencloudsynclib.h"
#include "libsync/syncfileitem.h"


namespace OCC {
/**
 * @brief Downloads the remote file via GET
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT GETFileJob : public AbstractNetworkJob
{
    Q_OBJECT
    QIODevice *_device;
    QMap<QByteArray, QByteArray> _headers;
    QString _expectedEtagForResume;
    std::optional<uint64_t> _expectedContentLength;
    std::optional<uint64_t> _contentLength;
    uint64_t _resumeStart;

public:
    // DOES NOT take ownership of the device.
    // For directDownloadUrl:
    explicit GETFileJob(AccountPtr account, const QUrl &url, const QString &path, QIODevice *device, const QMap<QByteArray, QByteArray> &headers,
        const QString &expectedEtagForResume, uint64_t resumeStart, QObject *parent = nullptr);
    virtual ~GETFileJob();

    uint64_t currentDownloadPosition();

    void start() override;
    void finished() override;

    void newReplyHook(QNetworkReply *reply) override;

    uint64_t resumeStart() const;

    std::optional<uint64_t> contentLength() const;
    std::optional<uint64_t> expectedContentLength() const;
    void setExpectedContentLength(uint64_t size);

    void setChoked(bool c);
    void setBandwidthLimited(bool b);
    void giveBandwidthQuota(qint64 q);
    void setBandwidthManager(BandwidthManager *bwm);

    QString &etag() { return _etag; }
    time_t lastModified() { return _lastModified; }

    void setErrorString(const QString &s) { _errorString = s; }
    QString errorString() const;
    SyncFileItem::Status errorStatus() { return _errorStatus; }
    void setErrorStatus(const SyncFileItem::Status &s) { _errorStatus = s; }

private Q_SLOTS:
    void slotReadyRead();
    void slotMetaDataChanged();

Q_SIGNALS:
    void downloadProgress(int64_t, int64_t);

protected:
    bool restartDevice();

    QString _etag;
    time_t _lastModified = 0;
    QString _errorString;
    SyncFileItem::Status _errorStatus = SyncFileItem::NoStatus;
    bool _bandwidthLimited = false; // if _bandwidthQuota will be used
    bool _bandwidthChoked = false; // if download is paused (won't read on readyRead())
    qint64 _bandwidthQuota = 0;
    bool _httpOk = false;
    QPointer<BandwidthManager> _bandwidthManager = nullptr;
};
}
