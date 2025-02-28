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

#include "qmlcredentials.h"

#include "libsync/theme.h"

#include <QClipboard>
#include <QGuiApplication>

using namespace OCC;


QmlCredentials::QmlCredentials(const QUrl &host, const QString &displayName, QObject *parent)
    : QObject(parent)
    , _host(host)
    , _displayName(displayName)
{
}

bool QmlCredentials::isRefresh() const
{
    return _isRefresh;
}

void QmlCredentials::setIsRefresh(bool newIsRefresh)
{
    if (_isRefresh != newIsRefresh) {
        _isRefresh = newIsRefresh;
        Q_EMIT isRefreshChanged();
    }
}

QmlOAuthCredentials::QmlOAuthCredentials(OAuth *oauth, const QUrl &host, const QString &displayName, QObject *parent)
    : QmlCredentials(host, displayName, parent)
    , _oauth(oauth)
{
    connect(_oauth, &OAuth::authorisationLinkChanged, this, [this] {
        _ready = true;
        Q_EMIT readyChanged();
    });
    connect(_oauth, &QObject::destroyed, this, &QmlOAuthCredentials::readyChanged);
}

void QmlOAuthCredentials::copyAuthenticationUrlToClipboard()
{
    qApp->clipboard()->setText(_oauth->authorisationLink().toString(QUrl::FullyEncoded));
}

void QmlOAuthCredentials::openAuthenticationUrlInBrowser()
{
    _oauth->openBrowser();
}

bool QmlOAuthCredentials::isReady() const
{
    return isValid() && _ready;
}

bool QmlOAuthCredentials::isValid() const
{
    return _oauth;
}
