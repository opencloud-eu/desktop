/*
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

#pragma once
#include "accountfwd.h"
#include "libsync/creds/idtoken.h"
#include "libsync/creds/jwt.h"
#include "opencloudsynclib.h"

#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QUrl>

namespace OCC {
class JsonJob;

/**
 * Job that do the authorization grant and fetch the access token
 *
 * Normal workflow:
 *
 *   --> start()
 *       |
 *       +----> fetchWellKnown() query the ".well-known/openid-configuration" endpoint
 *       |
 *       +----> openBrowser() open the browser after fetchWellKnown finished to the specified page
 *       |                    (or the default 'oauth2/authorize' if fetchWellKnown does not exist)
 *       |                    Then the browser will redirect to http://127.0.0.1:xxx
 *       |
 *       +----> _server starts listening on a TCP port waiting for an HTTP request with a 'code'
 *                |
 *                v
 *             request the access_token and the refresh_token via 'apps/oauth2/api/v1/token'
 *                |
 *                +-> Request the user_id is not present
 *                |     |
 *                v     v
 *              finalize(...): Q_EMIT result(...)
 *
 */
class OPENCLOUD_SYNC_EXPORT OAuth : public QObject
{
    Q_OBJECT
public:
    enum Result { LoggedIn, Error, ErrorInsecureUrl };
    Q_ENUM(Result)
    enum class TokenEndpointAuthMethods : char { none, client_secret_basic, client_secret_post };
    Q_ENUM(TokenEndpointAuthMethods)

    enum class PromptValuesSupported : char { none = 0, consent = 1 << 0, select_account = 1 << 1, login = 1 << 2 };
    Q_ENUM(PromptValuesSupported)
    Q_DECLARE_FLAGS(PromptValuesSupportedFlags, PromptValuesSupported)

    OAuth(const QUrl &serverUrl, QNetworkAccessManager *networkAccessManager, const QVariantMap &dynamicRegistrationData, QObject *parent);
    ~OAuth() override;

    void setIdToken(IdToken &&idToken);
    const IdToken &idToken() const;

    QVariantMap dynamicRegistrationData() const;

    virtual void startAuthentication();
    void openBrowser();
    QUrl authorisationLink() const;

    // TODO: private api for tests
    QString clientId() const;
    QString clientSecret() const;

    static void persist(const AccountPtr &accountPtr, const QVariantMap &dynamicRegistrationData, const IdToken &idToken);

Q_SIGNALS:
    /**
     * The state has changed.
     * when logged in, token has the value of the token.
     */
    void result(OAuth::Result result, const QString &token = QString(), const QString &refreshToken = QString());

    /**
     * emitted when the call to the well-known endpoint is finished
     */
    void authorisationLinkChanged();

    void fetchWellKnownFinished();

    void dynamicRegistrationDataReceived();

    void refreshError(QNetworkReply::NetworkError error, const QString &errorString);


protected:
    void updateDynamicRegistration();

    QUrl _serverUrl;
    QVariantMap _dynamicRegistrationData;
    QNetworkAccessManager *_networkAccessManager;
    bool _isRefreshingToken = false;

    QString _clientId;
    QString _clientSecret;

    QUrl _registrationEndpoint;

    virtual void fetchWellKnown();

    QNetworkReply *postTokenRequest(QUrlQuery &&queryItems);


private:
    void finalize(const QPointer<QTcpSocket> &socket, const QString &accessToken, const QString &refreshToken, const QUrl &messageUrl);

    QByteArray generateRandomString(size_t size) const;

    QTcpServer _server;
    bool _wellKnownFinished = false;

    QUrl _authEndpoint;
    QUrl _tokenEndpoint;
    QByteArray _pkceCodeVerifier;
    QByteArray _state;

    IdToken _idToken;

    TokenEndpointAuthMethods _endpointAuthMethod = TokenEndpointAuthMethods::client_secret_basic;
    PromptValuesSupportedFlags _supportedPromtValues = {PromptValuesSupported::consent, PromptValuesSupported::select_account};
};

/**
 * This variant of OAuth uses an account's network access manager etc.
 * Instead of relying on the user to provide a working server URL, a CheckServerJob is run upon start(), which also stores the fetched cookies in the account's state.
 * Furthermore, it takes care of storing and loading the dynamic registration data in the account's credentials manager.
 */
class OPENCLOUD_SYNC_EXPORT AccountBasedOAuth : public OAuth
{
    Q_OBJECT

public:
    explicit AccountBasedOAuth(AccountPtr account, QObject *parent = nullptr);

    void startAuthentication() override;

    void refreshAuthentication(const QString &refreshToken);

Q_SIGNALS:
    void refreshFinished(const QString &accessToken, const QString &refreshToken);
    void restored(QPrivateSignal);


protected:
    void fetchWellKnown() override;


    void restore();

private:
    AccountPtr _account;
    bool _restored = false;
};

QString OPENCLOUD_SYNC_EXPORT toString(OAuth::PromptValuesSupportedFlags s);
Q_DECLARE_OPERATORS_FOR_FLAGS(OAuth::PromptValuesSupportedFlags)
} // namespce OCC
