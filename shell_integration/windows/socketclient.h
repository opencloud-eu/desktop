// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include <windows.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OCShell {

void log(const std::wstring &message);

struct MenuItem
{
    std::wstring command;
    std::wstring title;
    bool enabled = true;
    bool separator = false;
};

/**
 * A minimal client for the socket api of the desktop client, see OCC::SocketApi.
 */
class SocketClient
{
public:
    SocketClient();
    ~SocketClient();

    SocketClient(const SocketClient &) = delete;
    SocketClient &operator=(const SocketClient &) = delete;

    /**
     * Requests the context menu entries for files.
     * Returns an empty list if the client is not running or the files are not handled by the client.
     */
    static std::vector<MenuItem> fetchMenuItems(const std::vector<std::wstring> &files);

    /**
     * Requests the strings provided by GET_STRINGS, like CONTEXT_MENU_TITLE and CONTEXT_MENU_ICON.
     * Returns an empty map if the client is not running.
     */
    static std::map<std::wstring, std::wstring> fetchStrings();

    [[nodiscard]] static bool sendCommand(const std::wstring &command, const std::vector<std::wstring> &files);

private:
    // sends message and returns the received lines up to end
    static std::optional<std::vector<std::wstring>> request(const std::wstring &message, std::wstring_view end);

    bool connect();
    bool sendLine(const std::wstring &line);
    std::optional<std::wstring> readLine(DWORD timeout);
    bool completeIo(BOOL started, DWORD timeout, DWORD *bytes);

    HANDLE _pipe = INVALID_HANDLE_VALUE;
    OVERLAPPED _overlapped = {};
    std::string _buffer;
};

}
