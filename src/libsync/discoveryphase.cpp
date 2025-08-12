/*
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include "discoveryphase.h"
#include "discovery.h"

#include "account.h"
#include "common/asserts.h"
#include "common/checksums.h"
#include "filesystem.h"

#include <QDateTime>
#include <QFile>
#include <QLoggingCategory>
#include <QUrl>
#include <cstring>

using namespace Qt::Literals::StringLiterals;
namespace OCC {

Q_LOGGING_CATEGORY(lcDiscovery, "sync.discovery", QtInfoMsg)

/* Given a sorted list of paths ending with '/', return whether or not the given path is within one of the paths of the list*/
static bool findPathInList(const std::set<QString> &list, const QString &path)
{
    if (list.size() == 1 && *list.cbegin() == QLatin1String("/")) {
        // Special case for the case "/" is there, it matches everything
        return true;
    }

    QString pathSlash = path + QLatin1Char('/');

    // Since the list is sorted, we can do a binary search.
    // If the path is a prefix of another item or right after in the lexical order.
    auto it = std::lower_bound(list.begin(), list.end(), pathSlash);

    if (it != list.end() && *it == pathSlash) {
        return true;
    }

    if (it == list.begin()) {
        return false;
    }
    --it;
    Q_ASSERT(it->endsWith(QLatin1Char('/'))); // Folder::setSelectiveSyncBlackList makes sure of that
    return pathSlash.startsWith(*it);
}

bool DiscoveryPhase::isInSelectiveSyncBlackList(const QString &path) const
{
    if (_selectiveSyncBlackList.empty()) {
        // If there is no black list, everything is allowed
        return false;
    }

    // Block if it is in the black list
    if (findPathInList(_selectiveSyncBlackList, path)) {
        return true;
    }

    return false;
}

/* Given a path on the remote, give the path as it is when the rename is done */
QString DiscoveryPhase::adjustRenamedPath(const QString &original, SyncFileItem::Direction d) const
{
    return OCC::adjustRenamedPath(d == SyncFileItem::Down ? _renamedItemsRemote : _renamedItemsLocal, original);
}

QString adjustRenamedPath(const QHash<QString, QString> &renamedItems, const QString &original)
{
    int slashPos = original.size();
    while ((slashPos = original.lastIndexOf(QLatin1Char('/'), slashPos - 1)) > 0) {
        auto it = renamedItems.constFind(original.left(slashPos));
        if (it != renamedItems.constEnd()) {
            return *it + original.mid(slashPos);
        }
    }
    return original;
}

QPair<bool, QString> DiscoveryPhase::findAndCancelDeletedJob(const QString &originalPath)
{
    bool result = false;
    QString oldEtag;
    auto it = _deletedItem.constFind(originalPath);
    if (it != _deletedItem.cend()) {
        const auto &item = *it;
        const SyncInstructions instruction = item->instruction();
        if (instruction == CSYNC_INSTRUCTION_IGNORE && item->_type == ItemTypeVirtualFile) {
            // re-creation of virtual files count as a delete
            // restoration after a prohibited move
            // a file might be in an error state and thus gets marked as CSYNC_INSTRUCTION_IGNORE
            // after it was initially marked as CSYNC_INSTRUCTION_REMOVE
            // return true, to not trigger any additional actions on that file that could elad to dataloss
            result = true;
            oldEtag = item->_etag;
        } else {
            if (!(instruction == CSYNC_INSTRUCTION_REMOVE
                    // re-creation of virtual files count as a delete
                    || (item->_type == ItemTypeVirtualFile && instruction == CSYNC_INSTRUCTION_NEW)
                    || (item->_isRestoration && instruction == CSYNC_INSTRUCTION_NEW)
                    // we encountered an ignored error
                    || (item->_hasBlacklistEntry && instruction == CSYNC_INSTRUCTION_IGNORE))) {
                qCWarning(lcDiscovery) << u"OC_ENFORCE(FAILING)" << originalPath;
                qCWarning(lcDiscovery) << u"instruction == CSYNC_INSTRUCTION_REMOVE" << (instruction == CSYNC_INSTRUCTION_REMOVE);
                qCWarning(lcDiscovery) << u"(item->_type == ItemTypeVirtualFile && instruction == CSYNC_INSTRUCTION_NEW)"
                                       << (item->_type == ItemTypeVirtualFile && instruction == CSYNC_INSTRUCTION_NEW);
                qCWarning(lcDiscovery) << u"(item->_isRestoration && instruction == CSYNC_INSTRUCTION_NEW)"
                                       << (item->_isRestoration && instruction == CSYNC_INSTRUCTION_NEW);
                qCWarning(lcDiscovery) << u"(item->_hasBlacklistEntry && instruction == CSYNC_INSTRUCTION_IGNORE)"
                                       << (item->_hasBlacklistEntry && instruction == CSYNC_INSTRUCTION_IGNORE);
                qCWarning(lcDiscovery) << u"instruction" << instruction;
                qCWarning(lcDiscovery) << u"item->_type" << item->_type;
                qCWarning(lcDiscovery) << u"item->_isRestoration " << item->_isRestoration;
                qCWarning(lcDiscovery) << u"item->_remotePerm" << item->_remotePerm;
                OC_ENFORCE(false);
            }
            item->setInstruction(CSYNC_INSTRUCTION_NONE);
            result = true;
            oldEtag = item->_etag;
        }
        _deletedItem.erase(it);
    }
    if (auto *otherJob = _queuedDeletedDirectories.take(originalPath)) {
        oldEtag = otherJob->_dirItem->_etag;
        delete otherJob;
        result = true;
    }
    return { result, oldEtag };
}

