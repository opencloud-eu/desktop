// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/networkjobs/getfilejob.h"

#include "libsync/owncloudpropagator_p.h"

using namespace OCC;


Q_LOGGING_CATEGORY(lcGetJob, "sync.networkjob.get", QtInfoMsg)

// DOES NOT take ownership of the device.
GETFileJob::GETFileJob(AccountPtr account, const QUrl &url, const QString &path, QIODevice *device, const QMap<QByteArray, QByteArray> &headers,
    const QString &expectedEtagForResume, qint64 resumeStart, QObject *parent)
    : AbstractNetworkJob(account, url, path, parent)
    , _device(device)
    , _headers(headers)
    , _expectedEtagForResume(expectedEtagForResume)
    , _expectedContentLength(-1)
    , _contentLength(-1)
    , _resumeStart(resumeStart)
{
    connect(this, &GETFileJob::networkError, this, [this] {
        if (timedOut()) {
            qCWarning(lcGetJob) << this << u"timeout";
            _errorString = tr("Connection Timeout");
            _errorStatus = SyncFileItem::FatalError;
        }
    });

    // Long downloads must not block non-propagation jobs.
    setPriority(QNetworkRequest::LowPriority);
}

void GETFileJob::start()
{
    if (_resumeStart > 0) {
        _headers["Range"] = "bytes=" + QByteArray::number(_resumeStart) + '-';
        _headers["Accept-Ranges"] = "bytes";
        qCDebug(lcGetJob) << u"Retry with range " << _headers["Range"];
    }

    QNetworkRequest req;
    for (auto it = _headers.cbegin(); it != _headers.cend(); ++it) {
        req.setRawHeader(it.key(), it.value());
    }

    sendRequest("GET", req);

    qCDebug(lcGetJob) << _bandwidthManager << _bandwidthChoked << _bandwidthLimited;
    if (_bandwidthManager) {
        _bandwidthManager->registerDownloadJob(this);
    }
    AbstractNetworkJob::start();
}

void GETFileJob::finished()
{
    if (_bandwidthManager) {
        _bandwidthManager->unregisterDownloadJob(this);
    }
    if (reply()->bytesAvailable() && _httpOk) {
        // we were throttled, write out the remaining data
        slotReadyRead();
        Q_ASSERT(!reply()->bytesAvailable());
    }
}

void GETFileJob::newReplyHook(QNetworkReply *reply)
{
    reply->setReadBufferSize(16 * 1024); // keep low so we can easier limit the bandwidth

    connect(reply, &QNetworkReply::metaDataChanged, this, &GETFileJob::slotMetaDataChanged);
    connect(reply, &QNetworkReply::finished, this, &GETFileJob::slotReadyRead);
    connect(reply, &QNetworkReply::downloadProgress, this, &GETFileJob::downloadProgress);
}

void GETFileJob::slotMetaDataChanged()
{
    // For some reason setting the read buffer in GETFileJob::start doesn't seem to go
    // through the HTTP layer thread(?)
    reply()->setReadBufferSize(16 * 1024);

    int httpStatus = reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (httpStatus == 301 || httpStatus == 302 || httpStatus == 303 || httpStatus == 307 || httpStatus == 308 || httpStatus == 401) {
        // Redirects and auth failures (OAuth token renew) are handled by AbstractNetworkJob and
        // will end up restarting the job. We do not want to process further data from the initial
        // request. newReplyHook() will reestablish signal connections for the follow-up request.
        disconnect(reply(), &QNetworkReply::finished, this, &GETFileJob::slotReadyRead);
        disconnect(reply(), &QNetworkReply::readyRead, this, &GETFileJob::slotReadyRead);
        return;
    }

    // If the status code isn't 2xx, don't write the reply body to the file.
    // For any error: handle it when the job is finished, not here.
    if (httpStatus / 100 != 2) {
        // Disable the buffer limit, as we don't limit the bandwidth for error messages.
        // (We are only going to do a readAll() at the end.)
        reply()->setReadBufferSize(0);
        return;
    }
    if (reply()->error() != QNetworkReply::NoError) {
        return;
    }
    _etag = getEtagFromReply(reply());

    if (_etag.isEmpty()) {
        qCWarning(lcGetJob) << u"No E-Tag reply by server, considering it invalid";
        _errorString = tr("No E-Tag received from server, check Proxy/Gateway");
        _errorStatus = SyncFileItem::NormalError;
        abort();
        return;
    } else if (!_expectedEtagForResume.isEmpty() && _expectedEtagForResume != _etag) {
        qCWarning(lcGetJob) << u"We received a different E-Tag for resuming!" << _expectedEtagForResume << u"vs" << _etag;
        _errorString = tr("We received a different E-Tag for resuming. Retrying next time.");
        _errorStatus = SyncFileItem::NormalError;
        abort();
        return;
    }

    bool ok;
    _contentLength = reply()->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
    if (ok && _expectedContentLength != -1 && _contentLength != _expectedContentLength) {
        qCWarning(lcGetJob) << u"We received a different content length than expected!" << _expectedContentLength << u"vs" << _contentLength;
        _errorString = tr("We received an unexpected download Content-Length.");
        _errorStatus = SyncFileItem::NormalError;
        abort();
        return;
    }

    qint64 start = 0;
    const QString ranges = QString::fromUtf8(reply()->rawHeader("Content-Range"));
    if (!ranges.isEmpty()) {
        static QRegularExpression rx(QStringLiteral("bytes (\\d+)-"));
        const auto match = rx.match(ranges);
        if (match.hasMatch()) {
            start = match.captured(1).toLongLong();
        }
    }
    if (start != _resumeStart) {
        qCWarning(lcGetJob) << u"Wrong content-range: " << ranges << u" while expecting start was" << _resumeStart;
        if (ranges.isEmpty()) {
            // device doesn't support range, just try again from scratch
            _device->close();
            if (!_device->open(QIODevice::WriteOnly)) {
                _errorString = _device->errorString();
                _errorStatus = SyncFileItem::NormalError;
                abort();
                return;
            }
            _resumeStart = 0;
        } else {
            _errorString = tr("Server returned wrong content-range");
            _errorStatus = SyncFileItem::NormalError;
            abort();
            return;
        }
    }

    auto lastModified = reply()->header(QNetworkRequest::LastModifiedHeader);
    if (!lastModified.isNull()) {
        _lastModified = Utility::qDateTimeToTime_t(lastModified.toDateTime());
    }
    _httpOk = true;
    connect(reply(), &QIODevice::readyRead, this, &GETFileJob::slotReadyRead);
}

