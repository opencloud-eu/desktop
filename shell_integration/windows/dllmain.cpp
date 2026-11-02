// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "explorercommand.h"

#include <wrl/module.h>

#include <appmodel.h>

#include <format>
#include <mutex>

using namespace Microsoft::WRL;

// CoCreatableClass can't handle qualified names
using OCShell::ExplorerCommand;
CoCreatableClass(ExplorerCommand);

namespace {
HMODULE sModule = nullptr;

std::wstring modulePath(HMODULE module)
{
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())));
    return path;
}

std::wstring packageFullName()
{
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
        return L"<none>";
    }
    std::wstring name(length, L'\0');
    if (GetCurrentPackageFullName(&length, name.data()) != ERROR_SUCCESS) {
        return L"<none>";
    }
    name.resize(length - 1);
    return name;
}

// not logged from DllMain, which must not do any work
void logLoaded()
{
    static std::once_flag once;
    std::call_once(once, [] {
        OCShell::log(
            std::format(L"Loaded {} into {} (pid {}), package: {}", modulePath(sModule), modulePath(nullptr), GetCurrentProcessId(), packageFullName()));
    });
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        sModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv)
{
    logLoaded();
    const auto result = Module<InProc>::GetModule().GetClassObject(clsid, riid, ppv);
    if (FAILED(result)) {
        OCShell::log(std::format(L"DllGetClassObject failed: {:#010x}", static_cast<uint32_t>(result)));
    }
    return result;
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}
