/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
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

#include <QObject>
#include <QtQml/QQmlEngine>

namespace OCC {

class CommonStrings : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT
public:
    Q_INVOKABLE static QString fileBrowser();
    Q_INVOKABLE static QString showInFileBrowser(const QString &path = {});
    Q_INVOKABLE static QString showInWebBrowser();
    Q_INVOKABLE static QString copyToClipBoard();
    Q_INVOKABLE static QString filterButtonText(int filterCount);
};
}
