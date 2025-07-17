/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
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

#pragma once

#include "libsync/opencloudsynclib.h"

#include <QString>

#include <filesystem>

#include <functional>
#include <qt_windows.h>

namespace OCC {
namespace Utility {
    class OPENCLOUD_SYNC_EXPORT Handle
    {
    public:
        /**
         * A RAAI for Windows Handles
         */
        Handle() = default;
        explicit Handle(HANDLE h);
        explicit Handle(HANDLE h, std::function<void(HANDLE)> &&close, uint32_t error = NO_ERROR);

        struct CreateHandleParameter
        {
            uint32_t accessMode = 0;
            uint32_t shareMode = FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE;
            uint32_t creationFlags = OPEN_EXISTING;
            bool followSymlinks = true;
            bool async = false;
        };
        static Handle createHandle(const std::filesystem::path &path, const CreateHandleParameter &p = {});

        Handle(const Handle &) = delete;
        Handle &operator=(const Handle &) = delete;

        Handle(Handle &&other)
        {
            std::swap(_handle, other._handle);
            std::swap(_close, other._close);
        }

        Handle &operator=(Handle &&other)
        {
            if (this != &other) {
                std::swap(_handle, other._handle);
                std::swap(_close, other._close);
                std::swap(_error, other._error);
            }
            return *this;
        }

        ~Handle();

        const HANDLE &handle() const { return _handle; }

        void close();

        explicit operator bool() const { return _handle != INVALID_HANDLE_VALUE; }

        operator HANDLE() const { return _handle; }

        HANDLE release() { return std::exchange(_handle, INVALID_HANDLE_VALUE); }

        uint32_t error() const;
        bool hasError() const;

        QString errorMessage() const;

    private:
        HANDLE _handle = INVALID_HANDLE_VALUE;
        std::function<void(HANDLE)> _close;
        uint32_t _error = NO_ERROR;
    };

    // Possibly refactor to share code with UnixTimevalToFileTime in c_time.c
    OPENCLOUD_SYNC_EXPORT void UnixTimeToFiletime(time_t t, FILETIME *filetime);
    OPENCLOUD_SYNC_EXPORT void FiletimeToLargeIntegerFiletime(const FILETIME *filetime, LARGE_INTEGER *hundredNSecs);
    OPENCLOUD_SYNC_EXPORT void UnixTimeToLargeIntegerFiletime(time_t t, LARGE_INTEGER *hundredNSecs);

    OPENCLOUD_SYNC_EXPORT QString formatWinError(long error);

    class OPENCLOUD_SYNC_EXPORT NtfsPermissionLookupRAII
    {
    public:
        /**
         * NTFS permissions lookup is disabled by default for performance reasons
         * Enable it and disable it again once we leave the scope
         * https://doc.qt.io/Qt-5/qfileinfo.html#ntfs-permissions
         */
        NtfsPermissionLookupRAII();
        ~NtfsPermissionLookupRAII();

    private:
        Q_DISABLE_COPY(NtfsPermissionLookupRAII);
    };
}
}
