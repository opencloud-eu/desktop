/*
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

#ifndef MIRALL_ACCESS_MANAGER_H
#define MIRALL_ACCESS_MANAGER_H

#include "opencloudsynclib.h"
#include <QNetworkAccessManager>
#include <QSslCertificate>
#include <QSslKey>

class QByteArray;
class QUrl;

namespace OCC {
class CookieJar;

/**
 * @brief The AccessManager class
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT AccessManager : public QNetworkAccessManager
{
    Q_OBJECT

public:
    static QByteArray generateRequestId();

    AccessManager(QObject *parent = nullptr);

    QSet<QSslCertificate> customTrustedCaCertificates();

    /***
     * Warning calling those will break running network jobs
     */
    void setCustomTrustedCaCertificates(const QSet<QSslCertificate> &certificates);
    /***
     * Warning calling those will break running network jobs
     */
    void addCustomTrustedCaCertificates(const QList<QSslCertificate> &certificates);

    /***
     * Set the client certificate and private key for mTLS authentication.
     * Warning calling this will break running network jobs.
     */
    void setClientCertificate(const QSslCertificate &cert, const QSslKey &key);

    CookieJar *openCloudCookieJar() const;

    /***
     * Remove all errors for already accepted certificates
     */
    QList<QSslError> filterSslErrors(const QList<QSslError> &errors) const;

protected:
    QNetworkReply *createRequest(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *outgoingData = nullptr) override;

private:
    QSet<QSslCertificate> _customTrustedCaCertificates;
    QSslCertificate _clientCertificate;
    QSslKey _clientPrivateKey;
};

} // namespace OCC

#endif
