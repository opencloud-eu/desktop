/*
 * Copyright (C) by Olivier Goffart <ogoffart@owncloud.com>
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

#include "opencloudsynclib.h"
// Chain in the base include and extend the namespace
#include "common/filesystembase.h"
#include "common/result.h"

#ifdef Q_OS_WIN
#include "common/utility_win.h"
#endif

class QFile;

namespace OCC {

class SyncJournal;

/**
 *  \addtogroup libsync
 *  @{
 */

/**
 * @brief This file contains file system helper
 */
namespace FileSystem {
    /**
     * @brief compare two files with given filename and return true if they have the same content
     */
    bool fileEquals(const QString &fn1, const QString &fn2);


    OPENCLOUD_SYNC_EXPORT time_t fileTimeToTime_t(std::filesystem::file_time_type fileTime);
    OPENCLOUD_SYNC_EXPORT std::filesystem::file_time_type time_tToFileTime(time_t fileTime);
    /**
     * @brief Get the mtime for a filepath
     *
     * Use this over QFileInfo::lastModified() to avoid timezone related bugs. See
     * owncloud/core#9781 for details.
     */

    time_t OPENCLOUD_SYNC_EXPORT getModTime(const std::filesystem::path &filename);
    inline time_t getModTime(const QString &filename)
    {
        return getModTime(toFilesystemPath(filename));
    }

    bool OPENCLOUD_SYNC_EXPORT setModTime(const std::filesystem::path &filename, time_t modTime);
    inline time_t setModTime(const QString &filename, time_t modTime)
    {
        return setModTime(toFilesystemPath(filename), modTime);
    }

    /**
     * @brief Get the size for a file
     *
     * Use this over QFileInfo::size() to avoid bugs with lnk files on Windows.
     * See https://bugreports.qt.io/browse/QTBUG-24831.
     */
    qint64 OPENCLOUD_SYNC_EXPORT getSize(const std::filesystem::path &filename);
    inline qint64 getSize(const QFileInfo &info)
    {
        return getSize(toFilesystemPath(info.absoluteFilePath()));
    }

    /**
     * @brief Retrieve a file inode with csync
     */
    bool OPENCLOUD_SYNC_EXPORT getInode(const std::filesystem::path &filename, quint64 *inode);
    inline bool getInode(const QString &filename, quint64 *inode)
    {
        return getInode(toFilesystemPath(filename), inode);
    }


    /**
     * @brief Check if \a fileName has changed given previous size and mtime
     *
     * Nonexisting files are covered through mtime: they have an previousMtime of -1.
     *
     * @return true if the file's mtime or size are not what is expected.
     */
    bool OPENCLOUD_SYNC_EXPORT fileChanged(const QFileInfo &info, qint64 previousSize, time_t previousMtime, std::optional<quint64> previousInode = {});


    struct RemoveEntry
    {
        const QString path;
        const bool isDir;
    };
    struct RemoveError
    {
        const RemoveEntry entry;
        const QString error;
    };

    using RemoveEntryList = std::vector<RemoveEntry>;
    using RemoveErrorList = std::vector<RemoveError>;

    /**
     * Removes a directory and its contents recursively
     *
     * Returns true if all removes succeeded.
     */
    bool OPENCLOUD_SYNC_EXPORT removeRecursively(const QString &path, RemoveEntryList *success, RemoveEntryList *locked, RemoveErrorList *errors);

    namespace Tags {
        std::optional<QByteArray> OPENCLOUD_SYNC_EXPORT get(const QString &path, const QString &key);
        OCC::Result<void, QString> OPENCLOUD_SYNC_EXPORT set(const QString &path, const QString &key, const QByteArray &value);
        bool OPENCLOUD_SYNC_EXPORT remove(const QString &path, const QString &key);
    }
}

/** @} */
}
