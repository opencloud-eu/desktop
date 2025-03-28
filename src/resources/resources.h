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

#include <QIcon>
#include <QUrl>
#include <QtQuick/QQuickImageProvider>

namespace OCC::Resources {
Q_NAMESPACE_EXPORT(OPENCLOUD_RESOURCES_EXPORT)
/**
 * Wehther we allow a fallback to a vanilla icon
 */
enum class IconType { BrandedIcon, BrandedIconWithFallbackToVanillaIcon, VanillaIcon };
Q_ENUM_NS(IconType);


/**
 *
 * @return Whether we are using the vanilla theme
 */
bool OPENCLOUD_RESOURCES_EXPORT isVanillaTheme();

/**
 * Whether use the dark icon theme
 * The function also ensures the theme supports the dark theme
 */
bool OPENCLOUD_RESOURCES_EXPORT isUsingDarkTheme();

QIcon OPENCLOUD_RESOURCES_EXPORT loadIcon(const QString &flavor, const QString &name, IconType iconType);

QColor OPENCLOUD_RESOURCES_EXPORT tint();

/**
 * Returns a universal (non color schema aware) icon.
 */
QIcon OPENCLOUD_RESOURCES_EXPORT themeUniversalIcon(const QString &name, IconType iconType = IconType::BrandedIcon);

class OPENCLOUD_RESOURCES_EXPORT CoreImageProvider : public QQuickImageProvider
{
    Q_OBJECT
public:
    CoreImageProvider();

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};


QUrl OPENCLOUD_RESOURCES_EXPORT iconToFileSystemUrl(const QIcon &icon, QAnyStringView type = QStringLiteral("png"));
}
