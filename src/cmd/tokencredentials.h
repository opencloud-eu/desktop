/*
 * Copyright (C) by Hannah von Reth <h.vonreth@opencloud.eu>
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

#include "libsync/creds/abstractcredentials.h"

namespace OCC {
class TokensAccessManager;

class TokenCredentials : public AbstractCredentials
{
private:
    Q_OBJECT
public:
    TokenCredentials(QByteArray &&username, QByteArray &&token);

    AccessManager *createAM() const override;
    bool ready() const override;
    void fetchFromKeychain() override;
    void restartOauth() override;
    void persist() override;
    void invalidateToken() override;
    void forgetSensitiveData() override;

private:
    TokensAccessManager *_accessManager;
};
}
