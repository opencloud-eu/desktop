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


#ifndef SERVERCONNECTION_H
#define SERVERCONNECTION_H

#include "common/utility.h"

#include "appprovider.h"
#include "capabilities.h"
#include "jobqueue.h"
#include "resources/resources.h"

#include <QByteArray>
#include <QGradient>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSharedPointer>
#include <QSslCertificate>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QUrl>
#include <QUuid>
#include <QtQmlIntegration/QtQmlIntegration>

class QSettings;
class QNetworkReply;
class QUrl;
class AccessManager;

namespace OCC {

class CredentialManager;
class AbstractCredentials;
class Account;
typedef QSharedPointer<Account> AccountPtr;
class AccessManager;
class SimpleNetworkJob;

namespace GraphApi {
    class SpacesManager;
}

class ResourcesCache;

class OPENCLOUD_SYNC_EXPORT Account : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUuid uid READ uuid CONSTANT)
    Q_PROPERTY(QString davDisplayName READ davDisplayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString displayNameWithHost READ displayNameWithHost NOTIFY displayNameChanged)
    Q_PROPERTY(QString initials READ initials NOTIFY displayNameChanged)
    Q_PROPERTY(QString hostName READ hostName NOTIFY urlChanged)
    Q_PROPERTY(bool hasAvatar READ hasAvatar NOTIFY avatarChanged)
    Q_PROPERTY(QGradient::Preset avatarGradient READ avatarGradient NOTIFY displayNameChanged)
    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    QML_ELEMENT
    QML_UNCREATABLE("Only created in the C++ code")

public:
    /**
     * Set a custom directory which all accounts created after this call will share to store their cached files in.
     */
    static void setCommonCacheDirectory(const QString &directory);
    static QString commonCacheDirectory();

    static AccountPtr create(const QUuid &uuid);
    ~Account() override;

    AccountPtr sharedFromThis();

    /***
     * The default folder containing all spaces.
     * This function will assert if the sync root is empty.
     */
    QString defaultSyncRoot() const;

    /***
     * Whether we have defaultSyncRoot defined.
     */
    bool hasDefaultSyncRoot() const;

    /***
     * Set defaultSyncRoot and creates the path on the filesystem.
     * Setting an empty string will have no effect.
     */
    void setDefaultSyncRoot(const QString &syncRoot);

    QString davDisplayName() const;
    void setDavDisplayName(const QString &newDisplayName);

    QIcon avatar() const;
    void setAvatar(const QIcon &img);
    bool hasAvatar() const;

    /// The name of the account as shown in the toolbar
    QString displayNameWithHost() const;
    QString initials() const;
    QGradient::Preset avatarGradient() const;

    /** Server url of the account */
    void setUrl(const QUrl &url);
    QUrl url() const;
    QString hostName() const;

    /** Holds the accounts credentials */
    AbstractCredentials *credentials() const;
    void setCredentials(AbstractCredentials *cred);

    /** Create a network request on the account's QNAM.
     *
     * Network requests in AbstractNetworkJobs are created through
     * this function. Other places should prefer to use jobs or
     * sendRequest().
     */
    QNetworkReply *sendRawRequest(const QByteArray &verb,
        const QUrl &url,
        QNetworkRequest req = QNetworkRequest(),
        QIODevice *data = nullptr);

    /** The certificates of the account */
    QSet<QSslCertificate> approvedCerts() const { return _approvedCerts; }

    /***
     * Warning calling those will break running network jobs on the current access manager
     */
    void setApprovedCerts(const QList<QSslCertificate> &certs);

    /***
     * Warning calling those will break running network jobs on the current access manager
     */
    void addApprovedCerts(const QSet<QSslCertificate> &certs);

    /** Access the server capabilities */
    const Capabilities &capabilities() const;
    void setCapabilities(const Capabilities &caps);

    bool hasCapabilities() const;

    void setAppProvider(AppProvider &&p);
    const AppProvider &appProvider() const;

    enum class ServerSupportLevel {
        Supported,
        Unknown,
        Unsupported
    };
    Q_ENUMS(ServerSupportLevel)
    ServerSupportLevel serverSupportLevel() const;

    /** True when the server connection is using HTTP2  */
    bool isHttp2Supported() const;
    void setHttp2Supported(bool value);

    void clearCookieJar();

    AccessManager *accessManager();

    JobQueue *jobQueue();

    QUuid uuid() const;

    CredentialManager *credentialManager() const;

    GraphApi::SpacesManager *spacesManager() const;

    /**
     * We encountered an authentication error.
     */
    void invalidCredentialsEncountered();

    ResourcesCache *resourcesCache() const;

public Q_SLOTS:
    /// Used when forgetting credentials
    void clearAMCache();

Q_SIGNALS:
    /// Triggered by invalidCredentialsEncountered()
    // this signal is emited when a network job failed due to invalid credentials
    void invalidCredentials(QPrivateSignal);

    void credentialsFetched();
    void credentialsAsked();

    // e.g. when the approved SSL certificates changed
    void wantsAccountSaved(Account *acc);

    void serverVersionChanged();
    void capabilitiesChanged();

    void avatarChanged();
    void displayNameChanged();

    void unknownConnectionState();

    void requestUrlUpdate(const QUrl &newUrl);

    // the signal exists on the Account object as the Approvider itself can change during runtime
    void appProviderErrorOccured(const QString &error);

    void urlChanged();

private:
    // directory all newly created accounts store their various caches in
    static QString _customCommonCacheDirectory;

    Account(const QUuid &uuid, QObject *parent = nullptr);
    void setSharedThis(AccountPtr sharedThis);

    QWeakPointer<Account> _sharedThis;
    QUuid _uuid;
    QString _displayName;
    QString _defaultSyncRoot;
    QIcon _avatarImg;
    QUrl _url;
    QString _cacheDirectory;

    QSet<QSslCertificate> _approvedCerts;
    Capabilities _capabilities;
    QPointer<AccessManager> _am;
    QPointer<QNetworkDiskCache> _networkCache = nullptr;
    QPointer<ResourcesCache> _resourcesCache;
    QScopedPointer<AbstractCredentials> _credentials;
    bool _http2Supported = true;

    JobQueue _jobQueue;
    JobQueueGuard _queueGuard;
    CredentialManager *_credentialManager;
    AppProvider _appProvider;

    GraphApi::SpacesManager *_spacesManager = nullptr;
    friend class AccountManager;
};
}

Q_DECLARE_METATYPE(OCC::AccountPtr)


QDebug OPENCLOUD_SYNC_EXPORT operator<<(QDebug debug, const OCC::Account *job);

#endif //SERVERCONNECTION_H
