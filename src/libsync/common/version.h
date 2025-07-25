/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#pragma once

#include "libsync/opencloudsynclib.h"

#include <QString>
#include <QVersionNumber>

namespace OCC::Version {
OPENCLOUD_SYNC_EXPORT const QVersionNumber &version();

OPENCLOUD_SYNC_EXPORT const QVersionNumber &versionWithBuildNumber();

inline int buildNumber()
{
    return versionWithBuildNumber().segmentAt(3);
}

/**
 * git, rc1, rc2
 * Empty in releases
 */
OPENCLOUD_SYNC_EXPORT QString suffix();

/**
 * The commit id
 */
OPENCLOUD_SYNC_EXPORT QString gitSha();

OPENCLOUD_SYNC_EXPORT QString displayString();
}
