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

// event masks
#include "folderwatcher.h"

#include <cstdint>

#include <QFlags>
#include <QTimer>

#if defined(Q_OS_WIN)
#include "folderwatcher_win.h"
#elif defined(Q_OS_MAC)
#include "folderwatcher_mac.h"
#elif defined(Q_OS_UNIX)
#include "folderwatcher_linux.h"
#endif

#include "folder.h"
#include "filesystem.h"

using namespace std::chrono_literals;

namespace {
constexpr auto notificationTimeoutC = 10s;
}

namespace OCC {

Q_LOGGING_CATEGORY(lcFolderWatcher, "gui.folderwatcher", QtInfoMsg)

FolderWatcher::FolderWatcher(Folder *folder)
    : QObject(folder)
    , _folder(folder)
{
    _timer.setInterval(notificationTimeoutC);
    _timer.setSingleShot(true);
    connect(&_timer, &QTimer::timeout, this, [this] {
        auto paths = popChangeSet();
        Q_ASSERT(!paths.empty());
        if (!paths.isEmpty()) {
            qCInfo(lcFolderWatcher) << u"Detected changes in paths:" << paths;
            Q_EMIT pathChanged(paths);
        }
    });
}

FolderWatcher::~FolderWatcher()
{
}

void FolderWatcher::init(const QString &root)
{
    Q_ASSERT(!_d);
    _d.reset(new FolderWatcherPrivate(this, root));
}

bool FolderWatcher::pathIsIgnored(const QString &path) const
{
    Q_ASSERT(!path.isEmpty());
    Q_ASSERT(_folder);
    if (!QFileInfo::exists(path)) {
        std::error_code ec;
        const auto relPath =
            FileSystem::fromFilesystemPath(std::filesystem::proximate(FileSystem::toFilesystemPath(path), FileSystem::toFilesystemPath(_folder->path()), ec));
        Q_ASSERT(!ec);
        if (ec) {
            qCDebug(lcFolderWatcher) << u"* Failed to lookup record for path:" << path << u"error:" << ec.message();
            return false;
        }
        if (auto record = _folder->journalDb()->getFileRecord(relPath); record.isValid()) {
            // we know about the file, it got removed, we should not ignore that
            qCDebug(lcFolderWatcher) << u"* Not ignoring removed file" << path;
            return false;
        }
        // probably a temporary file that no longer exists
        qCDebug(lcFolderWatcher) << u"* Ignoring file" << path << u"It no longer exists and we don't have it in the database.";
        return true;
    }
    if (_folder->isFileExcludedAbsolute(path) && !Utility::isConflictFile(path)) {
        qCDebug(lcFolderWatcher) << u"* Ignoring file" << path;
        return true;
    }
    return false;
}

bool FolderWatcher::isReliable() const
{
    return _isReliable;
}

int FolderWatcher::testLinuxWatchCount() const
{
#ifdef Q_OS_LINUX
    return _d->testWatchCount();
#else
    return -1;
#endif
}

void FolderWatcher::addChanges(QSet<QString> &&paths)
{
    Q_ASSERT(thread() == QThread::currentThread());
    // the timer must be inactive if we haven't received changes yet
    Q_ASSERT((!_timer.isActive() && _changeSet.isEmpty()) || !_changeSet.isEmpty());
    Q_ASSERT(!paths.isEmpty());
    // ------- handle ignores:
    auto it = paths.cbegin();
    while (it != paths.cend()) {
        if (pathIsIgnored(*it)) {
            it = paths.erase(it);
        } else {
            ++it;
        }
    }
    if (!paths.isEmpty()) {
        _changeSet.unite(paths);
        if (!_timer.isActive()) {
            _timer.start();
            // promote that we will report changes once _timer times out
            Q_EMIT changesDetected();
        }
    }
}

QSet<QString> FolderWatcher::popChangeSet()
{
    // stop the timer as we pop all queued changes
    _timer.stop();
    return std::move(_changeSet);
}

} // namespace OCC
