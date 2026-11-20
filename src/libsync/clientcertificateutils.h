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

#pragma once

#include "opencloudsynclib.h"

#include <QList>
#include <QSslCertificate>
#include <QSslKey>
#include <QString>

namespace OCC::ClientCertificateUtils {

struct Pkcs12Result
{
    QSslCertificate certificate;
    QSslKey privateKey;
    QList<QSslCertificate> caCertificates;
};

/**
 * Import a PKCS#12 (.p12/.pfx) file.
 *
 * @param filePath Path to the PKCS#12 file
 * @param password Password to decrypt the file
 * @param result Output struct containing the certificate, key, and CA certs
 * @return true on success, false on failure
 */
OPENCLOUD_SYNC_EXPORT bool importPkcs12(const QString &filePath, const QString &password, Pkcs12Result *result);

}
