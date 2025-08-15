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
#include "commonstrings.h"

#include <QCoreApplication>

using namespace OCC;

QString CommonStrings::fileBrowser()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Explorer");
#elif defined(Q_OS_MAC)
    return QStringLiteral("Finder");
#else
    return tr("file manager");
#endif
}

QString CommonStrings::showInFileBrowser(const QString &path)
{
    if (path.isEmpty()) {
        return tr("Show in %1").arg(fileBrowser());
    }
    return tr("Show »%1« in %2").arg(path, fileBrowser());
}

QString CommonStrings::showInWebBrowser()
{
    return tr("Show in web browser");
}

QString CommonStrings::copyToClipBoard()
{
    return tr("Copy");
}

QString CommonStrings::filterButtonText(int filterCount)
{
    return tr("%n Filter(s)", nullptr, filterCount);
}
