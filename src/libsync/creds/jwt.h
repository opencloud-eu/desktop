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
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace OCC {
class JWT
{
public:
    JWT();
    explicit JWT(const QByteArray &jwt);

    QByteArray serialize() const;

    bool isValid() const;

protected:
    QJsonObject _header;
    QJsonObject _payload;
    QByteArray _signauture;
};

// https://openid.net/specs/openid-connect-core-1_0.html#IDToken
class IdToken : public OCC::JWT
{
public:
    using OCC::JWT::JWT;

    QString sub() const;

    QString preferred_username() const;
};
}
