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

#include "abstractsetupwizardpage.h"

#include <QList>
#include <QSslCertificate>
#include <QSslKey>

namespace Ui {
class ServerUrlSetupWizardPage;
}

namespace OCC::Wizard {
class ServerUrlSetupWizardPage : public AbstractSetupWizardPage
{
    Q_OBJECT

public:
    ServerUrlSetupWizardPage(const QUrl &serverUrl);

    QUrl userProvidedUrl() const;

    bool validateInput() const override;

    void keyPressEvent(QKeyEvent *event) override;

    // Optional client certificate for mTLS, selected on this page before the account exists.
    // Null/empty when the user has not configured one.
    QSslCertificate clientCertificate() const;
    QSslKey clientPrivateKey() const;
    QList<QSslCertificate> clientCaCertificates() const;

private:
    void slotConfigureClientCertificate();
    void updateClientCertStatus();

    ::Ui::ServerUrlSetupWizardPage *_ui;

    QSslCertificate _clientCertificate;
    QSslKey _clientPrivateKey;
    QList<QSslCertificate> _clientCaCertificates;

public:
    ~ServerUrlSetupWizardPage() noexcept override;
};
}
