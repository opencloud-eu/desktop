/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#pragma once

#include "owncloudpropagator.h"
#include "syncfileitem.h"
#include "networkjobs.h"
#include <QLoggingCategory>
#include <QNetworkReply>

namespace OCC {

inline QString getEtagFromReply(QNetworkReply *reply)
{
    QByteArray rawEtag = reply->rawHeader("OC-ETag");
    if (rawEtag.isEmpty()) {
        rawEtag = reply->rawHeader("ETag");
    }
    return Utility::normalizeEtag(QString::fromUtf8(rawEtag));
}

/**
 * Given an error from the network, map to a SyncFileItem::Status error
 */
inline SyncFileItem::Status classifyError(
    QNetworkReply::NetworkError nerror, int httpCode, bool *anotherSyncNeeded = nullptr, const QByteArray &errorBody = QByteArray())
{
    Q_ASSERT(nerror != QNetworkReply::NoError); // we should only be called when there is an error

    if (nerror == QNetworkReply::RemoteHostClosedError) {
        // Sometimes server bugs lead to a connection close on certain files,
        // that shouldn't bring the rest of the syncing to a halt.
        return SyncFileItem::NormalError;
    }

    if (nerror > QNetworkReply::NoError && nerror <= QNetworkReply::UnknownProxyError) {
        // A *transient* connectivity error on a single file must NOT abort the whole sync
        // run. The blanket FatalError below returns up to propagator()->abort(), which wedges
        // a large multi-day sync on one network blip. Treat the recoverable connectivity
        // errors as a per-file NormalError and request another pass so the file is
        // re-discovered and retried; keep FatalError only for genuinely fatal cases
        // (TLS handshake, proxy auth, redirect loops, ...).
        switch (nerror) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyTimeoutError:
            if (anotherSyncNeeded != nullptr) {
                *anotherSyncNeeded = true;
            }
            return SyncFileItem::NormalError;
        default:
            // network error or proxy error -> fatal
            return SyncFileItem::FatalError;
        }
    }

    switch (httpCode) {
    case 423:
        // "Locked"
        // Should be temporary.
        if (anotherSyncNeeded != nullptr) {
            *anotherSyncNeeded = true;
        }
        return SyncFileItem::Message;
    case 425:
        // "Too Early"
        // The file is currently post processed after an upload
        // once the post processing finished we get a new etag and retry
        return SyncFileItem::Message;
    case 502:
        // "Bad Gateway"
        // Should be temporary.
        if (anotherSyncNeeded != nullptr) {
            *anotherSyncNeeded = true;
        }
        Q_FALLTHROUGH();
    case 412:
        // "Precondition Failed"
        // Happens when the e-tag has changed
        return SyncFileItem::SoftError;
    case 503: {
        // When the server is in maintenance mode, we want to exit the sync immediatly
        // so that we do not flood the server with many requests
        // BUG: This relies on a translated string and is thus unreliable.
        //      In the future it should return a NormalError and trigger a status.php
        //      check that detects maintenance mode reliably and will terminate the sync run.
        auto probablyMaintenance =
                errorBody.contains(R"(>Sabre\DAV\Exception\ServiceUnavailable<)")
                && !errorBody.contains("Storage is temporarily not available");
        return probablyMaintenance ? SyncFileItem::FatalError : SyncFileItem::NormalError;
    }
    }
    return SyncFileItem::NormalError;
}
}
