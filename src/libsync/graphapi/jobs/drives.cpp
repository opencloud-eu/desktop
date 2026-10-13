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

#include <OAICollection_of_drives.h>
#include <OAIDrive.h>

using namespace Qt::Literals::StringLiterals;

using namespace OCC;
using namespace GraphApi;

namespace {
constexpr auto mountpointC = "mountpoint"_L1;
constexpr auto vaultContentTypeC = "application/vnd.opencloud.vault"_L1;
}

Drives::Drives(const AccountPtr &account, QObject *parent)
    : JsonJob(account, account->url(), u"/graph/v1.0/me/drives"_s, "GET"_ba, {}, parent)
{
}

Drives::~Drives() { }

const QList<OpenAPI::OAIDrive> &Drives::drives() const
{
    if (_drives.isEmpty() && parseError().error == QJsonParseError::NoError) {
        OpenAPI::OAICollection_of_drives drives;
        drives.fromJsonObject(data());
        _drives = drives.getValue();
        // At the moment we don't support mountpoints but use the Share Jail
        _drives.erase(std::remove_if(_drives.begin(), _drives.end(),
                          [](const OpenAPI::OAIDrive &it) { return it.getDriveType() == mountpointC || it.getLibreGraphContentType() == vaultContentTypeC; }),
            _drives.end());
    }
    return _drives;
}
