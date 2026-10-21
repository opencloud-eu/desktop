// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "explorercommand.h"

#include <wrl/module.h>

using namespace Microsoft::WRL;

// CoCreatableClass can't handle qualified names
using OCShell::ExplorerCommand;
CoCreatableClass(ExplorerCommand);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

// The client registers the extension for the current user on startup, see OCC::Utility::startShellIntegration().
// This allows an uninstaller to remove the registration with regsvr32 /u.
STDAPI DllUnregisterServer()
{
    for (const auto *key : {L"" OC_EXPLORER_COMMAND_VERB_KEY, L"" OC_EXPLORER_COMMAND_CLSID_KEY}) {
        const auto result = RegDeleteTreeW(HKEY_CURRENT_USER, key);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            return HRESULT_FROM_WIN32(result);
        }
    }
    return S_OK;
}
