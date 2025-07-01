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

#include "account.h"

#include <QJsonParseError>


namespace OCC {

class OPENCLOUD_SYNC_EXPORT WebFinger : public QObject
{
    Q_OBJECT
public:
    WebFinger(QNetworkAccessManager *nam, QObject *parent = nullptr);

    void start(const QUrl &url, const QString &resourceId);

    const QJsonParseError &error() const;

    const QUrl &href() const;

    /***
     * ID used to describe our rel attribute
     * Defined on the server in services/webfinger/pkg/relations/owncloud_instance.go as OpenCloudInstanceRel
     * The server uses "http://webfinger.opencloud/rel/server-instance" (without .eu)
     * 
     * According to RFC 7033 (WebFinger), the "rel" is either a URI or a registered relation type
     * as specified in RFC 5988 (Web Linking). For custom relation types like this one, 
     * a URI format is used as described in RFC 7033 section 10.3:
     * https://datatracker.ietf.org/doc/html/rfc7033#section-10.3
     */
    static QString relId();

Q_SIGNALS:
    void finished();

private:
    QNetworkAccessManager *_nam;
    QJsonParseError _error;
    QUrl _href;
};
}
