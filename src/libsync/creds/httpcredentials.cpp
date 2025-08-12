/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 * Copyright (C) by Krzesimir Nowak <krzesimir@endocode.com>
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
#include "creds/httpcredentials.h"

#include "accessmanager.h"
#include "account.h"
#include "configfile.h"
#include "creds/credentialmanager.h"
#include "oauth.h"
#include "syncengine.h"

#include <QAuthenticator>
#include <QLoggingCategory>
#include <QNetworkInformation>
#include <QNetworkReply>

#include <chrono>

using namespace std::chrono_literals;

Q_LOGGING_CATEGORY(lcHttpCredentials, "sync.credentials.http", QtInfoMsg)

namespace {
constexpr int TokenRefreshMaxRetries = 3;
constexpr std::chrono::seconds TokenRefreshDefaultTimeout = 30s;
const char authenticationFailedC[] = "opencloud-authentication-failed";

auto refreshTokenKeyC()
{
    return QStringLiteral("http/oauthtoken");
}
}

namespace OCC {

class HttpCredentialsAccessManager : public AccessManager
{
    Q_OBJECT
public:
    HttpCredentialsAccessManager(const HttpCredentials *cred, QObject *parent = nullptr)
        : AccessManager(parent)
        , _cred(cred)
    {
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        QNetworkRequest req(request);
        if (!req.attribute(HttpCredentials::DontAddCredentialsAttribute).toBool()) {
            if (_cred && !_cred->_accessToken.isEmpty()) {
                req.setRawHeader("Authorization", "Bearer " + _cred->_accessToken.toUtf8());
            }
        }
        return AccessManager::createRequest(op, req, outgoingData);
    }

private:
    // The credentials object dies along with the account, while the QNAM might
    // outlive both.
    QPointer<const HttpCredentials> _cred;
};

HttpCredentials::HttpCredentials(const QString &accessToken)
    : _accessToken(accessToken)
    , _ready(true)
{
}

AccessManager *HttpCredentials::createAM() const
{
    AccessManager *am = new HttpCredentialsAccessManager(this);

    connect(am, &QNetworkAccessManager::authenticationRequired,
        this, &HttpCredentials::slotAuthentication);

    return am;
}

bool HttpCredentials::ready() const
{
    return _ready;
}

void HttpCredentials::fetchFromKeychain()
{
    _wasFetched = true;

    if (!_ready && !_refreshToken.isEmpty()) {
        // This happens if the credentials are still loaded from the keychain, bur we are called
        // here because the auth is invalid, so this means we simply need to refresh the credentials
        refreshAccessToken();
        return;
    }

    if (_ready) {
        Q_EMIT fetched();
    } else {
        fetchFromKeychainHelper();
    }
}

void HttpCredentials::fetchFromKeychainHelper()
{
    auto job = _account->credentialManager()->get(refreshTokenKeyC());
    connect(job, &CredentialJob::finished, this, [job, this] {
        auto handleError = [job, this] {
            qCWarning(lcHttpCredentials) << u"Could not retrieve client password from keychain" << job->errorString();

            // we come here if the password is empty or any other keychain
            // error happend.

            _fetchErrorString = job->error() != QKeychain::EntryNotFound ? job->errorString() : QString();

            _accessToken.clear();
            _ready = false;
            Q_EMIT fetched();
        };
        if (job->error() != QKeychain::NoError) {
            handleError();
            return;
        }
        const auto data = job->data().toString();
        if (OC_ENSURE(!data.isEmpty())) {
            _refreshToken = data;
            refreshAccessToken();
        } else {
            handleError();
        }
    });
}

void HttpCredentials::checkCredentials(QNetworkReply *reply)
{
    // The function is called in order to determine whether we need to ask the user for a password
    // if we are using OAuth, we already started a refresh in slotAuthentication, at least in theory, ensure the auth is started.
    // If the refresh fails, we are going to Q_EMIT authenticationFailed ourselves
    if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
        slotAuthentication(reply, nullptr);
    }
}

void HttpCredentials::slotAuthentication(QNetworkReply *reply, QAuthenticator *authenticator)
{
    qCDebug(lcHttpCredentials) << Q_FUNC_INFO << reply;
    if (!_ready)
        return;
    Q_UNUSED(authenticator)
    // Because of issue #4326, we need to set the login and password manually at every requests
    // Thus, if we reach this signal, those credentials were invalid and we terminate.
    qCWarning(lcHttpCredentials) << u"Stop request: Authentication failed for " << reply->url().toString() << reply->request().rawHeader("Original-Request-ID");
    reply->setProperty(authenticationFailedC, true);

    if (!_oAuthJob) {
        qCInfo(lcHttpCredentials) << u"Refreshing token";
        refreshAccessToken();
    }
}

