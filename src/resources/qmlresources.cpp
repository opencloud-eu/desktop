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

#include "resources/qmlresources.h"

#include "resources/resources.h"

namespace {
constexpr QSize minIconSize{16, 16};
}

using namespace OCC;

Resources::Icon Resources::parseIcon(const QString &id)
{
    const auto data = QUrlQuery(id);

    return {data.queryItemValue(QLatin1String("theme")), QUrl::fromPercentEncoding(data.queryItemValue(QLatin1String("icon")).toUtf8()),
        QVariant(data.queryItemValue(QLatin1String("size"))).value<FontIcon::Size>(), QVariant(data.queryItemValue(QLatin1String("enabled"))).toBool()};
}

QPixmap Resources::pixmap(const QSize &requestedSize, const QIcon &icon, QIcon::Mode mode, QSize *outSize)
{
    Q_ASSERT(requestedSize.isValid());
    QSize actualSize = requestedSize.isValid() ? requestedSize : icon.availableSizes().constFirst();
    if (outSize) {
        *outSize = actualSize;
    }
    return icon.pixmap(actualSize.expandedTo(minIconSize), mode);
}
