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

#include "setupwizardaccountbuilder.h"

#include "gui/guiutility.h"
#include "networkjobs/fetchuserinfojobfactory.h"

#include <QDir>
#include <QFileInfo>

namespace OCC::Wizard {

OAuth2AuthenticationStrategy::OAuth2AuthenticationStrategy(
    const QString &token, const QString &refreshToken, const QVariantMap &dynamicRegistrationData, const IdToken &idToken)
    : _token(token)
    , _refreshToken(refreshToken)
    , _dynamicRegistrationData(dynamicRegistrationData)
    , _idToken(idToken)
{
}

HttpCredentialsGui *OAuth2AuthenticationStrategy::makeCreds()
{
    Q_ASSERT(isValid());
    return new HttpCredentialsGui(_token, _refreshToken);
}

bool OAuth2AuthenticationStrategy::isValid()
{
    return !_token.isEmpty() && !_refreshToken.isEmpty();
}

FetchUserInfoJobFactory OAuth2AuthenticationStrategy::makeFetchUserInfoJobFactory(QNetworkAccessManager *nam)
{
    return FetchUserInfoJobFactory::fromOAuth2Credentials(nam, _token);
}

const QVariantMap &OAuth2AuthenticationStrategy::dynamicRegistrationData() const
{
    return _dynamicRegistrationData;
}

const IdToken &OAuth2AuthenticationStrategy::idToken() const
{
    return _idToken;
}

SetupWizardAccountBuilder::SetupWizardAccountBuilder() = default;

void SetupWizardAccountBuilder::setServerUrl(const QUrl &serverUrl)
{
    _serverUrl = serverUrl;

    // to not keep credentials longer than necessary, we purge them whenever the URL is set
    // for this reason, we also don't insert already-known credentials on the credentials pages when switching to them
    _authenticationStrategy.reset();
}

QUrl SetupWizardAccountBuilder::serverUrl() const
{
    return _serverUrl;
}

AccountPtr SetupWizardAccountBuilder::build() const
{
    auto newAccountPtr = Account::create(QUuid::createUuid());

    Q_ASSERT(!_serverUrl.isEmpty() && _serverUrl.isValid());
    newAccountPtr->setUrl(_serverUrl);

    if (!_webFingerSelectedInstance.isEmpty()) {
        Q_ASSERT(_serverUrl.isValid());
        newAccountPtr->setUrl(_webFingerSelectedInstance);
    }

    Q_ASSERT(hasValidCredentials());

    // TODO: perhaps _authenticationStrategy->setUpAccountPtr(...) would be more elegant? no need for getters then
    newAccountPtr->setCredentials(_authenticationStrategy->makeCreds());
    newAccountPtr->credentials()->persist();
    OAuth::persist(newAccountPtr, _authenticationStrategy->dynamicRegistrationData(), _authenticationStrategy->idToken());

    newAccountPtr->setDavDisplayName(_displayName);

    newAccountPtr->addApprovedCerts({ _customTrustedCaCertificates.begin(), _customTrustedCaCertificates.end() });

    if (!_defaultSyncTargetDir.isEmpty()) {
        newAccountPtr->setDefaultSyncRoot(_defaultSyncTargetDir);
        if (!QFileInfo::exists(_defaultSyncTargetDir)) {
            OC_ASSERT(QDir().mkpath(_defaultSyncTargetDir));
        }
        Utility::markDirectoryAsSyncRoot(_defaultSyncTargetDir, newAccountPtr->uuid());
    }

    return newAccountPtr;
}

bool SetupWizardAccountBuilder::hasValidCredentials() const
{
    if (_authenticationStrategy == nullptr) {
        return false;
    }

    return _authenticationStrategy->isValid();
}

QString SetupWizardAccountBuilder::displayName() const
{
    return _displayName;
}

void SetupWizardAccountBuilder::setDisplayName(const QString &displayName)
{
    _displayName = displayName;
}

void SetupWizardAccountBuilder::setAuthenticationStrategy(std::unique_ptr<OAuth2AuthenticationStrategy> &&strategy)
{
    _authenticationStrategy = std::move(strategy);
}

void SetupWizardAccountBuilder::addCustomTrustedCaCertificate(const QSslCertificate &customTrustedCaCertificate)
{
    _customTrustedCaCertificates.insert(customTrustedCaCertificate);
}

void SetupWizardAccountBuilder::clearCustomTrustedCaCertificates()
{
    _customTrustedCaCertificates.clear();
}

OAuth2AuthenticationStrategy *SetupWizardAccountBuilder::authenticationStrategy() const
{
    return _authenticationStrategy.get();
}

void SetupWizardAccountBuilder::setSyncTargetDir(const QString &syncTargetDir)
{
    _defaultSyncTargetDir = syncTargetDir;
}

QString SetupWizardAccountBuilder::syncTargetDir() const
{
    return _defaultSyncTargetDir;
}

void SetupWizardAccountBuilder::setWebFingerAuthenticationServerUrl(const QUrl &url)
{
    _webFingerAuthenticationServerUrl = url;
}

QUrl SetupWizardAccountBuilder::webFingerAuthenticationServerUrl() const
{
    return _webFingerAuthenticationServerUrl;
}

void SetupWizardAccountBuilder::setWebFingerInstances(const QVector<QUrl> &instancesList)
{
    _webFingerInstances = instancesList;
}

QVector<QUrl> SetupWizardAccountBuilder::webFingerInstances() const
{
    return _webFingerInstances;
}

void SetupWizardAccountBuilder::setWebFingerSelectedInstance(const QUrl &instance)
{
    _webFingerSelectedInstance = instance;
}

QUrl SetupWizardAccountBuilder::webFingerSelectedInstance() const
{
    return _webFingerSelectedInstance;
}
}
