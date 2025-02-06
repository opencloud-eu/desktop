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
            qCInfo(lcFolderWatcher) << "Detected changes in paths:" << paths;
            Q_EMIT pathChanged(paths);
        }
    });
}

FolderWatcher::~FolderWatcher()
{
}

void FolderWatcher::init(const QString &root)
{
    _d.reset(new FolderWatcherPrivate(this, root));
}

bool FolderWatcher::pathIsIgnored(const QString &path) const
{
    Q_ASSERT(!path.isEmpty());
    Q_ASSERT(_folder);
    if (_folder->isFileExcludedAbsolute(path) && !Utility::isConflictFile(path)) {
        qCDebug(lcFolderWatcher) << "* Ignoring file" << path;
        return true;
    }
    return false;
}

bool FolderWatcher::isReliable() const
{
    return _isReliable;
}

void FolderWatcher::startNotificatonTest(const QString &path)
{
#ifdef Q_OS_MAC
    // Testing the folder watcher on OSX is harder because the watcher
    // automatically discards changes that were performed by our process.
    // It would still be useful to test but the OSX implementation
    // is deferred until later.
    return;
#endif

    Q_ASSERT(_testNotificationPath.isEmpty());
    Q_ASSERT(!path.isEmpty());
    _testNotificationPath = path;

    // Don't do the local file modification immediately:
    // wait for FolderWatchPrivate::_ready
    startNotificationTestWhenReady();
}

void FolderWatcher::startNotificationTestWhenReady()
{
    if (!_testNotificationPath.isEmpty()) {
        // we already received the notification
        return;
    }
    if (!_d->isReady()) {
        QTimer::singleShot(1s, this, &FolderWatcher::startNotificationTestWhenReady);
        return;
    }

    if (OC_ENSURE(QFile::exists(_testNotificationPath))) {
        const auto mtime = FileSystem::getModTime(_testNotificationPath);
        FileSystem::setModTime(_testNotificationPath, mtime + 1);
    } else {
        QFile f(_testNotificationPath);
        f.open(QIODevice::WriteOnly | QIODevice::Append);
    }

    QTimer::singleShot(notificationTimeoutC + 5s, this, [this]() {
        if (!_testNotificationPath.isEmpty())
            Q_EMIT becameUnreliable(tr("The watcher did not receive a test notification."));
        _testNotificationPath.clear();
    });
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
    Q_ASSERT(!_timer.isActive() && _changeSet.isEmpty() || !_changeSet.isEmpty());
    Q_ASSERT(!paths.isEmpty());
    // ------- handle ignores:
    auto it = paths.cbegin();
    while (it != paths.cend()) {
        // we cause a file change from time to time to check whether the folder watcher works as expected
        if (!_testNotificationPath.isEmpty() && Utility::fileNamesEqual(*it, _testNotificationPath)) {
            _testNotificationPath.clear();
        }
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
    return std::move(_changeSet);
}

} // namespace OCC
