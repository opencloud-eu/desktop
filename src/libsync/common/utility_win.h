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
#include <qt_windows.h>

#include <filesystem>

#include <functional>

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

        /**
         *
         * Returns the file system path associated with this handle, if it was set during handle creation.
         *
         * This can be useful for error reporting, logging, or debugging, as it allows you to identify
         * the file or resource that the handle refers to. If no path was set, the returned path may be empty.
         *
         * @return Reference to the associated std::filesystem::path, or an empty path if not set.
         */
        const std::filesystem::path &path() const;

        QString errorMessage() const;

    private:
        HANDLE _handle = INVALID_HANDLE_VALUE;
        std::function<void(HANDLE)> _close;
        uint32_t _error = NO_ERROR;
        std::filesystem::path _path;
    };

    OPENCLOUD_SYNC_EXPORT void UnixTimeToLargeIntegerFiletime(time_t t, LARGE_INTEGER *hundredNSecs);

    OPENCLOUD_SYNC_EXPORT QString formatWinError(long error);
}
}
