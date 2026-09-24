/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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

#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>

#include "folder.h"
#include "folderwatcher_linux.h"

#include <QObject>
#include <QStringList>
#include <QVarLengthArray>

using namespace Qt::Literals::StringLiterals;

namespace OCC {

FolderWatcherPrivate::FolderWatcherPrivate(FolderWatcher *p, const QString &path)
    : _parent(p)
    , _folder(path)
{
    _fd = inotify_init1(IN_CLOEXEC);
    if (_fd != -1) {
        _socket = new QSocketNotifier(_fd, QSocketNotifier::Read);
        connect(_socket, &QSocketNotifier::activated, this, &FolderWatcherPrivate::slotReceivedNotification);
    } else {
        qCWarning(lcFolderWatcher) << u"inotify_init1() failed: " << strerror(errno);
    }

    QMetaObject::invokeMethod(this, [path, this] { slotAddFolderRecursive(path); });
}

FolderWatcherPrivate::~FolderWatcherPrivate()
{
    if (_socket) {
        _socket->setEnabled(false);
        _socket->deleteLater();
    }
    removeFoldersBelow(_folder);
    if (_fd != -1) {
        close(_fd);
        _fd = -1;
    }
}
// attention: result list passed by reference!
bool FolderWatcherPrivate::findFoldersBelow(const QDir &dir, QStringList &fullList)
{
    if (!dir.exists()) {
        qCDebug(lcFolderWatcher) << u"      - non existing path coming in: " << dir.absolutePath();
        return false;
    } else if (!dir.isReadable()) {
        qCDebug(lcFolderWatcher) << u"      - path without read permissions coming in: " << dir.absolutePath();
        return false;
    }

    const QDir::Filters filter = QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden;

    bool ok = true;
    for (const QString &path : dir.entryList({ QStringLiteral("*") }, filter)) {
        const QString fullPath(dir.path() + QLatin1String("/") + path);
        fullList.append(fullPath);
        ok &= findFoldersBelow(QDir(fullPath), fullList);
    }
    return ok;
}

void FolderWatcherPrivate::inotifyRegisterPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    int wd = inotify_add_watch(_fd, path.toUtf8().constData(),
        IN_CLOSE_WRITE | IN_ATTRIB | IN_MOVE | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_ONLYDIR);
    if (wd != -1) {
        _watchToPath.insert(wd, path);
        _pathToWatch.insert(path, wd);
    } else {
        // If we're running out of memory or inotify watches, become unreliable.
        if (_parent->_isReliable && (errno == ENOMEM || errno == ENOSPC)) {
            _parent->_isReliable = false;
            Q_EMIT _parent->becameUnreliable(tr("This problem usually happens when the inotify watches are exhausted. "
                                                "Check the FAQ for details."));
        }
    }
}

void FolderWatcherPrivate::slotAddFolderRecursive(const QString &path)
{
    if (_pathToWatch.contains(path)) {
        return;
    }

    int subdirCount = 0;
    qCDebug(lcFolderWatcher) << u"(+) Watcher:" << path;

    QDir inPath(path);
    inotifyRegisterPath(inPath.absolutePath());

    QStringList allSubfolders;
    if (!findFoldersBelow(QDir(path), allSubfolders)) {
        qCWarning(lcFolderWatcher).nospace() << u"Could not traverse all sub folders of '" << path << u"'";
    }

    for (const auto &subfolder : std::as_const(allSubfolders)) {
        QDir folder(subfolder);
        if (folder.exists() && !_pathToWatch.contains(folder.absolutePath())) {
            ++subdirCount;
            if (_parent->pathIsIgnored(subfolder)) {
                qCDebug(lcFolderWatcher) << u"* Not adding" << folder.path();
                continue;
            }
            inotifyRegisterPath(folder.absolutePath());
        } else {
            qCDebug(lcFolderWatcher) << u"    `-> discarded:" << folder.path();
        }
    }

    if (subdirCount > 0) {
        qCDebug(lcFolderWatcher) << u"    `-> and" << subdirCount << u"subdirectories";
    }

    qCDebug(lcFolderWatcher) << u"    --- Finished scanning" << path;
}