void DiscoveryPhase::startJob(ProcessDirectoryJob *job)
{
    OC_ENFORCE(!_currentRootJob);
    connect(job, &ProcessDirectoryJob::finished, this, [this, job] {
        OC_ENFORCE(_currentRootJob == sender());
        _currentRootJob = nullptr;
        if (job->_dirItem)
            Q_EMIT itemDiscovered(job->_dirItem);
        job->deleteLater();

        // Once the main job has finished recurse here to execute the remaining
        // jobs for queued deleted directories.
        if (!_queuedDeletedDirectories.isEmpty()) {
            auto nextJob = _queuedDeletedDirectories.take(_queuedDeletedDirectories.firstKey());
            startJob(nextJob);
        } else {
            Q_EMIT finished();
        }
    });
    _currentRootJob = job;
    job->start();
}

void DiscoveryPhase::setSelectiveSyncBlackList(const QSet<QString> &list)
{
    _selectiveSyncBlackList = {list.cbegin(), list.cend()};
}

void DiscoveryPhase::setSelectiveSyncWhiteList(const QSet<QString> &list)
{
    _selectiveSyncWhiteList = {list.cbegin(), list.cend()};
}

void DiscoveryPhase::scheduleMoreJobs()
{
    auto limit = std::max(1, _syncOptions._parallelNetworkJobs());
    if (_currentRootJob && _currentlyActiveJobs < limit) {
        _currentRootJob->processSubJobs(limit - _currentlyActiveJobs);
    }
}

DiscoverySingleLocalDirectoryJob::DiscoverySingleLocalDirectoryJob(const AccountPtr &account, const QString &localPath, OCC::Vfs *vfs, QObject *parent)
    : QObject(parent)
    , QRunnable()
    , _localPath(localPath)
    , _account(account)
    , _vfs(vfs)
{
    qRegisterMetaType<QVector<LocalInfo> >("QVector<LocalInfo>");
}

// Use as QRunnable
void DiscoverySingleLocalDirectoryJob::run() {
    std::error_code ec;
    const auto localPath = FileSystem::toFilesystemPath(_localPath);
    QVector<LocalInfo> results;
    for (const auto &dirent : std::filesystem::directory_iterator{localPath, ec}) {
        ItemType type = LocalInfo::typeFromDirectoryEntry(dirent);
        if (type == ItemTypeUnsupported) {
            continue;
        }
        auto info = _vfs->statTypeVirtualFile(dirent, type);
        if (!info.isValid()) {
            continue;
        }
        results.push_back(std::move(info));
    }
    if (ec) {
        qCCritical(lcDiscovery) << u"Error while opening directory" << _localPath << ec.message();
        QString errorString = tr("Error while opening directory %1").arg(_localPath);
        if (ec.value() == EACCES) {
            errorString = tr("Directory not accessible on client, permission denied");
            Q_EMIT finishedNonFatalError(errorString);
            return;
        } else if (ec.value() == ENOENT) {
            errorString = tr("Directory not found: %1").arg(_localPath);
        } else if (ec.value() == ENOTDIR) {
            // Not a directory..
            // Just consider it is empty
            return;
        }
        Q_EMIT finishedFatalError(errorString);
        return;
    }

    Q_EMIT finished(results);
}

