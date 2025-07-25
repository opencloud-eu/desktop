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

#ifndef MIRALL_FOLDERWATCHER_WIN_H
#define MIRALL_FOLDERWATCHER_WIN_H

#include "libsync/common/utility_win.h"

#include <QThread>

namespace OCC {

class FolderWatcher;
class FolderWatcherPrivate;

/**
 * @brief The WatcherThread class
 * @ingroup gui
 */
class WatcherThread : public QThread
{
    Q_OBJECT
public:
    WatcherThread(FolderWatcherPrivate *parent, const QString &path);
    ~WatcherThread() override;

    void stop();

protected:
    enum class WatchChanges {
        Done,
        NeedBiggerBuffer,
        Error,
    };

    void run() override;
    WatchChanges watchChanges(size_t fileNotifyBufferSize);
    void processEntries(FILE_NOTIFY_INFORMATION *curEntry);
    void closeHandle();

Q_SIGNALS:
    void changed(QSet<QString> path);
    void lostChanges();

private:
    FolderWatcherPrivate *_parent;
    const QString _path;
    const QString _longPath;
    Utility::Handle _directory;
    HANDLE _resultEvent;
    HANDLE _stopEvent;
};

/**
 * @brief Windows implementation of FolderWatcher
 * @ingroup gui
 */
class FolderWatcherPrivate : public QObject
{
    Q_OBJECT
public:
    FolderWatcherPrivate(FolderWatcher *p, const QString &path);
    ~FolderWatcherPrivate() override;

    /// Set to non-zero once the WatcherThread is capturing events.
    bool isReady() const
    {
        return _ready;
    }

private:
    FolderWatcher *_parent;
    QScopedPointer<WatcherThread> _thread;
    bool _ready = false;
    friend class WatcherThread;
};
}

#endif // MIRALL_FOLDERWATCHER_WIN_H
