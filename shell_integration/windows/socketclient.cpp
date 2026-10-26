// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Hannah von Reth <h.vonreth@opencloud.eu>

#include "socketclient.h"

#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace {

// the pipe does not exist if the client is not running, in that case WaitNamedPipeW returns immediately
constexpr DWORD connectTimeoutC = 100;
constexpr DWORD writeTimeoutC = 1000;
// the client answers from its main thread, which might be busy
constexpr ULONGLONG requestTimeoutC = 2000;

constexpr auto menuItemPrefixC = L"MENU_ITEM:"sv;
constexpr auto menuSeparatorPrefixC = L"MENU_SEPARATOR:"sv;
constexpr auto menuEndC = L"GET_MENU_ITEMS:END"sv;
constexpr auto stringPrefixC = L"STRING:"sv;
constexpr auto stringsEndC = L"GET_STRINGS:END"sv;

std::wstring pipePath()
{
    // must match OCC::Utility::socketApiSocketPath()
    std::wstring userName(256, L'\0');
    const auto size = GetEnvironmentVariableW(L"USERNAME", userName.data(), static_cast<DWORD>(userName.size()));
    if (size == 0 || size >= userName.size()) {
        return {};
    }
    userName.resize(size);
    return L"\\\\.\\pipe\\OpenCloud-" + userName;
}

std::string toUtf8(std::wstring_view in)
{
    const auto size = WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring toUtf16(std::string_view in)
{
    const auto size = MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.data(), static_cast<int>(in.size()), out.data(), size);
    return out;
}

// the socket api separates multiple files with the record separator
std::wstring joinFiles(const std::vector<std::wstring> &files)
{
    std::wstring out;
    for (const auto &file : files) {
        if (!out.empty()) {
            out += L'\x1e';
        }
        out += file;
    }
    return out;
}

// MENU_ITEM:[command]:[flags]:[title], the title might contain colons
std::optional<OCShell::MenuItem> parseMenuItem(std::wstring_view line)
{
    line.remove_prefix(menuItemPrefixC.size());
    const auto commandEnd = line.find(L':');
    if (commandEnd == std::wstring_view::npos) {
        return {};
    }
    const auto flagsEnd = line.find(L':', commandEnd + 1);
    if (flagsEnd == std::wstring_view::npos) {
        return {};
    }
    const auto flags = line.substr(commandEnd + 1, flagsEnd - commandEnd - 1);
    return OCShell::MenuItem{.command = std::wstring(line.substr(0, commandEnd)),
        .title = std::wstring(line.substr(flagsEnd + 1)),
        .enabled = flags.find(L'd') == std::wstring_view::npos};
}
}

void OCShell::log(const std::wstring &message)
{
    OutputDebugStringW((L"OpenCloud ExplorerCommand: " + message + L"\n").data());
}

