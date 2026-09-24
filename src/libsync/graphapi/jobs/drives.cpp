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

#include "drives.h"

#include "account.h"
#include "collection_of_driveitems.h"
#include "collection_of_drives.h"

#include "client/collection_of_drives.h"


using namespace OCC;
using namespace GraphApi;

namespace {

const auto mountpointC = QLatin1String("mountpoint");
}

Drives::Drives(const AccountPtr &account, QObject *parent)
    : JsonJob(account, account->url(), QStringLiteral("/graph/v1.0/me/drives"), "GET", {}, parent)
{
}

Drives::~Drives() { }

const QList<QtOpenAPI::Drive> &Drives::drives() const
{
    if (_drives.isEmpty() && parseError().error == QJsonParseError::NoError) {
        QtOpenAPI::Collection_of_drives drives;
        drives.fromJsonObject(data());
        _drives = drives.getValueValue();
        // At the moment we don't support mountpoints but use the Share Jail
        _drives.erase(
            std::remove_if(_drives.begin(), _drives.end(), [](const QtOpenAPI::Drive &it) { return it.getDriveTypeValue() == mountpointC; }), _drives.end());
    }
    return _drives;
}
