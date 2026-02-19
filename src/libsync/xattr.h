// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/common/result.h"
#include "libsync/opencloudsynclib.h"

#include <filesystem>
#include <optional>

#include <QString>

namespace OCC {
namespace FileSystem {
    namespace Xattr {
        OPENCLOUD_SYNC_EXPORT bool supportsxattr(const std::filesystem::path &path);
        OPENCLOUD_SYNC_EXPORT std::optional<QByteArray> getxattr(const std::filesystem::path &path, const QString &name);
        OPENCLOUD_SYNC_EXPORT Result<void, QString> setxattr(const std::filesystem::path &path, const QString &name, const QByteArray &value);
        OPENCLOUD_SYNC_EXPORT Result<void, QString> removexattr(const std::filesystem::path &path, const QString &name);
    }
}
}
