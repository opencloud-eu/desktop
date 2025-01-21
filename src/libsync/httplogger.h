/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
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

#include "opencloudsynclib.h"

#include <QNetworkReply>
#include <QUrl>

namespace OCC {
namespace HttpLogger {
    void OPENCLOUD_SYNC_EXPORT logRequest(QNetworkReply *reply, QNetworkAccessManager::Operation operation, QIODevice *device);

    /**
    * Helper to construct the HTTP verb used in the request
    */
    QByteArray OPENCLOUD_SYNC_EXPORT requestVerb(QNetworkAccessManager::Operation operation, const QNetworkRequest &request);
    inline QByteArray requestVerb(const QNetworkReply &reply)
    {
        return requestVerb(reply.operation(), reply.request());
    }
}
}
