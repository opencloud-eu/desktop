/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#include "gui/opencloudguilib.h"

#include "common/chronoelapsedtimer.h"
#include "gui/guiutility.h"
#include "libsync/accountfwd.h"

#include <QNetworkReply>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <chrono>

namespace OCC {

class OPENCLOUD_GUI_EXPORT ConnectionValidator : public QObject
{
    Q_OBJECT
public:
    explicit ConnectionValidator(AccountPtr account, QObject *parent = nullptr);

    enum class ValidationMode { ValidateServer, ValidateAuthAndUpdate };
    Q_ENUM(ValidationMode)

    enum Status {
        Undefined,
        Connected,
        NotConfigured,
        CredentialsNotReady, // Credentials aren't ready
        CredentialsWrong, // AuthenticationRequiredError
        SslError, // SSL handshake error, certificate rejected by user?
        StatusNotFound, // Error retrieving status.php
        ServiceUnavailable, // 503 on authed request
        MaintenanceMode, // maintenance enabled in status.php
        Timeout, // actually also used for other errors on the authed request
        CaptivePortal, // We're stuck behind a captive portal and (will) get SSL certificate problems
    };
    Q_ENUM(Status)

    // How often should the Application ask this object to check for the connection?
    static constexpr auto DefaultCallingInterval = std::chrono::seconds(62);


    /** Whether to clear the cookies before we start the CheckServerJob job
     * This option also depends on Theme::instance()->connectionValidatorClearCookies()
     */
    void setClearCookies(bool clearCookies);

public Q_SLOTS:
    /// Checks the server and the authentication.
    void checkServer(ConnectionValidator::ValidationMode mode = ConnectionValidator::ValidationMode::ValidateAuthAndUpdate);

    void systemProxyLookupDone(const QNetworkProxy &proxy);

Q_SIGNALS:
    void connectionResult(ConnectionValidator::Status status, const QStringList &errors);

    void sslErrors(const QList<QSslError> &errors);

protected Q_SLOTS:
    void slotCheckServerAndAuth();

    void slotStatusFound(const QUrl &url, const QJsonObject &info);

private:
    void reportResult(Status status);

    QStringList _errors;
    AccountPtr _account;
    bool _clearCookies = false;

    Utility::ChronoElapsedTimer _duration;
    bool _finished = false;

    ConnectionValidator::ValidationMode _mode = ConnectionValidator::ValidationMode::ValidateAuthAndUpdate;
};
}
