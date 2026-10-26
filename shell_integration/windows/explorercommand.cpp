// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "explorercommand.h"

#include <comdef.h>
#include <shlwapi.h>

#include <algorithm>
#include <format>

using namespace Microsoft::WRL;

namespace {

// EnumSubCommands is called before the selection is known, so we provide a fixed number of sub commands
// and hide the ones that are not needed. The client currently provides up to 10 items.
constexpr size_t subCommandCountC = 16;

std::wstring errorString(HRESULT hr)
{
    return std::format(L"{:#010x}: {}", static_cast<uint32_t>(hr), _com_error(hr).ErrorMessage());
}

std::vector<std::wstring> filePaths(IShellItemArray *items)
{
    if (!items) {
        return {};
    }
    DWORD count = 0;
    if (const auto hr = items->GetCount(&count); FAILED(hr)) {
        OCShell::log(L"Failed to get the item count " + errorString(hr));
        return {};
    }
    std::vector<std::wstring> out;
    out.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        if (const auto hr = items->GetItemAt(i, &item); FAILED(hr)) {
            OCShell::log(std::format(L"Failed to get item {} {}", i, errorString(hr)));
            return {};
        }
        LPWSTR path = nullptr;
        // fails for items that are not part of the file system
        if (const auto hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path); FAILED(hr)) {
            OCShell::log(std::format(L"Item {} is not a file system item {}", i, errorString(hr)));
            return {};
        }
        out.emplace_back(path);
        CoTaskMemFree(path);
    }
    return out;
}

HRESULT copyString(const std::wstring &in, LPWSTR *out)
{
    *out = nullptr;
    if (in.empty()) {
        return E_FAIL;
    }
    return SHStrDupW(in.data(), out);
}

// Represents the menu item at _index for the selection passed to the methods
class SubCommand final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand>
{
public:
    SubCommand(std::shared_ptr<OCShell::MenuCache> cache, size_t index)
        : _cache(std::move(cache))
        , _index(index)
    {
    }

    IFACEMETHODIMP GetTitle(IShellItemArray *items, LPWSTR *name) override
    {
        *name = nullptr;
        const auto item = menuItem(filePaths(items));
        return item ? copyString(item->title, name) : E_FAIL;
    }

    IFACEMETHODIMP GetIcon(IShellItemArray *, LPWSTR *icon) override
    {
        *icon = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray *, LPWSTR *infoTip) override
    {
        *infoTip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID *commandName) override
    {
        *commandName = GUID_NULL;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetState(IShellItemArray *items, BOOL, EXPCMDSTATE *cmdState) override
    {
        const auto item = menuItem(filePaths(items));
        *cmdState = !item ? ECS_HIDDEN : item->enabled ? ECS_ENABLED : ECS_DISABLED;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray *items, IBindCtx *) override
    {
        const auto files = filePaths(items);
        const auto item = menuItem(files);
        if (files.empty() || !item) {
            OCShell::log(std::format(L"Not invoking sub command {}, no files or no menu item", _index));
            return E_FAIL;
        }
        OCShell::log(std::format(L"Invoking {} for {} file(s), first: {}", item->command, files.size(), files.front()));
        if (!OCShell::SocketClient::sendCommand(item->command, files)) {
            OCShell::log(L"Failed to send " + item->command);
            return E_FAIL;
        }
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *flags) override
    {
        // the flags are queried without the selection, so we can't provide separators
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **enumCommands) override
    {
        *enumCommands = nullptr;
        return E_NOTIMPL;
    }

private:
    std::optional<OCShell::MenuItem> menuItem(const std::vector<std::wstring> &files) const
    {
        auto menuItems = _cache->menuItems(files);
        if (_index >= menuItems.size()) {
            return {};
        }
        return std::move(menuItems[_index]);
    }

    const std::shared_ptr<OCShell::MenuCache> _cache;
    const size_t _index;
};

class EnumCommands final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IEnumExplorerCommand>
{
public:
    explicit EnumCommands(std::vector<ComPtr<IExplorerCommand>> commands)
        : _commands(std::move(commands))
    {
    }

