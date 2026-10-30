/*
 * Copyright (C) by OpenCloud GmbH
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

#include "clientcertificateutils.h"

#include <QBuffer>
#include <QFile>
#include <QLoggingCategory>

namespace OCC::ClientCertificateUtils {

Q_LOGGING_CATEGORY(lcClientCert, "sync.clientcert", QtInfoMsg)

bool importPkcs12(const QString &filePath, const QString &password, Pkcs12Result *result)
{
    Q_ASSERT(result);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcClientCert) << "Failed to open PKCS#12 file:" << filePath;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QSslCertificate cert;
    QSslKey key;
    QList<QSslCertificate> caCerts;

    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    if (!QSslCertificate::importPkcs12(&buffer, &key, &cert, &caCerts, password.toUtf8())) {
        qCWarning(lcClientCert) << "Failed to import PKCS#12 file (wrong password or invalid format):" << filePath;
        return false;
    }

    if (cert.isNull() || key.isNull()) {
        qCWarning(lcClientCert) << "PKCS#12 file did not contain a valid certificate or key:" << filePath;
        return false;
    }

    qCInfo(lcClientCert) << "Successfully imported client certificate:"
                         << cert.subjectDisplayName()
                         << "issued by" << cert.issuerDisplayName()
                         << "expires" << cert.expiryDate().toString(Qt::ISODate);

    result->certificate = cert;
    result->privateKey = key;
    result->caCertificates = caCerts;

    return true;
}

}
