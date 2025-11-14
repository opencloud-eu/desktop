/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "filesystem.h"

#include "common/asserts.h"
#include "common/utility.h"
#include "libsync/discoveryinfo.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "csync.h"
#include "syncfileitem.h"

#include <sys/stat.h>

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
#include <sys/xattr.h>
#endif

#ifdef Q_OS_WIN32
#include "common/utility_win.h"
#include <winsock2.h>
#endif

using namespace Qt::Literals::StringLiterals;

namespace {
static constexpr unsigned MaxValueSize = 1023; // This is without a terminating NUL character
} // anonymous namespace

namespace OCC {

bool FileSystem::fileEquals(const QString &fn1, const QString &fn2)
{
    // compare two files with given filename and return true if they have the same content
    QFile f1(fn1);
    QFile f2(fn2);
    if (!f1.open(QIODevice::ReadOnly) || !f2.open(QIODevice::ReadOnly)) {
        qCWarning(lcFileSystem) << u"fileEquals: Failed to open " << fn1 << u"or" << fn2;
        return false;
    }

    if (getSize(QFileInfo{fn1}) != getSize(QFileInfo{fn2})) {
        return false;
    }

    const int BufferSize = 16 * 1024;
    QByteArray buffer1(BufferSize, 0);
    QByteArray buffer2(BufferSize, 0);
    // the files have the same size, compare all of it
    while (!f1.atEnd()) {
        f1.read(buffer1.data(), BufferSize);
        f2.read(buffer2.data(), BufferSize);
        if (buffer1 != buffer2) {
            return false;
        }
    };
    return true;
}
time_t FileSystem::fileTimeToTime_t(std::filesystem::file_time_type fileTime)
{
#ifdef HAS_CLOCK_CAST
    return std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(fileTime));
#else
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(std::chrono::file_clock::to_sys(fileTime));
    return std::chrono::system_clock::to_time_t(systemTime);
#endif
}
std::filesystem::file_time_type FileSystem::time_tToFileTime(time_t fileTime)
{
#ifdef HAS_CLOCK_CAST
    return std::chrono::clock_cast<std::chrono::file_clock>(std::chrono::system_clock::from_time_t(fileTime));
#else
    return std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(fileTime));
#endif
}

time_t FileSystem::getModTime(const std::filesystem::path &filename)
{
    std::error_code rc;
    const auto fileTime = std::filesystem::last_write_time(filename, rc);
    if (rc) {
        Q_ASSERT(!rc);
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    } else {
        return fileTimeToTime_t(fileTime);
    }
}

bool FileSystem::setModTime(const std::filesystem::path &filename, time_t modTime)
{
    std::error_code rc;
    std::filesystem::last_write_time(filename, time_tToFileTime(modTime), rc);
    if (rc) {
        qCWarning(lcFileSystem) << u"Error setting mtime for" << filename << u"failed: rc" << rc.value() << u", error message:" << rc.message();
        Q_ASSERT(!rc);
        return false;
    }
    return true;
}

FileSystem::FileChangedInfo FileSystem::FileChangedInfo::fromSyncFileItem(const SyncFileItem *const item)
{
    return {.size = item->_size, .mtime = item->_modtime, .inode = (item->_inode == 0 ? std::optional<quint64>{} : item->_inode), .type = item->_type};
}

FileSystem::FileChangedInfo FileSystem::FileChangedInfo::fromSyncFileItemPrevious(const SyncFileItem *const item)
{
    return {.size = item->_previousSize, .mtime = item->_previousModtime, .type = item->_type};
}

FileSystem::FileChangedInfo FileSystem::FileChangedInfo::fromSyncJournalFileRecord(const SyncJournalFileRecord &record)
{
    if (!record.isValid()) {
        return {};
    }
    return {.size = record.size(), .mtime = record.modtime(), .inode = record.inode(), .type = record.type()};
}

bool FileSystem::fileChanged(const std::filesystem::path &path, const FileChangedInfo &previousInfo)
{
    const auto dirent = std::filesystem::directory_entry{path};
    if (!dirent.exists()) {
        if (previousInfo.mtime.has_value()) {
            qCDebug(lcFileSystem) << path.native() << u"was removed";
            return true;
        } else {
            // the file didn't exist and doesn't exist
            Q_ASSERT(false); // pointless call
            return false;
        }
    }

    const auto type = LocalInfo::typeFromDirectoryEntry(dirent);
    if (previousInfo.type != ItemTypeUnsupported) {
        // only check for dir and file, as virtual files are irrelevant here
        if (previousInfo.type == ItemTypeDirectory && type == ItemTypeFile) {
            qCDebug(lcFileSystem) << u"File" << path.native() << u"has changed: from dir to file";
            return true;
        }
        if (previousInfo.type == ItemTypeFile && type == ItemTypeDirectory) {
            qCDebug(lcFileSystem) << u"File" << path.native() << u"has changed: from file to dir";
            return true;
        }
    }
    const auto info = LocalInfo(dirent, type);
    if (previousInfo.inode.has_value() && previousInfo.inode.value() != info.inode()) {
        qCDebug(lcFileSystem) << u"File" << path.native() << u"has changed: inode" << previousInfo.inode.value() << u"<-->" << info.inode();
        return true;
    }
    if (info.isDirectory()) {
        if (previousInfo.size != 0) {
            qCDebug(lcFileSystem) << u"File" << path.native() << u"has changed: from file to dir";
            return true;
        }
    } else if (info.size() != previousInfo.size) {
        qCDebug(lcFileSystem) << u"File" << path.native() << u"has changed: size: " << previousInfo.size << u"<->" << info.size();
        return true;
    }
    if (info.modtime() != previousInfo.mtime) {
        qCDebug(lcFileSystem) << u"File" << path.native() << u"has changed: mtime: " << previousInfo.mtime << u"<->" << info.modtime();
        return true;
    }
    return false;
}

