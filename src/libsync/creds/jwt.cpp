/*
 * Copyright (C) by Hannah von Reth <hvonreth@opencloud.eu>
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
#include "jwt.h"


OCC::JWT::JWT(const QByteArray &jwt)
{
    auto parts = jwt.split('.');
    if (parts.size() != 3) {
        return;
    }
    auto parse = [](const QByteArray &part) { return QJsonDocument::fromJson(QByteArray::fromBase64(part)).object(); };
    _header = parse(parts[0]);
    _payload = parse(parts[1]);
    _signauture = parts[2];
}

QByteArray OCC::JWT::serialize() const
{
    if (!isValid()) {
        return {};
    }
    auto encode = [](const QJsonObject &part) {
        return QJsonDocument(part).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    };
    return encode(_header) + '.' + encode(_payload) + '.' + _signauture;
}

bool OCC::JWT::isValid() const
{
    return !_header.isEmpty() && !_payload.isEmpty() && !_signauture.isEmpty();
}

QJsonObject OCC::JWT::header() const
{
    return _header;
}

QJsonObject OCC::JWT::payload() const
{
    return _payload;
}
