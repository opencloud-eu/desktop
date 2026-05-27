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

#include "clientcertificatedialog.h"

#include "libsync/clientcertificateutils.h"

#include <QFileDialog>
#include <QLocale>
#include <QGroupBox>
#include <QInputDialog>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QVBoxLayout>

namespace OCC {

Q_LOGGING_CATEGORY(lcClientCertDialog, "gui.clientcertdialog", QtInfoMsg)

ClientCertificateDialog::ClientCertificateDialog(const AccountPtr &account, QWidget *parent)
    : QWidget(parent)
    , _account(account)
{
    setWindowTitle(tr("Client Certificate (mTLS)"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);

    auto *statusGroup = new QGroupBox(tr("Certificate Status"), this);
    auto *statusLayout = new QVBoxLayout(statusGroup);

    _statusLabel = new QLabel(this);
    _statusLabel->setWordWrap(true);
    statusLayout->addWidget(_statusLabel);

    _detailsLabel = new QLabel(this);
    _detailsLabel->setWordWrap(true);
    _detailsLabel->setTextFormat(Qt::RichText);
    statusLayout->addWidget(_detailsLabel);

    layout->addWidget(statusGroup);

    auto *buttonLayout = new QHBoxLayout;
    _importButton = new QPushButton(tr("Import Certificate..."), this);
    _removeButton = new QPushButton(tr("Remove Certificate"), this);

    auto *closeButton = new QPushButton(tr("Close"), this);

    buttonLayout->addWidget(_importButton);
    buttonLayout->addWidget(_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);

    layout->addLayout(buttonLayout);
    layout->addStretch();

    connect(_importButton, &QPushButton::clicked, this, &ClientCertificateDialog::slotImportCertificate);
    connect(_removeButton, &QPushButton::clicked, this, &ClientCertificateDialog::slotRemoveCertificate);
    connect(closeButton, &QPushButton::clicked, this, &ClientCertificateDialog::close);

    updateCertificateDisplay();
}

void ClientCertificateDialog::slotImportCertificate()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Select PKCS#12 Certificate"),
        QString(),
        tr("PKCS#12 Files (*.p12 *.pfx);;All Files (*)"));

    if (filePath.isEmpty()) {
        return;
    }

    bool ok = false;
    const QString password = QInputDialog::getText(
        this,
        tr("Certificate Password"),
        tr("Enter the password for the certificate file:"),
        QLineEdit::Password,
        QString(),
        &ok);

    if (!ok) {
        return;
    }

    ClientCertificateUtils::Pkcs12Result result;
    if (!ClientCertificateUtils::importPkcs12(filePath, password, &result)) {
        QMessageBox::warning(this, tr("Import Failed"),
            tr("Failed to import the certificate file.\n\n"
               "Please check that the file is a valid PKCS#12 (.p12/.pfx) file "
               "and that the password is correct."));
        return;
    }

    _account->setClientCertificate(result.certificate, result.privateKey);

    if (!result.caCertificates.isEmpty()) {
        _account->addApprovedCerts({result.caCertificates.begin(), result.caCertificates.end()});
    }

    qCInfo(lcClientCertDialog) << "Client certificate imported successfully:" << result.certificate.subjectDisplayName();

    updateCertificateDisplay();
}

void ClientCertificateDialog::slotRemoveCertificate()
{
    _account->clearClientCertificate();
    qCInfo(lcClientCertDialog) << "Client certificate removed";
    updateCertificateDisplay();
}

void ClientCertificateDialog::updateCertificateDisplay()
{
    if (_account->hasClientCertificate()) {
        const auto &cert = _account->clientCertificate();
        _statusLabel->setText(tr("A client certificate is configured for mTLS authentication."));

        const QString details = QStringLiteral(
            "<table>"
            "<tr><td><b>%1</b></td><td>%2</td></tr>"
            "<tr><td><b>%3</b></td><td>%4</td></tr>"
            "<tr><td><b>%5</b></td><td>%6</td></tr>"
            "<tr><td><b>%7</b></td><td>%8</td></tr>"
            "<tr><td><b>%9</b></td><td>%10</td></tr>"
            "</table>")
            .arg(
                tr("Subject:"), cert.subjectDisplayName(),
                tr("Issuer:"), cert.issuerDisplayName(),
                tr("Valid from:"), QLocale().toString(cert.effectiveDate(), QLocale::LongFormat),
                tr("Expires:"), QLocale().toString(cert.expiryDate(), QLocale::LongFormat),
                tr("Fingerprint (SHA-256):"), QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex(':')));

        _detailsLabel->setText(details);
        _detailsLabel->setVisible(true);
        _removeButton->setEnabled(true);
    } else {
        _statusLabel->setText(tr("No client certificate configured."));
        _detailsLabel->setVisible(false);
        _removeButton->setEnabled(false);
    }
}

}
