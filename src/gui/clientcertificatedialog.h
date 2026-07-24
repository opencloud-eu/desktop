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

#include "account.h"
#include "libsync/clientcertificateutils.h"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace OCC {

class ClientCertificateDialog : public QWidget
{
    Q_OBJECT

public:
    explicit ClientCertificateDialog(const AccountPtr &account, QWidget *parent = nullptr);

    /**
     * Prompt the user for a PKCS#12 (.p12/.pfx) file and its password, then import it.
     *
     * Shows a file picker followed by a password prompt. On success fills @p result and
     * returns true. On a failed import a warning box is shown and false is returned.
     * Returns false without showing a warning if the user cancels either prompt.
     *
     * This is account-independent so it can be reused from the setup wizard, where no
     * AccountPtr exists yet.
     */
    static bool promptImportCertificate(QWidget *parent, ClientCertificateUtils::Pkcs12Result *result);

private Q_SLOTS:
    void slotImportCertificate();
    void slotRemoveCertificate();

private:
    void updateCertificateDisplay();

    AccountPtr _account;
    QLabel *_statusLabel;
    QLabel *_detailsLabel;
    QPushButton *_importButton;
    QPushButton *_removeButton;
};

}
