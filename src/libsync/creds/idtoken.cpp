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

#include "idtoken.h"

#include <QJsonArray>

using namespace OCC;

IdToken::IdToken() { }

IdToken::IdToken(const QJsonObject &payload)
    : _payload(payload)
{
}

QVariantList IdToken::aud() const
{
    const auto aud = _payload.value(QLatin1String("aud"));
    if (aud.isArray()) {
        return aud.toArray().toVariantList();
    }
    return {aud.toString()};
}

QString IdToken::sub() const
{
    return _payload.value(QLatin1String("sub")).toString();
}

QString IdToken::preferred_username() const
{
    return _payload.value(QLatin1String("preferred_username")).toString();
}

bool IdToken::isValid() const
{
    return !_payload.isEmpty();
}

QJsonObject IdToken::toJson() const
{
    return _payload;
}
