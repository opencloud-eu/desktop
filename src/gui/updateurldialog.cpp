/*
* Copyright (C) by Fabian Müller <fmueller@owncloud.com>
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

#include "updateurldialog.h"


namespace OCC {

UpdateUrlDialog::UpdateUrlDialog(const QString &title, const QString &content, const QUrl &oldUrl, const QUrl &newUrl, QWidget *parent)
    : OCQuickWidget(parent)
    , _oldUrl(oldUrl)
    , _newUrl(newUrl)
    , _content(content)
{
    setWindowTitle(title);
    setOCContext(QUrl(QStringLiteral("qrc:/qt/qml/eu/OpenCloud/gui/qml/UpdateUrlDialog.qml")), parent, this, QJSEngine::CppOwnership);
}

UpdateUrlDialog *UpdateUrlDialog::fromAccount(AccountPtr account, const QUrl &newUrl, QWidget *parent)
{
    auto *dialog = new UpdateUrlDialog(tr("Url update requested for %1").arg(account->displayNameWithHost()),
        tr("The URL for <b>%1</b> changed from:<br><b>%2</b> to:<br> <b>%3</b>.<br><br>Do you want to accept the new URL permanently?<br>"
           "This will cause an application restart.")
            .arg(account->displayNameWithHost(), account->url().toString(), newUrl.toString()),
        account->url(), newUrl, parent);
    dialog->_requiresRestart = true;
    Q_EMIT dialog->requiresRestartChanged();
    return dialog;
}
}
