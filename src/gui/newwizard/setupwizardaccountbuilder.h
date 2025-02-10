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

/**
 * The server can use varying authentication methods, for instance HTTP Basic or OAuth2.
 * Depending on the concrete authentication method the server uses, the account's credentials must be initialized differently.
 * We use the strategy pattern to be able to model multiple methods and allow adding new ones by just adding another strategy implementation.
 */
class AbstractAuthenticationStrategy
{
public:
    virtual ~AbstractAuthenticationStrategy();

    /**
     * Create credentials object for use in the account.
     * @return credentials
     */
    virtual HttpCredentialsGui *makeCreds() = 0;

    /**
     * Checks whether the passed credentials are valid.
     * @return true if valid, false otherwise
     */
    virtual bool isValid() = 0;

    virtual FetchUserInfoJobFactory makeFetchUserInfoJobFactory(QNetworkAccessManager *nam) = 0;
};

class OAuth2AuthenticationStrategy : public AbstractAuthenticationStrategy
{
public:
    explicit OAuth2AuthenticationStrategy(const QString &token, const QString &refreshToken);

    HttpCredentialsGui *makeCreds() override;

    bool isValid() override;

    FetchUserInfoJobFactory makeFetchUserInfoJobFactory(QNetworkAccessManager *nam) override;

private:
    QString _token;
    QString _refreshToken;
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

    void setAuthenticationStrategy(AbstractAuthenticationStrategy *strategy);
    AbstractAuthenticationStrategy *authenticationStrategy() const;

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
     * Set dynamic registration data. Used by OIDC servers to identify dynamically registered clients.
     */
    void setDynamicRegistrationData(const QVariantMap &dynamicRegistrationData);
    QVariantMap dynamicRegistrationData() const;

    /**
     * Attempt to build an account from the previously entered information.
     * @return built account or null if information is still missing
     */
    AccountPtr build();

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

    std::unique_ptr<AbstractAuthenticationStrategy> _authenticationStrategy;

    QVariantMap _dynamicRegistrationData;

    QString _displayName;

    QSet<QSslCertificate> _customTrustedCaCertificates;

    QString _defaultSyncTargetDir;
};
}
