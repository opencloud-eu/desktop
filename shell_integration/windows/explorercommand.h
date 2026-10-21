// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "explorercommandids.h"
#include "socketclient.h"

#include <shobjidl_core.h>
#include <wrl/implements.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace OCShell {

/**
 * Caches the menu items for a selection and the strings provided by the client.
 * Explorer calls the methods of the root command and of the sub commands from different threads in no particular order,
 * every method that receives the selection asks the cache, only the first one requests the items from the client.
 */
class MenuCache
{
public:
    std::vector<MenuItem> menuItems(const std::vector<std::wstring> &files);

    // a string provided by GET_STRINGS, empty if the client is not running
    std::wstring string(const std::wstring &key);

private:
    std::mutex _mutex;
    std::vector<std::wstring> _files;
    // unset until the items were requested
    std::optional<std::vector<MenuItem>> _menuItems;

    std::mutex _stringsMutex;
    std::map<std::wstring, std::wstring> _strings;
};

/**
 * The root entry of the context menu, the sub commands are provided by the client.
 */
class __declspec(uuid(OC_EXPLORER_COMMAND_CLSID_STRING)) ExplorerCommand final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IExplorerCommand>
{
public:
    // IExplorerCommand
    IFACEMETHODIMP GetTitle(IShellItemArray *items, LPWSTR *name) override;
    IFACEMETHODIMP GetIcon(IShellItemArray *items, LPWSTR *icon) override;
    IFACEMETHODIMP GetToolTip(IShellItemArray *items, LPWSTR *infoTip) override;
    IFACEMETHODIMP GetCanonicalName(GUID *commandName) override;
    IFACEMETHODIMP GetState(IShellItemArray *items, BOOL okToBeSlow, EXPCMDSTATE *cmdState) override;
    IFACEMETHODIMP Invoke(IShellItemArray *items, IBindCtx *bindCtx) override;
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS *flags) override;
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand **enumCommands) override;

private:
    const std::shared_ptr<MenuCache> _cache = std::make_shared<MenuCache>();
};

}