void GETFileJob::setBandwidthManager(BandwidthManager *bwm)
{
    _bandwidthManager = bwm;
}

void GETFileJob::setChoked(bool c)
{
    if (c != _bandwidthChoked) {
        _bandwidthChoked = c;
        QMetaObject::invokeMethod(this, &GETFileJob::slotReadyRead, Qt::QueuedConnection);
    }
}

void GETFileJob::setBandwidthLimited(bool b)
{
    if (_bandwidthLimited != b) {
        _bandwidthLimited = b;
        QMetaObject::invokeMethod(this, &GETFileJob::slotReadyRead, Qt::QueuedConnection);
    }
}

void GETFileJob::giveBandwidthQuota(qint64 q)
{
    _bandwidthQuota = q;
    qCDebug(lcGetJob) << u"Got" << q << u"bytes";
    QMetaObject::invokeMethod(this, &GETFileJob::slotReadyRead, Qt::QueuedConnection);
}

qint64 GETFileJob::currentDownloadPosition()
{
    if (_device && _device->pos() > 0 && _device->pos() > qint64(_resumeStart)) {
        return _device->pos();
    }
    return _resumeStart;
}

void GETFileJob::slotReadyRead()
{
    Q_ASSERT(reply());
    if (!_httpOk) {
        return;
    }

    const qint64 bufferSize = std::min<qint64>(1024 * 8, reply()->bytesAvailable());
    QByteArray buffer(bufferSize, Qt::Uninitialized);

    while (reply()->bytesAvailable() > 0) {
        if (_bandwidthChoked) {
            qCWarning(lcGetJob) << u"Download choked";
            break;
        }
        qint64 toRead = bufferSize;
        if (_bandwidthLimited) {
            toRead = std::min<qint64>(bufferSize, _bandwidthQuota);
            if (toRead == 0) {
                qCWarning(lcGetJob) << u"Out of badnwidth quota";
                break;
            }
            _bandwidthQuota -= toRead;
        }

        const qint64 read = reply()->read(buffer.data(), toRead);
        if (read < 0) {
            _errorString = networkReplyErrorString(*reply());
            _errorStatus = SyncFileItem::NormalError;
            qCWarning(lcGetJob) << u"Error while reading from device: " << _errorString;
            abort();
            return;
        }

        const qint64 written = _device->write(buffer.constData(), read);
        if (written != read) {
            _errorString = _device->errorString();
            _errorStatus = SyncFileItem::NormalError;
            qCWarning(lcGetJob) << u"Error while writing to file" << written << read << _errorString;
            abort();
            return;
        }
    }
}


GETFileJob::~GETFileJob()
{
    if (_bandwidthManager) {
        _bandwidthManager->unregisterDownloadJob(this);
    }
}

QString GETFileJob::errorString() const
{
    if (!_errorString.isEmpty()) {
        return _errorString;
    }
    return AbstractNetworkJob::errorString();
}
