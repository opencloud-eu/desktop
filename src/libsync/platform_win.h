/*
 * Copyright (C) by Fabian Müller <fmueller@owncloud.com>
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

#include "platform.h"

namespace OCC {

class WinPlatform : public Platform
{
public:
    ~WinPlatform() override;

    void setApplication(QCoreApplication *application) override;

    void startServices() override;

private:
    WinPlatform(Type type);

    /// Utility thread that takes care of proper Windows logout handling.
    void startShutdownWatcher();

    friend class Platform;
};

} // namespace OCC
