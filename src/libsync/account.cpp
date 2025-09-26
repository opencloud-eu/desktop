/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "account.h"
#include "accessmanager.h"
#include "capabilities.h"
#include "common/asserts.h"
#include "cookiejar.h"
#include "creds/abstractcredentials.h"
#include "creds/credentialmanager.h"
#include "graphapi/spacesmanager.h"
#include "networkjobs.h"
#include "networkjobs/resources.h"
#include "theme.h"

#include <QAuthenticator>
#include <QDir>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkDiskCache>
#include <QSslKey>
#include <QStandardPaths>

namespace OCC {

Q_LOGGING_CATEGORY(lcAccount, "sync.account", QtInfoMsg)

QString Account::_customCommonCacheDirectory = {};

void Account::setCommonCacheDirectory(const QString &directory)
{
    _customCommonCacheDirectory = directory;
}

QString Account::commonCacheDirectory()
{
    if (_customCommonCacheDirectory.isEmpty()) {
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }

    return _customCommonCacheDirectory;
}

Account::Account(const QUuid &uuid, QObject *parent)
    : QObject(parent)
    , _uuid(uuid)
    , _capabilities({}, {})
    , _jobQueue(this)
    , _queueGuard(&_jobQueue)
    , _credentialManager(new CredentialManager(this))
    , _spacesManager(new GraphApi::SpacesManager(this))
{
    qRegisterMetaType<AccountPtr>("AccountPtr");

    _cacheDirectory = QStringLiteral("%1/accounts/%2").arg(commonCacheDirectory(), _uuid.toString(QUuid::WithoutBraces));
    QDir().mkpath(_cacheDirectory);

    // we need to make sure the directory we pass to the resources cache exists
    const QString resourcesCacheDir = QStringLiteral("%1/resources/").arg(_cacheDirectory);
    QDir().mkpath(resourcesCacheDir);
    _resourcesCache = new ResourcesCache(resourcesCacheDir, this);
}

AccountPtr Account::create(const QUuid &uuid)
{
    AccountPtr acc = AccountPtr(new Account(uuid));
    acc->setSharedThis(acc);
    return acc;
}

Account::~Account()
{
}

void Account::setSharedThis(AccountPtr sharedThis)
{
    _sharedThis = sharedThis.toWeakRef();
}

CredentialManager *Account::credentialManager() const
{
    return _credentialManager;
}

GraphApi::SpacesManager *Account::spacesManager() const
{
    return _spacesManager;
}

QUuid Account::uuid() const
{
    return _uuid;
}

AccountPtr Account::sharedFromThis()
{
    return _sharedThis.toStrongRef();
}

QIcon Account::avatar() const
{
    return _avatarImg;
}

void Account::setAvatar(const QIcon &img)
{
    _avatarImg = img;
    Q_EMIT avatarChanged();
}

bool Account::hasAvatar() const
{
    return !_avatarImg.isNull();
}

QString Account::displayNameWithHost() const
{
    QString user = davDisplayName();
    QString host = _url.host();
    const int port = url().port();
    if (port > 0 && port != 80 && port != 443) {
        host += QStringLiteral(":%1").arg(QString::number(port));
    }
    return tr("%1@%2").arg(user, host);
}

QString Account::initials() const
{
    QString out;
    for (const auto &p : davDisplayName().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out.append(p.first(1));
    }
    return out;
}

QGradient::Preset Account::avatarGradient() const
{
    return static_cast<QGradient::Preset>(qHash(displayNameWithHost()) % QGradient::NumPresets + 1);
}

QString Account::davDisplayName() const
{
    return _displayName;
}

void Account::setDavDisplayName(const QString &newDisplayName)
{
    if (_displayName != newDisplayName) {
        _displayName = newDisplayName;
        Q_EMIT displayNameChanged();
    }
}

AbstractCredentials *Account::credentials() const
{
    return _credentials.data();
}

void Account::setCredentials(AbstractCredentials *cred)
{
    // set active credential manager
    QNetworkCookieJar *jar = nullptr;
    if (_am) {
        jar = _am->cookieJar();
        jar->setParent(nullptr);
        _am->deleteLater();
    }

    // The order for these two is important! Reading the credential's
    // settings accesses the account as well as account->_credentials,
    _credentials.reset(cred);
    cred->setAccount(this);

    _am = _credentials->createAM();

    // the network access manager takes ownership when setCache is called, so we have to reinitialize it every time we reset the manager
    _networkCache = new QNetworkDiskCache(this);
    const QString networkCacheLocation = (QStringLiteral("%1/network/").arg(_cacheDirectory));
    qCDebug(lcAccount) << u"Cache location for account" << this << u"set to" << networkCacheLocation;
    _networkCache->setCacheDirectory(networkCacheLocation);
    _am->setCache(_networkCache);

    if (jar) {
        _am->setCookieJar(jar);
    }
    connect(_credentials.data(), &AbstractCredentials::fetched, this, [this] {
        Q_EMIT credentialsFetched();
        _queueGuard.unblock();
    });
    connect(_credentials.data(), &AbstractCredentials::authenticationStarted, this, [this] {
        _queueGuard.block();
    });
    connect(_credentials.data(), &AbstractCredentials::authenticationFailed, this, [this] { _queueGuard.clear(); });
}

/**
 * clear all cookies. (Session cookies or not)
 */
void Account::clearCookieJar()
{
    qCInfo(lcAccount) << u"Clearing cookies";
    _am->cookieJar()->deleteLater();
    _am->setCookieJar(new CookieJar);
}

AccessManager *Account::accessManager()
{
    return _am.data();
}

QNetworkReply *Account::sendRawRequest(const QByteArray &verb, const QUrl &url, QNetworkRequest req, QIODevice *data)
{
    Q_ASSERT(verb.isUpper());
    req.setUrl(url);
    if (verb == "HEAD" && !data) {
        return _am->head(req);
    } else if (verb == "GET" && !data) {
        return _am->get(req);
    } else if (verb == "POST") {
        return _am->post(req, data);
    } else if (verb == "PUT") {
        return _am->put(req, data);
    } else if (verb == "DELETE" && !data) {
        return _am->deleteResource(req);
    }
    return _am->sendCustomRequest(req, verb, data);
}

void Account::setApprovedCerts(const QList<QSslCertificate> &certs)
{
    _approvedCerts = { certs.begin(), certs.end() };
    _am->setCustomTrustedCaCertificates(_approvedCerts);
}

void Account::addApprovedCerts(const QSet<QSslCertificate> &certs)
{
    _approvedCerts.unite(certs);
    _am->setCustomTrustedCaCertificates(_approvedCerts);
    Q_EMIT wantsAccountSaved(this);
}

void Account::setUrl(const QUrl &url)
{
    if (_url != url) {
        _url = url;
        Q_EMIT urlChanged();
    }
}

QUrl Account::url() const
{
    return _url;
}

QString Account::hostName() const
{
    return _url.host();
}

JobQueue *Account::jobQueue()
{
    return &_jobQueue;
}

void Account::clearAMCache()
{
    _am->clearAccessCache();
}

const Capabilities &Account::capabilities() const
{
    Q_ASSERT(hasCapabilities());
    return _capabilities;
}

bool Account::hasCapabilities() const
{
    return _capabilities.isValid();
}

void Account::setCapabilities(const Capabilities &caps)
{
    if (_capabilities != caps) {
        Q_EMIT capabilitiesChanged();
        const bool versionChanged =
            caps.status().legacyVersion != _capabilities.status().legacyVersion || caps.status().productversion != _capabilities.status().productversion;
        _capabilities = caps;
        if (versionChanged) {
            Q_EMIT serverVersionChanged();
        }
    }
}

Account::ServerSupportLevel Account::serverSupportLevel() const
{
    if (!hasCapabilities()) {
        // not detected yet, assume it is fine.
        return ServerSupportLevel::Supported;
    }

    // OpenCloud
    if (!capabilities().status().productversion.isEmpty()) {
        return ServerSupportLevel::Supported;
    }
    return ServerSupportLevel::Unsupported;
}

bool Account::isHttp2Supported() const
{
    return _http2Supported;
}

void Account::setHttp2Supported(bool value)
{
    _http2Supported = value;
}

QString Account::defaultSyncRoot() const
{
    Q_ASSERT(!_defaultSyncRoot.isEmpty());
    return _defaultSyncRoot;
}
bool Account::hasDefaultSyncRoot() const
{
    return !_defaultSyncRoot.isEmpty();
}

void Account::setDefaultSyncRoot(const QString &syncRoot)
{
    Q_ASSERT(_defaultSyncRoot.isEmpty());
    if (!syncRoot.isEmpty()) {
        _defaultSyncRoot = syncRoot;
    }
}

void Account::setAppProvider(AppProvider &&p)
{
    _appProvider = std::move(p);
}

const AppProvider &Account::appProvider() const
{
    return _appProvider;
}

void Account::invalidCredentialsEncountered()
{
    Q_EMIT invalidCredentials(Account::QPrivateSignal());
}

ResourcesCache *Account::resourcesCache() const
{
    return _resourcesCache;
}

} // namespace OCC


QDebug operator<<(QDebug debug, const OCC::Account *acc)
{
    QDebugStateSaver saver(debug);
    debug.setAutoInsertSpaces(false);
    debug << u"OCC::Account(" << acc->displayNameWithHost() << u")";
    return debug.maybeSpace();
}
