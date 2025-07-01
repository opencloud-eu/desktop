/*
 * Copyright (C) Fabian Müller <fmueller@owncloud.com>
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

#include "account.h"
#include "gui/creds/httpcredentialsgui.h"
#include "networkjobs.h"
#include "networkjobs/fetchuserinfojobfactory.h"

namespace OCC::Wizard {

class OAuth2AuthenticationStrategy
{
public:
    explicit OAuth2AuthenticationStrategy(
        const QString &token, const QString &refreshToken, const QVariantMap &dynamicRegistrationData, const IdToken &idToken);

    HttpCredentialsGui *makeCreds();

    bool isValid();

    FetchUserInfoJobFactory makeFetchUserInfoJobFactory(QNetworkAccessManager *nam);


    const QVariantMap &dynamicRegistrationData() const;
    const IdToken &idToken() const;


private:
    QString _token;
    QString _refreshToken;

    const QVariantMap _dynamicRegistrationData;
    const IdToken _idToken;
};

/**
 * This class constructs an Account object from data entered by the user to the wizard resp. collected while checking the user's information.
 * The class does not perform any kind of validation. It is the caller's job to make sure the data is correct.
 */
class SetupWizardAccountBuilder
{
public:
    SetupWizardAccountBuilder();

    /**
     * Set server URL as well as the authentication type that needs to be used with this server.
     * @param serverUrl URL to server
     */
    void setServerUrl(const QUrl &serverUrl);
    QUrl serverUrl() const;

    void setAuthenticationStrategy(std::unique_ptr<OAuth2AuthenticationStrategy> &&strategy);
    OAuth2AuthenticationStrategy *authenticationStrategy() const;

    /**
     * Check whether credentials passed to the builder so far can be used to create a new account object.
     * Note that this does not mean they are correct, the method only checks whether there is "enough" data.
     * @return true if credentials are valid, false otherwise
     */
    bool hasValidCredentials() const;

    QString displayName() const;
    void setDisplayName(const QString &displayName);

    void setSyncTargetDir(const QString &syncTargetDir);
    QString syncTargetDir() const;

    /**
     * Store custom CA certificate for the newly built account.
     * @param customTrustedCaCertificate certificate to store
     */
    void addCustomTrustedCaCertificate(const QSslCertificate &customTrustedCaCertificate);

    /**
     * Remove all stored custom trusted CA certificates.
     */
    void clearCustomTrustedCaCertificates();

    /**
     * Attempt to build an account from the previously entered information.
     * @return built account or null if information is still missing
     */
    AccountPtr build() const;

    void setWebFingerAuthenticationServerUrl(const QUrl &url);
    QUrl webFingerAuthenticationServerUrl() const;

    void setWebFingerInstances(const QVector<QUrl> &instancesList);
    QVector<QUrl> webFingerInstances() const;

    void setWebFingerSelectedInstance(const QUrl &instance);
    QUrl webFingerSelectedInstance() const;

private:
    QUrl _serverUrl;

    QUrl _webFingerAuthenticationServerUrl;
    QVector<QUrl> _webFingerInstances;
    QUrl _webFingerSelectedInstance;

    std::unique_ptr<OAuth2AuthenticationStrategy> _authenticationStrategy;

    QString _displayName;

    QSet<QSslCertificate> _customTrustedCaCertificates;

    QString _defaultSyncTargetDir;
};
}
