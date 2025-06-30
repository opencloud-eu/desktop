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
#pragma once

#include <QJsonObject>

// https://openid.net/specs/openid-connect-core-1_0.html#IDToken
namespace OCC {
class IdToken
{
public:
    IdToken();
    explicit IdToken(const QJsonObject &paylod);

    QVariantList aud() const;

    QString sub() const;

    QString preferred_username() const;
    QString name() const;

    bool isValid() const;

    QJsonObject toJson() const;

private:
    QJsonObject _payload;
};
}
