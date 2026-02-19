// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/opencloudsynclib.h"

#include <QString>

#include <filesystem>

namespace OCC {
namespace FileSystem {
    class OPENCLOUD_SYNC_EXPORT Path
    {
    public:
        /**
         * Will create an absolute path from the given QString.
         * On Windows it will create a UNC path starting with "\\?\".
         * For path segments use Path::relative.
         * @param path
         */
        explicit Path(QAnyStringView path);

        Path(const std::filesystem::path &path);

        Path(std::filesystem::path &&path);

        /**
         * Creates a relative path segment
         * @param path
         * @return  A relative path
         */
        static Path relative(QAnyStringView path);

        const std::filesystem::path &get() const { return _path; }

        [[nodiscard]] operator std::filesystem::path() const { return _path; }

        explicit operator QString() const { return toString(); }

        Path operator/(const Path &other) const { return _path / other._path; }
        Path operator/(const std::filesystem::path &other) const { return _path / other; }
        Path operator/(QAnyStringView other) const { return _path / relative(other); }

        Path &operator/=(const Path &other)
        {
            _path /= other._path;
            return *this;
        }
        Path &operator/=(const std::filesystem::path &other)
        {
            _path /= other;
            return *this;
        }
        Path &operator/=(QAnyStringView other)
        {
            _path /= relative(other);
            return *this;
        }

        QString toString() const;

    private:
        std::filesystem::path _path;
    };
}
}
