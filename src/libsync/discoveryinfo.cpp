// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/discoveryinfo.h"
#include "libsync/filesystem.h"

#ifdef Q_OS_WIN
#include "libsync/common/utility_win.h"
#else
#include <sys/stat.h>
#endif

using namespace OCC;

OCC::LocalInfo::LocalInfo(const std::filesystem::directory_entry &dirent, ItemType type)
    : _name(FileSystem::fromFilesystemPath(dirent.path().filename()))
    , _type(type)
{
    Q_ASSERT(!dirent.is_symlink() || type == ItemTypeSymLink);
#ifdef Q_OS_WIN
    auto h = Utility::Handle::createHandle(dirent.path(), {.followSymlinks = false});
    if (!h) {
        qCWarning(lcFileSystem) << dirent.path().native() << h.errorMessage();
        _name.clear();
        return;
    }
    BY_HANDLE_FILE_INFORMATION fileInfo = {};
    if (!GetFileInformationByHandle(h, &fileInfo)) {
        const auto error = GetLastError();
        qCCritical(lcFileSystem) << "GetFileInformationByHandle failed on" << dirent.path().native() << OCC::Utility::formatWinError(error);
        _name.clear();
        return;
    }
    _isHidden = fileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN;
    _inode = ULARGE_INTEGER{{fileInfo.nFileIndexLow, fileInfo.nFileIndexHigh}}.QuadPart;
    _size = ULARGE_INTEGER{{fileInfo.nFileSizeLow, fileInfo.nFileSizeHigh}}.QuadPart;
    _modtime = FileSystem::fileTimeToTime_t(std::filesystem::file_time_type{
        std::filesystem::file_time_type::duration{ULARGE_INTEGER{fileInfo.ftLastWriteTime.dwLowDateTime, fileInfo.ftLastWriteTime.dwHighDateTime}.QuadPart}});
#else
    struct stat sb;
    if (lstat(dirent.path().native().data(), &sb) < 0) {
        qCCritical(lcFileSystem) << "lstat failed on" << dirent.path().native();
        name.clear();
        return;
    }
    _inode = sb.st_ino;
    _size = sb.st_size;
    _modtime = sb.st_mtime;
#ifdef Q_OS_MAC
    _isHidden = sb.st_flags & UF_HIDDEN;
#endif
#endif
}
LocalInfo::LocalInfo(const std::filesystem::directory_entry &dirent)
    : LocalInfo(dirent, LocalInfo::typeFromDirectoryEntry(dirent))
{
}

LocalInfo::LocalInfo(const std::filesystem::path &path)
    : LocalInfo(std::filesystem::directory_entry{path})
{
}

ItemType LocalInfo::typeFromDirectoryEntry(const std::filesystem::directory_entry &dirent)
{
    // a file can be a symlink but point to a regular file, so we check for symlink first
    if (dirent.is_symlink()) {
        return ItemTypeSymLink;
    }
    if (dirent.is_regular_file()) {
        return ItemTypeFile;
    }
    if (dirent.is_directory()) {
        return ItemTypeDirectory;
    }
    return ItemTypeUnsupported;
}
