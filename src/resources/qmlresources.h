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
#include "resources/opencloudresourceslib.h"

#include "resources/fonticon.h"

#include <QIcon>
#include <QtQml/QtQml>

namespace OCC {
namespace Resources {
    struct Icon
    {
        QString theme;
        QString iconName;
        FontIcon::Size size;
        bool enabled;
    };

    Icon OPENCLOUD_RESOURCES_EXPORT parseIcon(const QString &id);

    QPixmap OPENCLOUD_RESOURCES_EXPORT pixmap(const QSize &requestedSize, const QIcon &icon, QIcon::Mode mode, QSize *outSize);
} // Resources
} // OCC
