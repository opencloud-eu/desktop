/**
* Copyright (c) 2015 Daniel Molkentin <danimo@owncloud.com>. All rights reserved.
*
* This library is free software; you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; either version 2.1 of the License, or (at your option)
* any later version.
*
* This library is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
* details.
*/

#include "OCClientInterface.h"

#include "CommunicationSocket.h"
#include "Log.h"
#include "StringUtil.h"

#include <shlobj.h>

#include <Strsafe.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <iterator>
#include <unordered_set>

// gdiplus min/max
// don't use std yet, as gdiplus will cause issues
using std::max;
using std::min;
#include <gdiplus.h>
using namespace std;

#include <wincrypt.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include "nlohmann/json.hpp"

using Microsoft::WRL::ComPtr;

#define PIPE_TIMEOUT  5*1000 //ms

namespace {

bool sendV2(const CommunicationSocket &socket, const wstring &command, const nlohmann::json &j)
{
    static int messageId = 0;
    const nlohmann::json json { { "id", to_string(messageId++) }, { "arguments", j } };
    const auto data = json.dump();
    wstringstream tmp;
    tmp << command << L":" << StringUtil::toUtf16(data.data(), data.size()) << L"\n";
    return socket.SendMsg(tmp.str());
}

pair<wstring, nlohmann::json> parseV2(const wstring &data)
{
    const auto index = data.find(L":");
    const auto argStart = data.cbegin() + index + 1;
    const auto cData = StringUtil::toUtf8(&*argStart, distance(argStart, data.cend()));
    return { data.substr(0, index), nlohmann::json::parse(cData) };
}

std::shared_ptr<HBITMAP> saveImage(const string &data)
{
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    DWORD size = 2 * 1024;
    std::vector<BYTE> buf(size, 0);
    DWORD skipped;
    if (!CryptStringToBinaryA(data.data(), 0, CRYPT_STRING_BASE64, buf.data(), &size, &skipped, nullptr)) {
        OCShell::logWinError(L"Failed to decode icon");
        return {};
    }
    ComPtr<IStream> stream = SHCreateMemStream(buf.data(), size);
    if (!stream) {
        OCShell::log(L"Failed to create stream");
        return {};
    };
    HBITMAP result;
    Gdiplus::Bitmap bitmap(stream.Get(), true);
    const auto status = bitmap.GetHBITMAP(0, &result);
    if (status != Gdiplus::Ok) {
        OCShell::log(L"Failed to get HBITMAP", to_wstring(status));
        return {};
    }
    return std::shared_ptr<HBITMAP> { new HBITMAP(result), [gdiplusToken](auto o) {
                                         DeleteObject(o);
                                         Gdiplus::GdiplusShutdown(gdiplusToken);
                                     } };
}
}

OCClientInterface::ContextMenuInfo OCClientInterface::FetchInfo(const std::wstring &files)
{
    auto pipename = CommunicationSocket::DefaultPipePath();

    CommunicationSocket socket;
    if (!WaitNamedPipe(pipename.data(), PIPE_TIMEOUT)) {
        OCShell::logWinError(L"OCClientInterface::FetchInfo: Failed to connect to " + pipename);
        return {};
    }
    if (!socket.Connect(pipename)) {
        OCShell::log(L"OCClientInterface::FetchInfo: Failed to connect to " + pipename);
        return {};
    }
    bool ok = sendV2(socket, L"V2/GET_CLIENT_ICON", { { "size", 16 } })
        && socket.SendMsg(L"GET_STRINGS:CONTEXT_MENU_TITLE\n")
        && socket.SendMsg(L"GET_MENU_ITEMS:" + files + L"\n");

    if (!ok) {
        socket.Close();
        OCShell::log(L"OCClientInterface::FetchInfo: Failed to request the context menu");
        return {};
    }


    ContextMenuInfo info;
    bool endReceived = false;
    bool iconReceived = false;
    auto ready = [&] { return endReceived && iconReceived; };

    std::wstring response;
    int sleptCount = 0;
    while (sleptCount < 5) {
        if (socket.ReadLine(&response)) {
            if (StringUtil::begins_with(response, wstring(L"V2/"))) {
                const auto msg = parseV2(response);
                const auto &arguments = msg.second["arguments"];
                if (msg.first == L"V2/GET_CLIENT_ICON_RESULT") {
                    iconReceived = true;
                    if (arguments.contains("error")) {
                        OCShell::log(L"V2/GET_CLIENT_ICON failed", arguments["error"].get<string>());
                    } else {
                        info.icon = saveImage(arguments["png"].get<string>());
                    }
                }

            } else if (StringUtil::begins_with(response, wstring(L"REGISTER_PATH:"))) {
                wstring responsePath = response.substr(14); // length of REGISTER_PATH
                info.watchedDirectories.push_back(responsePath);
            } else if (StringUtil::begins_with(response, wstring(L"STRING:"))) {
                wstring stringName, stringValue;
                if (!StringUtil::extractChunks(response, stringName, stringValue)) {
                    continue;
                }
                if (stringName == L"CONTEXT_MENU_TITLE") {
                    info.contextMenuTitle = std::move(stringValue);
                }
            } else if (StringUtil::begins_with(response, wstring(L"MENU_ITEM:"))) {
                wstring commandName, flags, title;
                if (!StringUtil::extractChunks(response, commandName, flags, title)) {
                    continue;
                }
                info.menuItems.push_back({ commandName, flags, title });
            } else if (StringUtil::begins_with(response, wstring(L"GET_MENU_ITEMS:END"))) {
                endReceived = true;
            }
            if (ready()) {
                return info;
            }
        }
        else {
            Sleep(50);
            ++sleptCount;
        }
    }
    if (endReceived && !iconReceived) {
        OCShell::log(L"OCClientInterface::FetchInfo: received a menu but no icon");
        return info;
    }

    if (!endReceived && iconReceived) {
        OCShell::log(L"OCClientInterface::FetchInfo: received a icon but no menu");
        return {};
    }
    OCShell::log(L"OCClientInterface::FetchInfo: timeout");
    return {};
}

bool OCClientInterface::SendRequest(const wstring &verb, const std::wstring &path)
{
    auto pipename = CommunicationSocket::DefaultPipePath();

    CommunicationSocket socket;
    if (!WaitNamedPipe(pipename.data(), PIPE_TIMEOUT)) {
        return false;
    }
    if (!socket.Connect(pipename)) {
        return false;
    }

    return socket.SendMsg(verb + L":" + path + L"\n");
}
