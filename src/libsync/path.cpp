// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "path.h"

#include "libsync/common/filesystembase.h"

OCC::FileSystem::Path::Path(QAnyStringView path)
    : _path(toFilesystemPath(path.toString()))
{
}

OCC::FileSystem::Path::Path(const std::filesystem::path &path)
    : _path(path)
{
}

OCC::FileSystem::Path::Path(std::filesystem::path &&path)
    : _path(std::move(path))
{
}

OCC::FileSystem::Path OCC::FileSystem::Path::relative(QAnyStringView path)
{
    return QtPrivate::toFilesystemPath(path.toString());
}

QString OCC::FileSystem::Path::toString() const
{
    return fromFilesystemPath(_path);
}
