/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#include "opencloudtheme.h"

#include <QCoreApplication>
#include <QIcon>
#include <QString>
#include <QVariant>

namespace OCC {

OpenCloudTheme::OpenCloudTheme()
    : Theme()
{
}

QColor OpenCloudTheme::wizardHeaderBackgroundColor() const
{
    return QColor("#20434F");
}

QColor OpenCloudTheme::wizardHeaderTitleColor() const
{
    return Qt::white;
}

QIcon OpenCloudTheme::wizardHeaderLogo() const
{
    return Resources::themeUniversalIcon(QStringLiteral("wizard_logo"));
}
QIcon OpenCloudTheme::aboutIcon() const
{
    return Resources::themeUniversalIcon(QStringLiteral("opencloud-icon"));
}

QmlButtonColor OpenCloudTheme::primaryButtonColor() const
{
    return {"#E2BAFF", "#20434F", "#DADADA"};
}

QmlButtonColor OpenCloudTheme::secondaryButtonColor() const
{
    return {"#CA8DF5", "#19353F", "#B0B0B0"};
}
}