qint64 FileSystem::getSize(const std::filesystem::path &filename)
{
    std::error_code ec;
    const quint64 size = std::filesystem::file_size(filename, ec);
    if (ec) {
        if (!std::filesystem::is_directory(filename)) {
            qCCritical(lcFileSystem) << u"Error getting size for" << filename << ec.value() << ec.message();
        } else {
            qCWarning(lcFileSystem) << u"Called getFileSize on a directory";
            Q_ASSERT(false);
        }
        return 0;
    }
    return size;
}

// Code inspired from Qt5's QDir::removeRecursively
bool FileSystem::removeRecursively(const QString &path,
    RemoveEntryList *success,
    RemoveEntryList *locked,
    RemoveErrorList *errors)
{
    bool allRemoved = true;
    QDirIterator di(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

    QString removeError;
    while (di.hasNext()) {
        di.next();
        const QFileInfo &fi = di.fileInfo();
        // The use of isSymLink here is okay:
        // we never want to go into this branch for .lnk files
        const bool isDir = fi.isDir() && !fi.isSymLink() && !FileSystem::isJunction(fi.absoluteFilePath());
        if (isDir) {
            allRemoved &= removeRecursively(path + QLatin1Char('/') + di.fileName(), success, locked, errors); // recursive
        } else {
            if (FileSystem::isFileLocked(di.filePath(), FileSystem::LockMode::Exclusive)) {
                locked->push_back({ di.filePath(), isDir });
                allRemoved = false;
            } else {
                if (FileSystem::remove(di.filePath(), &removeError)) {
                    success->push_back({ di.filePath(), isDir });
                } else {
                    errors->push_back({ { di.filePath(), isDir }, removeError });
                    qCWarning(lcFileSystem) << u"Error removing " << di.filePath() << ':' << removeError;
                    allRemoved = false;
                }
            }
        }
    }
    if (allRemoved) {
        allRemoved = QDir().rmdir(path);
        if (allRemoved) {
            success->push_back({ path, true });
        } else {
            errors->push_back({ { path, true }, QCoreApplication::translate("FileSystem", "Could not remove folder") });
            qCWarning(lcFileSystem) << u"Error removing folder" << path;
        }
    }
    return allRemoved;
}

bool FileSystem::getInode(const std::filesystem::path &filename, quint64 *inode)
{
    const LocalInfo info(filename);
    Q_ASSERT(info.isValid());
    if (!info.isValid()) {
        *inode = 0;
        return false;
    }
    *inode = info.inode();
    return true;
}

namespace {

#ifdef Q_OS_LINUX
    Q_ALWAYS_INLINE ssize_t getxattr(const char *path, const char *name, void *value, size_t size, u_int32_t, int)
    {
        return ::getxattr(path, name, value, size);
    }

    Q_ALWAYS_INLINE int setxattr(const char *path, const char *name, const void *value, size_t size, u_int32_t, int)
    {
        return ::setxattr(path, name, value, size, 0);
    }

    Q_ALWAYS_INLINE int removexattr(const char *path, const char *name, int)
    {
        return ::removexattr(path, name);
    }
#endif // Q_OS_LINUX

} // anonymous namespace

std::optional<QByteArray> FileSystem::Tags::get(const QString &path, const QString &key)
{
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QString platformKey = key;
    if (Utility::isLinux()) {
        platformKey = QStringLiteral("user.") + platformKey;
    }

    QByteArray value(MaxValueSize + 1, '\0'); // Add a NUL character to terminate a string
    auto size = getxattr(path.toUtf8().constData(), platformKey.toUtf8().constData(), value.data(), MaxValueSize, 0, 0);
    if (size != -1) {
        value.truncate(size);
        return value;
    }
#elif defined(Q_OS_WIN)
    QFile file(QStringLiteral("%1:%2").arg(path, key));
    if (file.open(QIODevice::ReadOnly)) {
        return file.readAll();
    }
#endif // Q_OS_MAC || Q_OS_LINUX

    return {};
}

OCC::Result<void, QString> FileSystem::Tags::set(const QString &path, const QString &key, const QByteArray &value)
{
    OC_ASSERT(value.size() < MaxValueSize)

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QString platformKey = key;
    if (Utility::isLinux()) {
        platformKey = QStringLiteral("user.") + platformKey;
    }

    auto result = setxattr(path.toUtf8().constData(), platformKey.toUtf8().constData(), value.constData(), value.size(), 0, 0);
    if (result != 0) {
        return QString::fromUtf8(strerror(errno));
    }

    return {};
#elif defined(Q_OS_WIN)
    QFile file(QStringLiteral("%1:%2").arg(path, key));
    if (!file.open(QIODevice::WriteOnly)) {
        return file.errorString();
    }
    auto bytesWritten = file.write(value);
    if (bytesWritten != value.size()) {
        return QStringLiteral("wrote %1 out of %2 bytes").arg(QString::number(bytesWritten), QString::number(value.size()));
    }

    return {};
#else
    return QStringLiteral("function not implemented");
#endif // Q_OS_MAC || Q_OS_LINUX
}

bool FileSystem::Tags::remove(const QString &path, const QString &key)
{
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QString platformKey = key;
    if (Utility::isLinux()) {
        platformKey = QStringLiteral("user.%1").arg(platformKey);
    }

    auto result = removexattr(path.toUtf8().constData(), platformKey.toUtf8().constData(), 0);

    return result == 0;
#elif defined(Q_OS_WIN)
    return QFile::remove(QStringLiteral("%1:%2").arg(path, key));
#else
    return false;
#endif // Q_OS_MAC || Q_OS_LINUX
}


} // namespace OCC