DiscoverySingleDirectoryJob::DiscoverySingleDirectoryJob(const AccountPtr &account, const QUrl &baseUrl, const QString &path, QObject *parent)
    : QObject(parent)
    , _subPath(path)
    , _account(account)
    , _baseUrl(baseUrl)
    , _ignoredFirst(false)
    , _isRootPath(false)
{
}

void DiscoverySingleDirectoryJob::start()
{
    // Start the actual HTTP job
    _proFindJob = new PropfindJob(_account, _baseUrl, _subPath, PropfindJob::Depth::One, this);
    _proFindJob->setProperties({
        "resourcetype"_ba,
        "getlastmodified"_ba,
        "getcontentlength"_ba,
        "getetag"_ba,
        "http://owncloud.org/ns:id"_ba,
        "http://owncloud.org/ns:permissions"_ba,
        "http://owncloud.org/ns:checksums"_ba,
    });

    QObject::connect(_proFindJob, &PropfindJob::directoryListingIterated,
        this, &DiscoverySingleDirectoryJob::directoryListingIteratedSlot);
    QObject::connect(_proFindJob, &PropfindJob::finishedWithError, this, [this] {
        QString msg = _proFindJob->errorString();
        if (_proFindJob->reply()->error() == QNetworkReply::NoError
            && !_proFindJob->reply()->header(QNetworkRequest::ContentTypeHeader).toString().contains(QLatin1String("application/xml; charset=utf-8"))) {
            msg = tr("Server error: PROPFIND reply is not XML formatted!");
        }
        Q_EMIT finished(HttpError{_proFindJob->httpStatusCode(), msg});
        deleteLater();
    });
    QObject::connect(_proFindJob, &PropfindJob::finishedWithoutError, this, &DiscoverySingleDirectoryJob::lsJobFinishedWithoutErrorSlot);
    _proFindJob->start();
}

void DiscoverySingleDirectoryJob::abort()
{
    if (_proFindJob) {
        _proFindJob->abort();
    }
}

void DiscoverySingleDirectoryJob::directoryListingIteratedSlot(const QString &file, const QMap<QString, QString> &map)
{
    if (!_ignoredFirst) {
        // The first entry is for the folder itself, we should process it differently.
        _ignoredFirst = true;
        if (auto it = Utility::optionalFind(map, QStringLiteral("permissions"))) {
            auto perm = RemotePermissions::fromServerString(it->value());
            Q_EMIT firstDirectoryPermissions(perm);
        }
    } else {
        _results.emplace_back(file, map);
    }

    //This works in concerto with the RequestEtagJob and the Folder object to check if the remote folder changed.
    if (_firstEtag.isEmpty()) {
        if (auto it = Utility::optionalFind(map, QStringLiteral("getetag"))) {
            _firstEtag = Utility::normalizeEtag(it->value()); // for directory itself
        }
    }
}

void DiscoverySingleDirectoryJob::lsJobFinishedWithoutErrorSlot()
{
    if (!_ignoredFirst) {
        // This is a sanity check, if we haven't _ignoredFirst then it means we never received any directoryListingIteratedSlot
        // which means somehow the server XML was bogus
        Q_EMIT finished(HttpError{0, tr("Server error: PROPFIND reply is not XML formatted!")});
        deleteLater();
        return;
    } else if (!_error.isEmpty()) {
        Q_EMIT finished(HttpError{0, _error});
        deleteLater();
        return;
    }
    Q_EMIT etag(_firstEtag, _proFindJob->responseQTimeStamp());
    Q_EMIT finished(_results);
    deleteLater();
}
}
