/*
 * Copyright (C) by Hannah von Reth <h.vonreth@opencloud.eu>
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

#include "tokencredentials.h"

#include "creds/httpcredentials.h"
#include "libsync/accessmanager.h"

using namespace OCC;

class OCC::TokensAccessManager : public AccessManager
{
    Q_OBJECT
public:
    TokensAccessManager(QByteArray &&username, QByteArray &&token, QObject *parent)
        : AccessManager(parent)
        , _token("Basic " + QByteArray(username + ':' + token).toBase64())
    {
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) override
    {
        QNetworkRequest req(request);
        if (!req.attribute(HttpCredentials::DontAddCredentialsAttribute).toBool()) {
            req.setRawHeader("Authorization", _token);
        }
        return AccessManager::createRequest(op, req, outgoingData);
    }

private:
    QByteArray _token;
};

TokenCredentials::TokenCredentials(QByteArray &&username, QByteArray &&token)
    : _accessManager(new TokensAccessManager(std::move(username), std::move(token), this))
{
    connect(_accessManager, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
            Q_EMIT authenticationFailed();
        }
    });
}

AccessManager *TokenCredentials::createAM() const
{
    return _accessManager;
}

bool TokenCredentials::ready() const
{
    return true;
}

void TokenCredentials::fetchFromKeychain() { }

void TokenCredentials::restartOauth() { }

void TokenCredentials::persist() { }

void TokenCredentials::invalidateToken() { }

void TokenCredentials::forgetSensitiveData() { }


#include "tokencredentials.moc"
