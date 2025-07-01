/*
 * Copyright (C) by Erik Verbruggen <erik@verbruggen.consulting>
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

#include "application.h"
#include "guiutility.h"

#include <QCoreApplication>

// windows.h mus be included before appmodel.h
#include <windows.h>

#include <appmodel.h>

namespace OCC {

void Utility::startShellIntegration()
{
}

QString Utility::socketApiSocketPath()
{
    return QStringLiteral(R"(\\.\pipe\OpenCloud-%1)").arg(qEnvironmentVariable("USERNAME"));
}

bool Utility::isInstalledByStore()
{
    uint32_t length = 0;
    return GetCurrentPackageFamilyName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

} // namespace OCC
