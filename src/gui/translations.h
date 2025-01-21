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

#include "gui/opencloudguilib.h"

#include <QSet>
#include <QString>

namespace OCC {

namespace Translations {

    /**
    * @return translation files' filename prefix
    */
    OPENCLOUD_GUI_EXPORT const QString translationsFilePrefix();

    /**
    * @returntranslation files' filename suffix
    */
    OPENCLOUD_GUI_EXPORT const QString translationsFileSuffix();

    /**
     * @return path to translation files
     */
    OPENCLOUD_GUI_EXPORT QString translationsDirectoryPath();

    /**
     * @return list of locales for which translations are available
     */
    OPENCLOUD_GUI_EXPORT QSet<QString> listAvailableTranslations();

} // namespace Translations

} // namespace OCC
