/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include "creds/httpcredentialsgui.h"
#include "account.h"
#include "accountmodalwidget.h"
#include "application.h"
#include "common/asserts.h"
#include "creds/qmlcredentials.h"
#include "gui/accountsettings.h"
#include "networkjobs.h"
#include "settingsdialog.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QTimer>


namespace OCC {

Q_LOGGING_CATEGORY(lcHttpCredentialsGui, "sync.credentials.http.gui", QtInfoMsg)

HttpCredentialsGui::HttpCredentialsGui(const QString &accessToken, const QString &refreshToken)
    : HttpCredentials(accessToken)
{
    _refreshToken = refreshToken;
}

void HttpCredentialsGui::restartOauth()
{
    qCDebug(lcHttpCredentialsGui) << u"showing modal dialog asking user to log in again via OAuth2";
    if (_asyncAuth) {
        return;
    }
    if (!OC_ENSURE_NOT(_modalWidget)) {
        _modalWidget->deleteLater();
    }
    _asyncAuth.reset(new AccountBasedOAuth(_account->sharedFromThis(), this));
    connect(_asyncAuth.data(), &OAuth::result, this, &HttpCredentialsGui::asyncAuthResult);

    auto *oauthCredentials = new QmlOAuthCredentials(_asyncAuth.data(), _account->url(), _account->davDisplayName());
    _modalWidget = new AccountModalWidget(tr("Login required"), QUrl(QStringLiteral("qrc:/qt/qml/eu/OpenCloud/gui/qml/credentials/OAuthCredentials.qml")),
        oauthCredentials, ocApp()->settingsDialog());

    connect(oauthCredentials, &QmlOAuthCredentials::logOutRequested, _modalWidget, [this] {
        _modalWidget->reject();
        _modalWidget.clear();
        _asyncAuth.reset();
        requestLogout();
    });
    connect(oauthCredentials, &QmlOAuthCredentials::requestRestart, this, [this] {
        _modalWidget->reject();
        _modalWidget.clear();
        _asyncAuth.reset();
        restartOauth();
    });
    connect(this, &HttpCredentialsGui::oAuthLoginAccepted, _modalWidget, &AccountModalWidget::accept);
    connect(this, &HttpCredentialsGui::oAuthErrorOccurred, oauthCredentials, [this]() {
        Q_ASSERT(!ready());
        ocApp()->showSettings();
    });

    ocApp()->settingsDialog()->accountSettings(_account)->addModalWidget(_modalWidget);
    _asyncAuth->startAuthentication();
}

void HttpCredentialsGui::asyncAuthResult(OAuth::Result r, const QString &token, const QString &refreshToken)
{
    _asyncAuth.reset();
    switch (r) {
    case OAuth::ErrorInsecureUrl:
        // should not happen after the initial setup
        Q_ASSERT(false);
        [[fallthrough]];
    case OAuth::Error:
        Q_EMIT oAuthErrorOccurred();
        return;
    case OAuth::LoggedIn:
        Q_EMIT oAuthLoginAccepted();
        break;
    }

    _accessToken = token;
    _refreshToken = refreshToken;
    _ready = true;
    persist();
    Q_EMIT fetched();
}


} // namespace OCC
