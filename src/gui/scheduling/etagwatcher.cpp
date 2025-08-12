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

#include "gui/scheduling/etagwatcher.h"

#include "accountstate.h"
#include "gui/folderman.h"
#include "libsync/configfile.h"
#include "libsync/graphapi/spacesmanager.h"
#include "libsync/syncengine.h"


using namespace std::chrono_literals;

using namespace OCC;

Q_LOGGING_CATEGORY(lcEtagWatcher, "gui.scheduler.etagwatcher", QtInfoMsg)

ETagWatcher::ETagWatcher(FolderMan *folderMan, QObject *parent)
    : QObject(parent)
    , _folderMan(folderMan)
{
    connect(folderMan, &FolderMan::folderListChanged, this, [this] {
        decltype(_lastEtagJob) intersection;
        for (auto *f : _folderMan->folders()) {
            if (f->isReady()) {
                auto it = _lastEtagJob.find(f);
                if (it != _lastEtagJob.cend()) {
                    intersection[f] = std::move(it->second);
                } else {
                    intersection.emplace(f, QString());
                    connect(&f->syncEngine(), &SyncEngine::rootEtag, this, [f, this](const QString &etag, const QDateTime &time) {
                        // we don't use update etag here, as we don't want to reschedule the folder
                        _lastEtagJob[f] = etag;
                        f->accountState()->tagLastSuccessfullETagRequest(time);
                    });
                    connect(f, &Folder::spaceChanged, this, [f, this] {
                        const QString etag = Utility::normalizeEtag(f->space()->drive().getRoot().getETag());
                        // the server must provide a valid etag but there might be bugs
                        // https://github.com/owncloud/ocis/issues/7160
                        if (OC_ENSURE_NOT(etag.isEmpty())) {
                            auto &info = _lastEtagJob[f];
                            if (f->canSync() && info != etag) {
                                qCDebug(lcEtagWatcher) << u"Scheduling sync of" << f->displayName() << f->path() << u"due to an etag change";
                                info = etag;
                                _folderMan->scheduler()->enqueueFolder(f);
                            }
                        } else {
                            qCWarning(lcEtagWatcher) << u"Invalid empty etag received for" << f->displayName() << f->path();
                        }
                    });
                }
            }
        }
        _lastEtagJob = std::move(intersection);
    });
}