    IFACEMETHODIMP Next(ULONG count, IExplorerCommand **commands, ULONG *fetched) override
    {
        ULONG i = 0;
        for (; i < count && _current < _commands.size(); ++i, ++_current) {
            _commands[_current].CopyTo(&commands[i]);
        }
        if (fetched) {
            *fetched = i;
        }
        return i == count ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG count) override
    {
        const auto skipped = std::min<size_t>(count, _commands.size() - _current);
        _current += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Reset() override
    {
        _current = 0;
        return S_OK;
    }

    IFACEMETHODIMP Clone(IEnumExplorerCommand **out) override
    {
        *out = nullptr;
        return E_NOTIMPL;
    }

private:
    const std::vector<ComPtr<IExplorerCommand>> _commands;
    size_t _current = 0;
};
}

IFACEMETHODIMP OCShell::ExplorerCommand::GetTitle(IShellItemArray *, LPWSTR *name)
{
    return copyString(_cache->string(L"CONTEXT_MENU_TITLE"), name);
}

IFACEMETHODIMP OCShell::ExplorerCommand::GetIcon(IShellItemArray *, LPWSTR *icon)
{
    // an icon resource like C:\path\opencloud.exe,0
    return copyString(_cache->string(L"CONTEXT_MENU_ICON"), icon);
}

IFACEMETHODIMP OCShell::ExplorerCommand::GetToolTip(IShellItemArray *, LPWSTR *infoTip)
{
    *infoTip = nullptr;
    return E_NOTIMPL;
}

IFACEMETHODIMP OCShell::ExplorerCommand::GetCanonicalName(GUID *commandName)
{
    *commandName = __uuidof(ExplorerCommand);
    return S_OK;
}

std::vector<OCShell::MenuItem> OCShell::MenuCache::menuItems(const std::vector<std::wstring> &files)
{
    // the lock is held during the request, so concurrent callers wait for its result instead of sending their own
    std::lock_guard lock(_mutex);
    // methods that are called without a selection get the last known items
    if (files.empty() || (_menuItems && files == _files)) {
        return _menuItems.value_or(std::vector<MenuItem>{});
    }
    const auto start = GetTickCount64();
    auto menuItems = SocketClient::fetchMenuItems(files);
    // separators are not supported, see SubCommand::GetFlags
    std::erase_if(menuItems, [](const MenuItem &item) { return item.separator; });
    if (menuItems.size() > subCommandCountC) {
        log(std::format(L"Received {} menu items, only {} are displayed", menuItems.size(), subCommandCountC));
    }
    log(std::format(L"MenuCache: {} thread {}, {} file(s), first: {}, received {} menu item(s) in {}ms", static_cast<const void *>(this), GetCurrentThreadId(),
        files.size(), files.front(), menuItems.size(), GetTickCount64() - start));
    _files = files;
    _menuItems = std::move(menuItems);
    return *_menuItems;
}

std::wstring OCShell::MenuCache::string(const std::wstring &key)
{
    std::lock_guard lock(_stringsMutex);
    // if the client is not running we get nothing, try again on the next call
    if (_strings.empty()) {
        _strings = SocketClient::fetchStrings();
        log(std::format(L"MenuCache: {} received {} string(s)", static_cast<const void *>(this), _strings.size()));
    }
    const auto it = _strings.find(key);
    if (it == _strings.end()) {
        log(L"The client did not provide " + key);
        return {};
    }
    return it->second;
}

IFACEMETHODIMP OCShell::ExplorerCommand::GetState(IShellItemArray *items, BOOL, EXPCMDSTATE *cmdState)
{
    // We ignore okToBeSlow, the classic context menu does not call us again if we return E_PENDING.
    // The requests to the client use short timeouts instead.
    *cmdState = _cache->menuItems(filePaths(items)).empty() ? ECS_HIDDEN : ECS_ENABLED;
    return S_OK;
}

IFACEMETHODIMP OCShell::ExplorerCommand::Invoke(IShellItemArray *, IBindCtx *)
{
    // Only the sub commands can be invoked, if we end up here the verb is not registered as a cascading menu.
    // Don't return an error, Explorer would display it to the user.
    log(L"The root command was invoked, is the SubCommands registry value missing?");
    return S_OK;
}

IFACEMETHODIMP OCShell::ExplorerCommand::GetFlags(EXPCMDFLAGS *flags)
{
    *flags = ECF_HASSUBCOMMANDS;
    return S_OK;
}

IFACEMETHODIMP OCShell::ExplorerCommand::EnumSubCommands(IEnumExplorerCommand **enumCommands)
{
    *enumCommands = nullptr;
    std::vector<ComPtr<IExplorerCommand>> commands;
    commands.reserve(subCommandCountC);
    for (size_t i = 0; i < subCommandCountC; ++i) {
        auto command = Make<SubCommand>(_cache, i);
        if (!command) {
            return E_OUTOFMEMORY;
        }
        commands.push_back(std::move(command));
    }
    log(std::format(L"EnumSubCommands: {} thread {}", static_cast<const void *>(this), GetCurrentThreadId()));
    auto enumerator = Make<EnumCommands>(std::move(commands));
    if (!enumerator) {
        return E_OUTOFMEMORY;
    }
    return enumerator.CopyTo(enumCommands);
}
