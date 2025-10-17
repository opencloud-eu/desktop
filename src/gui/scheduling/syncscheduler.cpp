/*
 * Copyright (C) by Hannah von Reth <hannah.vonreth@owncloud.com>
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

#include "gui/scheduling/syncscheduler.h"

#include "gui/folderman.h"
#include "gui/networkinformation.h"
#include "gui/scheduling/etagwatcher.h"
#include "libsync/configfile.h"
#include "libsync/syncengine.h"

#include <queue>

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;

using namespace OCC;

Q_LOGGING_CATEGORY(lcSyncScheduler, "gui.scheduler.syncscheduler", QtInfoMsg)

class FolderPriorityQueue
{
private:
    struct Element
    {
        Element() { }

        Element(Folder *f, SyncScheduler::Priority p)
            : folder(f)
            , rawFolder(f)
            , priority(p)
        {
        }

        // We don't own the folder, so it might get deleted
        QPointer<Folder> folder = nullptr;
        // raw pointer for lookup in _scheduledFolders
        Folder *rawFolder = nullptr;
        SyncScheduler::Priority priority = SyncScheduler::Priority::Low;

        friend bool operator<(const Element &lhs, const Element &rhs) { return lhs.priority < rhs.priority; }
    };


public:
    FolderPriorityQueue() = default;

    void enqueueFolder(Folder *folder, SyncScheduler::Priority priority)
    {
        const auto [it, inserted] = _scheduledFolders.emplace(folder, priority);
        if (inserted) {
            // the folder is not yet scheduled
            _queue.emplace(folder, priority);
        } else {
            // if the new priority is higher we need to rebuild the queue
            if (priority > it->second) {
                // we need to reorder the queue
                // this is expensive
                decltype(_queue) out;
                for (; !_queue.empty(); _queue.pop()) {
                    const auto &tmp = _queue.top();
                    if (tmp.folder != folder) {
                        out.push(std::move(tmp));
                    } else {
                        out.emplace(folder, priority);
                    }
                }
                _queue = std::move(out);
                // replace the the old priority
                _scheduledFolders[folder] = priority;
            }
        }
    }

    auto empty() { return _queue.empty(); }
    auto size() { return _queue.size(); }

    std::pair<Folder *, SyncScheduler::Priority> pop()
    {
        Element out;
        while (!_queue.empty() && !out.folder) {
            // could be a nullptr by now
            out = _queue.top();
            [[maybe_unused]] auto removed = _scheduledFolders.erase(_queue.top().rawFolder);
            Q_ASSERT(removed = 1);
            _queue.pop();
        }
        return std::make_pair(out.folder, out.priority);
    }

    Folder *peek() const
    {
        if (_queue.empty()) {
            return nullptr;
        }
        return _queue.top().folder;
    }

    [[nodiscard]] bool contains(Folder *folder) const { return _scheduledFolders.contains(folder); }

private:
    // the actual queue
    std::priority_queue<Element> _queue;
    // helper container to ensure we don't enqueue a Folder multiple times
    std::unordered_map<Folder *, SyncScheduler::Priority> _scheduledFolders;
};

SyncScheduler::SyncScheduler(FolderMan *parent)
    : QObject(parent)
    , _pauseSyncWhenMetered(ConfigFile().pauseSyncWhenMetered())
    , _queue(new FolderPriorityQueue)
{
    new ETagWatcher(parent, this);

    // Normal syncs are performed incremental but when fullLocalDiscoveryInterval times out
    // a complete local discovery is performed.
    // This timer here triggers a sync independent of etag changes on the server.
    auto *fullLocalDiscoveryTimer = new QTimer(this);
    fullLocalDiscoveryTimer->setInterval(ConfigFile().fullLocalDiscoveryInterval() + 2min);
    connect(fullLocalDiscoveryTimer, &QTimer::timeout, this, [parent, this] {
        for (auto *f : parent->folders()) {
            if (f->isReady() && f->accountState()->state() == AccountState::State::Connected) {
                enqueueFolder(f);
            }
        }
    });
    fullLocalDiscoveryTimer->start();
}


SyncScheduler::~SyncScheduler()
{
    delete _queue;
}

void SyncScheduler::enqueueFolder(Folder *folder, Priority priority)
{
    if (!folder->canSync()) {
        qCWarning(lcSyncScheduler) << u"Cannot enqueue folder" << folder->path() << u": folder is marked as cannot sync";
        return;
    }

    qCInfo(lcSyncScheduler) << u"Enqueue" << folder->path() << priority << u"QueueSize:" << _queue->size() << u"scheduler is active:" << isRunning()
                            << (_currentSync ? u"current sync %1 %2 for %3"_s.arg(
                                                   _currentSync->path(), QDebug::toString(_currentSync->syncState()), QDebug::toString(_syncTimer))
                                             : u"no current sync running"_s);

    // TODO: setSyncState should not be public...
    if (folder != _currentSync) {
        // don't override the state of the currently syncing folder
        folder->setSyncState(SyncResult::Queued);
    }
    _queue->enqueueFolder(folder, priority);

    if (!_currentSync) {
        startNext();
    } else if (!OC_ENSURE(_currentSync->isSyncRunning())) {
        // the sync is not running, this should never happen, enqueue next
        _currentSync.clear();
        startNext();
    }
}

bool SyncScheduler::isFolderQueued(Folder *folder) const
{
    return _queue->contains(folder);
}

void SyncScheduler::startNext()
{
    if (!_running) {
        qCInfo(lcSyncScheduler) << u"Scheduler is paused, next sync is not started";
        return;
    }

    if (!_currentSync.isNull()) {
        qCInfo(lcSyncScheduler) << u"Another sync is already running, waiting for that to finish before starting a new sync";
        return;
    }

    Priority syncPriority = Priority::Low;
    QSet<Folder *> seen;
    seen.reserve(_queue->size());
    while (!_currentSync) {
        if (_queue->empty()) {
            qCInfo(lcSyncScheduler) << u"Queue is empty, no sync to start";
            return;
        }
        if (auto *folder = _queue->peek()) {
            if (seen.contains(folder)) {
                // We have seen this folder already, it means all folders in the queue are not syncable
                qCInfo(lcSyncScheduler) << u"Skipping sync of" << folder->path() << u"because it was already tried";
                return;
            }
        } else {
            std::ignore = _queue->pop();
            continue;
        }
        std::tie(_currentSync, syncPriority) = _queue->pop();
        seen.insert(_currentSync);
        // If the folder is deleted in the meantime, we skip it
        if (!_currentSync->canSync()) {
            qCInfo(lcSyncScheduler) << u"Skipping sync of" << _currentSync->path() << u"because it is not ready";
            enqueueFolder(_currentSync, syncPriority);
            _currentSync.clear();
            continue;
        }
    }

    if (_pauseSyncWhenMetered && NetworkInformation::instance()->isMetered()) {
        if (syncPriority == Priority::High) {
            qCInfo(lcSyncScheduler) << u"Scheduler is paused due to metered internet connection, BUT next sync is HIGH priority, so allow sync to start";
        } else {
            enqueueFolder(_currentSync, syncPriority);
            qCInfo(lcSyncScheduler) << u"Scheduler is paused due to metered internet connection, next sync is not started";
            return;
        }
    }

    connect(
        _currentSync, &Folder::syncFinished, this,
        [sync = _currentSync, this](const SyncResult &result) {
            qCInfo(lcSyncScheduler) << u"Sync finished for" << sync->path() << u"with status" << result.status();
            _currentSync.clear();
            startNext();
        },
        Qt::SingleShotConnection);
    qCInfo(lcSyncScheduler) << u"Starting sync for" << _currentSync->path() << u"QueueSize:" << _queue->size();
    _currentSync->startSync();
    _syncTimer.reset();
}

void SyncScheduler::start()
{
    _running = true;
    // give aborted syncs a chance to trigger the above slots
    QTimer::singleShot(0, this, &SyncScheduler::startNext);
    Q_EMIT isRunningChanged();
}

void SyncScheduler::stop()
{
    _running = false;
    Q_EMIT isRunningChanged();
}

bool SyncScheduler::hasCurrentRunningSyncRunning() const
{
    return _currentSync;
}

Folder *SyncScheduler::currentSync()
{
    return _currentSync;
}

void SyncScheduler::terminateCurrentSync(const QString &reason)
{
    if (_currentSync && _currentSync->isReady()) {
        qCInfo(lcSyncScheduler) << u"folder " << _currentSync->path() << u" Terminating!";
        if (OC_ENSURE(_currentSync->syncEngine().isSyncRunning())) {
            _currentSync->syncEngine().abort(reason);
        }
        _currentSync.clear();
    }
}

void SyncScheduler::setPauseSyncWhenMetered(bool pauseSyncWhenMetered)
{
    _pauseSyncWhenMetered = pauseSyncWhenMetered;
    if (!pauseSyncWhenMetered) {
        startNext();
    }
}

bool SyncScheduler::isRunning() const
{
    return _running;
}
