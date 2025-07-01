/*
* Copyright (C) Fabian Müller <fmueller@owncloud.com>
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

#include "enums.h"

#include "libsync/globalconfig.h"
#include "libsync/theme.h"

#include <QApplication>

using namespace OCC::Wizard;

template <>
QString OCC::Utility::enumToDisplayName(SetupWizardState state)
{
    switch (state) {
    case SetupWizardState::ServerUrlState:
        if (GlobalConfig::serverUrl().isValid()) {
            return QApplication::translate("SetupWizardState", "Server URL");
        } else {
            return QApplication::translate("SetupWizardState", "Welcome");
        }
    case SetupWizardState::CredentialsState:
        return QApplication::translate("SetupWizardState", "Login");
    case SetupWizardState::AccountConfiguredState:
        return QApplication::translate("SetupWizardState", "Sync Options");
    default:
        Q_UNREACHABLE();
    }
}

#include "moc_enums.cpp"