void FolderWatcherPrivate::slotReceivedNotification(int fd)
{
    qsizetype readBytes;
    QVarLengthArray<char, 2048> buffer(2048);

    while (true) {
        readBytes = read(fd, buffer.data(), buffer.size());
        auto error = errno;
        /**
          * From inotify documentation:
          *
          * The behavior when the buffer given to read(2) is too
          * small to return information about the next event
          * depends on the kernel version: in kernels  before 2.6.21,
          * read(2) returns 0; since kernel 2.6.21, read(2) fails with
          * the error EINVAL.
          */
        if (readBytes < 0 && error == EINVAL) {
            // double the buffer size
            buffer.resize(buffer.size() * 2);
            /* and try again ... */
        } else if (readBytes <= 0) {
            qCWarning(lcFolderWatcher) << "Failed to read from inotify fd: " << strerror(error);
            return;
        } else {
            // successful read
            break;
        }
    }

    QSet<QString> paths;
    paths.reserve(readBytes / static_cast<qsizetype>(sizeof(inotify_event)));
    // iterate over events in buffer

    for (auto *event = reinterpret_cast<inotify_event *>(buffer.data()); // start at the beginning of the buffer
        reinterpret_cast<char *>(event + 1) <= buffer.data() + readBytes; // check that we still have at least sizeof(inotify_event) left in the buffer
        event = reinterpret_cast<inotify_event *>(reinterpret_cast<char *>(event + 1) + (event ? event->len : 0))) { // skip over the header and event-payload


        if (event == nullptr) {
            qCDebug(lcFolderWatcher) << u"NULL event";
            continue;
        }
        // read the null terminated string, max length == event->len
        const auto fileName = QString::fromUtf8(event->name, static_cast<qsizetype>(qstrnlen(event->name, event->len)));
        if (event->wd <= -1) {
            qCWarning(lcFolderWatcher) << u"NULL watch descriptor" << event->wd << fileName;
            continue;
        }
        if (event->len == 0) {
            if (event->mask & IN_IGNORED) {
                if (auto path = Utility::optionalFind(_watchToPath, event->wd)) {
                    removeFoldersBelow(path->value());
                }
            }
            continue;
        }
        // Filter out journal changes - redundant with filtering in FolderWatcher::pathIsIgnored.
        if (fileName.startsWith(".sync_"_L1)) {
            continue;
        }
        if (auto path = Utility::optionalFind(_watchToPath, event->wd)) {
            const QString p = Utility::ensureTrailingSlash(path->value()) + fileName;
            paths.insert(p);

            if ((event->mask & (IN_MOVED_TO | IN_CREATE)) && QFileInfo(p).isDir() && !_parent->pathIsIgnored(p)) {
                slotAddFolderRecursive(p);
            }
            if (event->mask & (IN_MOVED_FROM | IN_DELETE)) {
                removeFoldersBelow(p);
            }
        } else {
            qCWarning(lcFolderWatcher) << "Received event for unknown watch descriptor: " << event->wd;
        }
    }
    if (!paths.isEmpty()) {
        _parent->addChanges(std::move(paths));
    }
}

void FolderWatcherPrivate::removeFoldersBelow(const QString &path)
{
    auto it = _pathToWatch.lowerBound(path);
    if (it == _pathToWatch.end())
        return;

    const QString pathSlash = Utility::ensureTrailingSlash(path);

    // Remove the entry and all subentries
    while (it != _pathToWatch.end()) {
        auto itPath = it.key();
        if (!itPath.startsWith(path))
            break;
        if (itPath != path && !itPath.startsWith(pathSlash)) {
            // order is 'foo', 'foo bar', 'foo/bar'
            ++it;
            continue;
        }

        const auto wid = it.value();
        inotify_rm_watch(_fd, wid);
        _watchToPath.remove(wid);
        it = _pathToWatch.erase(it);
        qCDebug(lcFolderWatcher) << u"Removed watch for" << itPath << wid;
    }
}

} // ns mirall