OCShell::SocketClient::SocketClient()
{
    _overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

OCShell::SocketClient::~SocketClient()
{
    if (_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(_pipe);
    }
    if (_overlapped.hEvent) {
        CloseHandle(_overlapped.hEvent);
    }
}

bool OCShell::SocketClient::connect()
{
    const auto path = pipePath();
    if (path.empty() || !_overlapped.hEvent) {
        return false;
    }
    if (!WaitNamedPipeW(path.data(), connectTimeoutC)) {
        return false;
    }
    _pipe = CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    return _pipe != INVALID_HANDLE_VALUE;
}

bool OCShell::SocketClient::completeIo(BOOL started, DWORD timeout, DWORD *bytes)
{
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    if (WaitForSingleObject(_overlapped.hEvent, timeout) != WAIT_OBJECT_0) {
        CancelIoEx(_pipe, &_overlapped);
        // the buffers must stay valid until the cancellation completed
        GetOverlappedResult(_pipe, &_overlapped, bytes, TRUE);
        return false;
    }
    return GetOverlappedResult(_pipe, &_overlapped, bytes, FALSE);
}

bool OCShell::SocketClient::sendLine(const std::wstring &line)
{
    const auto data = toUtf8(line + L'\n');
    DWORD written = 0;
    return completeIo(WriteFile(_pipe, data.data(), static_cast<DWORD>(data.size()), nullptr, &_overlapped), writeTimeoutC, &written) && written == data.size();
}

std::optional<std::wstring> OCShell::SocketClient::readLine(DWORD timeout)
{
    while (true) {
        if (const auto pos = _buffer.find('\n'); pos != std::string::npos) {
            auto line = toUtf16(std::string_view(_buffer).substr(0, pos));
            _buffer.erase(0, pos + 1);
            return line;
        }
        std::array<char, 4096> chunk;
        DWORD read = 0;
        if (!completeIo(ReadFile(_pipe, chunk.data(), static_cast<DWORD>(chunk.size()), nullptr, &_overlapped), timeout, &read) || read == 0) {
            return {};
        }
        _buffer.append(chunk.data(), read);
    }
}

std::optional<std::vector<std::wstring>> OCShell::SocketClient::request(const std::wstring &message, std::wstring_view end)
{
    SocketClient socket;
    if (!socket.connect()) {
        return {};
    }
    if (!socket.sendLine(message)) {
        log(L"Failed to send " + message);
        return {};
    }

    std::vector<std::wstring> lines;
    const auto deadline = GetTickCount64() + requestTimeoutC;
    while (true) {
        const auto now = GetTickCount64();
        if (now >= deadline) {
            log(L"Timeout while waiting for " + std::wstring(end));
            return {};
        }
        auto line = socket.readLine(static_cast<DWORD>(deadline - now));
        if (!line) {
            log(L"Failed to read the reply for " + std::wstring(end));
            return {};
        }
        if (*line == end) {
            return lines;
        }
        lines.push_back(std::move(*line));
    }
}

std::vector<OCShell::MenuItem> OCShell::SocketClient::fetchMenuItems(const std::vector<std::wstring> &files)
{
    if (files.empty()) {
        return {};
    }
    const auto lines = request(L"GET_MENU_ITEMS:" + joinFiles(files), menuEndC);
    if (!lines) {
        return {};
    }

    std::vector<MenuItem> items;
    for (const auto &line : *lines) {
        if (line.starts_with(menuItemPrefixC)) {
            if (auto item = parseMenuItem(line)) {
                items.push_back(std::move(*item));
            }
        } else if (line.starts_with(menuSeparatorPrefixC)) {
            // drop leading and repeated separators
            if (!items.empty() && !items.back().separator) {
                items.push_back({.separator = true});
            }
        }
        // everything else, like REGISTER_PATH or status broadcasts, is not relevant for us
    }
    if (!items.empty() && items.back().separator) {
        items.pop_back();
    }
    return items;
}

std::map<std::wstring, std::wstring> OCShell::SocketClient::fetchStrings()
{
    const auto lines = request(L"GET_STRINGS:", stringsEndC);
    if (!lines) {
        return {};
    }
    std::map<std::wstring, std::wstring> out;
    for (const auto &line : *lines) {
        // STRING:[key]:[value], the value might contain colons
        if (!line.starts_with(stringPrefixC)) {
            continue;
        }
        const auto keyEnd = line.find(L':', stringPrefixC.size());
        if (keyEnd == std::wstring::npos) {
            continue;
        }
        out.emplace(line.substr(stringPrefixC.size(), keyEnd - stringPrefixC.size()), line.substr(keyEnd + 1));
    }
    return out;
}

bool OCShell::SocketClient::sendCommand(const std::wstring &command, const std::vector<std::wstring> &files)
{
    SocketClient socket;
    if (!socket.connect()) {
        return false;
    }
    // the client might open a dialog, allow it to come to the foreground
    ULONG pid = 0;
    if (GetNamedPipeServerProcessId(socket._pipe, &pid)) {
        AllowSetForegroundWindow(pid);
    }
    return socket.sendLine(command + L':' + joinFiles(files));
}
