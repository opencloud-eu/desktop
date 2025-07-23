// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once
#include "libsync/common/remotepermissions.h"
#include "libsync/csync.h"

#include <filesystem>

namespace OCC {
/**
 * Represent all the meta-data about a file in the server
 */
struct RemoteInfo
{
    /** FileName of the entry (this does not contain any directory or path, just the plain name */
    QString name;
    QString etag;
    QByteArray fileId;
    QByteArray checksumHeader;
    OCC::RemotePermissions remotePerm;
    time_t modtime = 0;
    int64_t size = 0;
    bool isDirectory = false;
    bool isValid() const { return !name.isNull(); }

    QString directDownloadUrl;
    QString directDownloadCookies;
};

class OPENCLOUD_SYNC_EXPORT LocalInfo
{
public:
    LocalInfo() = default;
    LocalInfo(const std::filesystem::directory_entry &dirent, ItemType type);
    LocalInfo(const std::filesystem::directory_entry &dirent);
    LocalInfo(const std::filesystem::path &path);

    static ItemType typeFromDirectoryEntry(const std::filesystem::directory_entry &dirent);

    // TODO: getter setter, shared_data?
    /** FileName of the entry (this does not contain any directory or path, just the plain name */
    QString name;
    ItemType type = ItemTypeUnsupported;
    time_t modtime = 0;
    int64_t size = 0;
    uint64_t inode = 0;
    bool isDirectory = false;
    bool isHidden = false;
    bool isVirtualFile = false;

    // TODO: remove
    bool isSymLink = false;
    bool isValid() const { return !name.isNull(); }
};
}
