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

#include <QUrl>

namespace OCC {
namespace Resources {
    namespace Template {

        /**
         * Replace all occurances of @{} values in template with the values from values
         */
        OPENCLOUD_RESOURCES_EXPORT QString renderTemplateFromFile(const QString &filePath, const QMap<QAnyStringView, QAnyStringView> &values);
        OPENCLOUD_RESOURCES_EXPORT QString renderTemplate(QString &&templ, const QMap<QAnyStringView, QAnyStringView> &values);
    };
}
}
