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

#include "resources/jsontheme.h"

#include "explorercommandids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

// windows.h mus be included before appmodel.h
#include <windows.h>

#include <appmodel.h>

using namespace Qt::Literals::StringLiterals;

namespace OCC {

void Utility::startShellIntegration()
{
    // Register the Explorer context menu for the current user, it talks to us using the socket api.
    // We register on every start to keep the paths up to date.
    if (isInstalledByStore()) {
        // the msix package registers the Explorer integration in its manifest
        qCInfo(lcGuiUtility) << u"Installed as msix package, skipping the registration of the Explorer integration";
        return;
    }
    const QString dllPath = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/" OC_EXPLORER_COMMAND_DLL_NAME));
    if (!QFileInfo::exists(dllPath)) {
        qCInfo(lcGuiUtility) << u"Explorer integration not found at" << dllPath;
        return;
    }

    QSettings server(QStringLiteral("HKEY_CURRENT_USER\\" OC_EXPLORER_COMMAND_CLSID_KEY), QSettings::NativeFormat);
    // "." is the default value of a key
    server.setValue(u"."_s, Resources::JsonTheme::instance().applicationDisplayName());
    server.setValue(u"InprocServer32/."_s, dllPath);
    server.setValue(u"InprocServer32/ThreadingModel"_s, u"Apartment"_s);

    QSettings verb(QStringLiteral("HKEY_CURRENT_USER\\" OC_EXPLORER_COMMAND_VERB_KEY), QSettings::NativeFormat);
    verb.setValue(u"ExplorerCommandHandler"_s, QStringLiteral("{" OC_EXPLORER_COMMAND_CLSID_STRING "}"));
    // an empty SubCommands value turns the verb into a cascading menu, populated by IExplorerCommand::EnumSubCommands
    verb.setValue(u"SubCommands"_s, u""_s);
    // pass the whole selection to a single invocation
    verb.setValue(u"MultiSelectModel"_s, u"Player"_s);

    server.sync();
    verb.sync();
    if (server.status() != QSettings::NoError || verb.status() != QSettings::NoError) {
        qCWarning(lcGuiUtility) << u"Failed to register the Explorer integration";
    }
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
