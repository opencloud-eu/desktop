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

#ifndef CLIENTPROXY_H
#define CLIENTPROXY_H

#include <QObject>
#include <QNetworkProxy>
#include <QRunnable>
#include <QUrl>

#include "common/utility.h"
#include "libsync/csync.h"

namespace OCC {

class ConfigFile;

/**
 * @brief The ClientProxy class
 * @ingroup libsync
 */
namespace ClientProxy {
    bool isUsingSystemDefault();
    void setupQtProxyFromConfig(const QString &password);
};
}

#endif // CLIENTPROXY_H
