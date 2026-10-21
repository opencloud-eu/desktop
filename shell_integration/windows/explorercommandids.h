// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

// Shared between the Explorer extension and the client, which registers the extension for the current user.
// The values are narrow string literals so they can be used with L"", u"" and __declspec(uuid()).

#define OC_EXPLORER_COMMAND_CLSID_STRING "850FD5BE-2D56-4653-9DD7-C6F5DF8143B3"

// keep in sync with the target name in shell_integration/windows/CMakeLists.txt
#define OC_EXPLORER_COMMAND_DLL_NAME "OpenCloudExplorerCommand.dll"

// relative to HKEY_CURRENT_USER
#define OC_EXPLORER_COMMAND_CLSID_KEY "Software\\Classes\\CLSID\\{" OC_EXPLORER_COMMAND_CLSID_STRING "}"
#define OC_EXPLORER_COMMAND_VERB_KEY "Software\\Classes\\AllFilesystemObjects\\shell\\OpenCloud"