bool HttpCredentials::refreshAccessToken()
{
    return refreshAccessTokenInternal(0);
}

bool HttpCredentials::refreshAccessTokenInternal(int tokenRefreshRetriesCount)
{
    if (_refreshToken.isEmpty())
        return false;
    if (_oAuthJob) {
        return true;
    }

    // don't touch _ready or the account state will start a new authentication
    // _ready = false;

    // parent with nam to ensure we reset when the nam is reset
    _oAuthJob = new AccountBasedOAuth(_account->sharedFromThis(), _account->accessManager());
    connect(_oAuthJob, &AccountBasedOAuth::refreshError, this, [tokenRefreshRetriesCount, this](QNetworkReply::NetworkError error, const QString &) {
        _oAuthJob->deleteLater();

        auto networkUnavailable = []() {
            if (auto qni = QNetworkInformation::instance()) {
                if (qni->reachability() == QNetworkInformation::Reachability::Disconnected) {
                    return true;
                }
            }

            return false;
        };

        int nextTry = tokenRefreshRetriesCount + 1;
        std::chrono::seconds timeout = {};

        if (networkUnavailable()) {
            nextTry = 0;
            timeout = TokenRefreshDefaultTimeout;
        } else {
            switch (error) {
            case QNetworkReply::ContentNotFoundError:
                // 404: bigip f5?
                timeout = 0s;
                break;
            case QNetworkReply::HostNotFoundError:
                [[fallthrough]];
            case QNetworkReply::TimeoutError:
                [[fallthrough]];
            // Qt reports OperationCanceledError if the request timed out
            case QNetworkReply::OperationCanceledError:
                [[fallthrough]];
            case QNetworkReply::TemporaryNetworkFailureError:
                [[fallthrough]];
            // VPN not ready?
            case QNetworkReply::ConnectionRefusedError:
                nextTry = 0;
                [[fallthrough]];
            default:
                timeout = TokenRefreshDefaultTimeout;
            }
        }

        if (nextTry >= TokenRefreshMaxRetries) {
            qCWarning(lcHttpCredentials) << u"Too many failed refreshes" << nextTry << u"-> log out";
            forgetSensitiveData();
            Q_EMIT authenticationFailed();
            Q_EMIT fetched();
            return;
        }
        QTimer::singleShot(timeout, this, [nextTry, this] {
            refreshAccessTokenInternal(nextTry);
        });
        Q_EMIT authenticationFailed();
    });

    connect(_oAuthJob, &AccountBasedOAuth::refreshFinished, this, [this](const QString &accessToken, const QString &refreshToken) {
        _oAuthJob->deleteLater();
        if (refreshToken.isEmpty()) {
            // an error occured, log out
            forgetSensitiveData();
            Q_EMIT authenticationFailed();
            Q_EMIT fetched();
            return;
        }
        _refreshToken = refreshToken;
        if (!accessToken.isNull()) {
            _ready = true;
            _accessToken = accessToken;
            persist();
        }
        Q_EMIT fetched();
    });
    Q_EMIT authenticationStarted();
    _oAuthJob->refreshAuthentication(_refreshToken);

    return true;
}

void HttpCredentials::invalidateToken()
{
    qCWarning(lcHttpCredentials) << u"Invalidating the credentials";

    if (!_accessToken.isEmpty()) {
        _previousPassword = _accessToken;
    }
    _accessToken = QString();
    _ready = false;

    // clear the session cookie.
    _account->clearCookieJar();

    if (!_refreshToken.isEmpty()) {
        // Only invalidate the access_token (_password) but keep the _refreshToken in the keychain
        // (when coming from forgetSensitiveData, the _refreshToken is cleared)
        return;
    }

    _account->credentialManager()->clear(QStringLiteral("http"));
    // let QNAM forget about the password
    // This needs to be done later in the event loop because we might be called (directly or
    // indirectly) from QNetworkAccessManagerPrivate::authenticationRequired, which itself
    // is a called from a BlockingQueuedConnection from the Qt HTTP thread. And clearing the
    // cache needs to synchronize again with the HTTP thread.
    QTimer::singleShot(0, _account, &Account::clearAMCache);
}

void HttpCredentials::forgetSensitiveData()
{
    // need to be done before invalidateToken, so it actually deletes the refresh_token from the keychain
    _refreshToken.clear();

    invalidateToken();
    _previousPassword.clear();
}

void HttpCredentials::persist()
{
    // write secrets to the keychain
    // _refreshToken should only be empty when we are logged out...
    if (!_refreshToken.isEmpty()) {
        _account->credentialManager()->set(refreshTokenKeyC(), _refreshToken);
    }
}

} // namespace OCC

#include "httpcredentials.moc"
