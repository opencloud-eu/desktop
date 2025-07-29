// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once
#include "libsync/csync.h"

#include <QSharedData>

#include <filesystem>

namespace OCC {

class LocalInfoData;
class OPENCLOUD_SYNC_EXPORT LocalInfo
{
public:
    LocalInfo();
    ~LocalInfo();
    LocalInfo(const LocalInfo &other);
    LocalInfo &operator=(const LocalInfo &other);

    void swap(LocalInfo &other) noexcept { d.swap(other.d); }
    QT_MOVE_ASSIGNMENT_OPERATOR_IMPL_VIA_MOVE_AND_SWAP(LocalInfo);

    LocalInfo(const std::filesystem::directory_entry &dirent, ItemType type);
    LocalInfo(const std::filesystem::directory_entry &dirent);

    // TODO: consume path by default
    LocalInfo(const std::filesystem::path &path);


    static ItemType typeFromDirectoryEntry(const std::filesystem::directory_entry &dirent);

    bool isHidden() const;

    /** FileName of the entry (this does not contain any directory or path, just the plain name */
    QString name() const;
    time_t modtime() const;
    int64_t size() const;
    uint64_t inode() const;
    ItemType type() const;
    bool isDirectory() const;
    bool isVirtualFile() const;
    bool isSymLink() const;
    bool isValid() const;

private:
    QExplicitlySharedDataPointer<LocalInfoData> d;
};
}
Q_DECLARE_SHARED(OCC::LocalInfo);
