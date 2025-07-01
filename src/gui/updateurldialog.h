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

#pragma once

#include "account.h"

#include "gui/accountmodalwidget.h"

namespace OCC {

/**
 * Compares two given URLs.
 * In case they differ, it asks the user whether they want to accept the change.
 */
class UpdateUrlDialog : public QmlUtils::OCQuickWidget
{
    Q_OBJECT
    Q_PROPERTY(QUrl oldUrl MEMBER _oldUrl CONSTANT)
    Q_PROPERTY(QUrl newUrl MEMBER _newUrl CONSTANT)
    Q_PROPERTY(QString content MEMBER _content CONSTANT)
    Q_PROPERTY(bool requiresRestart MEMBER _requiresRestart NOTIFY requiresRestartChanged)
    QML_ELEMENT
    QML_UNCREATABLE("C++ only")
public:
    static UpdateUrlDialog *fromAccount(AccountPtr account, const QUrl &newUrl, QWidget *parent = nullptr);

    explicit UpdateUrlDialog(const QString &title, const QString &content, const QUrl &oldUrl, const QUrl &newUrl, QWidget *parent = nullptr);

Q_SIGNALS:
    void accepted();
    void rejected();

    void requiresRestartChanged();


private:
    QUrl _oldUrl;
    QUrl _newUrl;
    QString _content;
    bool _requiresRestart = false;
};

}
