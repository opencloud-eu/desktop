/*
 * Copyright (C) by Dominik Schmidt <dschmidt@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "vfs.h"
#include "common/filesystembase.h"
#include "common/version.h"
#include "plugin.h"
#include "syncjournaldb.h"
#include "syncfileitem.h"

#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>
#include <QPluginLoader>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
using namespace OCC;
using namespace Qt::Literals::StringLiterals;

Q_LOGGING_CATEGORY(lcVfs, "sync.vfs", QtInfoMsg)

QDebug operator<<(QDebug debug, const OCC::CallBackContext &context)
{
    QDebugStateSaver saver(debug);
    debug.setAutoInsertSpaces(false);
    debug << u"cfapiCallback(" << context.path << u", " << context.requestHexId();
    for (const auto &[k, v] : context.extraArgs.asKeyValueRange()) {
        debug << u", ";
        debug.noquote() << k << u"=";
        debug.quote() << v;
    };
    debug << u")";
    return debug.maybeSpace();
}

class OCC::VfsApiPrivate
{
public:
    QMap<uint64_t, HydrationJob *> hydrationJobs;
};

Vfs::Vfs(QObject *parent)
    : QObject(parent)
    , d(new VfsApiPrivate)
{
}

Vfs::~Vfs() = default;

Optional<Vfs::Mode> Vfs::modeFromString(const QString &str)
{
    // Note: Strings are used for config and must be stable
    // keep in sync with: QString Utility::enumToString(Vfs::Mode mode)
    if (str == QLatin1String("off")) {
        return Off;
    } else if (str == QLatin1String("cfapi")) {
        return WindowsCfApi;
    } else if (str == QLatin1String("xattr")) {
        return XAttr;
    }
    return {};
}

template <>
QString Utility::enumToString(Vfs::Mode mode)
{
    // Note: Strings are used for config and must be stable
    // keep in sync with: Optional<Vfs::Mode> Vfs::modeFromString(const QString &str)
    switch (mode) {
    case Vfs::Mode::WindowsCfApi:
        return QStringLiteral("cfapi");
    case Vfs::Mode::Off:
        return QStringLiteral("off");
    case Vfs::Mode::XAttr:
        return QStringLiteral("xattr");
    }
    Q_UNREACHABLE();
}

Result<void, QString> Vfs::checkAvailability(const QString &path, Vfs::Mode mode)
{
#ifdef Q_OS_WIN
    const auto canonicalPath = [path] {
        QFileInfo info(path);
        if (info.exists()) {
            return info.canonicalFilePath();
        } else {
            return info.absoluteFilePath();
        }
    }();
    const auto fileSystem = FileSystem::fileSystemForPath(canonicalPath);
    if (fileSystem.startsWith("ReFS"_L1, Qt::CaseInsensitive)) {
        return tr("ReFS is currently not supported.");
    }
    if (mode == Mode::WindowsCfApi) {
        if (QDir(canonicalPath).isRoot()) {
            return tr("The Virtual filesystem feature does not support a drive as sync root");
        }

        if (!fileSystem.startsWith("NTFS"_L1, Qt::CaseInsensitive)) {
            return tr("The Virtual filesystem feature requires a NTFS file system, %1 is using %2").arg(path, fileSystem);
        }
        const auto type = GetDriveTypeW(reinterpret_cast<const wchar_t *>(canonicalPath.mid(0, 3).utf16()));
        if (type == DRIVE_REMOTE) {
            return tr("The Virtual filesystem feature is not supported on network drives");
        }
    }
#else
    Q_UNUSED(mode);
    Q_UNUSED(path);
#endif
    return {};
}

void Vfs::start(const VfsSetupParams &params)
{
    _setupParams = std::make_unique<VfsSetupParams>(params);
    startImpl(this->params());
}

void Vfs::wipeDehydratedVirtualFiles()
{
    if (mode() == Vfs::Mode::Off) {
        // there are no placeholders
        return;
    }
    _setupParams->journal->getFilesBelowPath(QString(), [&](const SyncJournalFileRecord &rec) {
        // only handle dehydrated files
        if (rec.type() != ItemTypeVirtualFile && rec.type() != ItemTypeVirtualFileDownload) {
            return;
        }
        const QString relativePath = rec.path();
        qCDebug(lcVfs) << u"Removing db record for dehydrated file" << relativePath;
        _setupParams->journal->deleteFileRecord(relativePath);

        // If the local file is a dehydrated placeholder, wipe it too.
        // Otherwise leave it to allow the next sync to have a new-new conflict.
        const QString absolutePath = _setupParams->filesystemPath + relativePath;
        if (QFile::exists(absolutePath)) {
            // according to our db this is a dehydrated file, check it  to be sure
            if (isDehydratedPlaceholder(absolutePath)) {
                qCDebug(lcVfs) << u"Removing local dehydrated placeholder" << relativePath;
                FileSystem::remove(absolutePath);
            }
        }
    });

    _setupParams->journal->forceRemoteDiscoveryNextSync();

    // Postcondition: No ItemTypeVirtualFile / ItemTypeVirtualFileDownload left in the db.
    // But hydrated placeholders may still be around.
}

// ======================= Hydration ================

HydrationJob *Vfs::findHydrationJob(int64_t requestId) const
{
    // Find matching hydration job for request id
    return d->hydrationJobs.value(requestId);
}

void Vfs::cancelHydration(const OCC::CallBackContext &context)
{
    // Find matching hydration job for request id
    const auto hydrationJob = findHydrationJob(context.requestId);
    // If found, cancel it
    if (hydrationJob) {
        qCInfo(lcVfs) << u"Cancel hydration" << hydrationJob->context();
        hydrationJob->cancel();
    }
}

void Vfs::requestHydration(const OCC::CallBackContext &context, qint64 requestedFileSize)
{
    qCInfo(lcVfs) << u"Received request to hydrate" << context.path << context.fileId;
    const auto root = QDir::toNativeSeparators(params().filesystemPath);
    Q_ASSERT(context.path.startsWith(root));


    // Set in the database that we should download the file
    SyncJournalFileRecord record;
    params().journal->getFileRecordsByFileId(context.fileId, [&record](const auto &r) {
        Q_ASSERT(!record.isValid());
        record = r;
    });
    if (!record.isValid()) {
        qCInfo(lcVfs) << u"Couldn't hydrate, did not find file in db";
        Q_ASSERT(false); // how did we end up here if it's not  a cloud file
        Q_EMIT hydrationRequestFailed(context.requestId);
        Q_EMIT needSync();
        return;
    }

    bool isNotVirtualFileFailure = false;
    if (!record.isVirtualFile()) {
        if (isDehydratedPlaceholder(context.path)) {
            qCWarning(lcVfs) << u"Hydration requested for a placeholder file that is incorrectly not marked as a virtual file in the local database. "
                                  u"Attempting to correct this inconsistency...";
            auto item = SyncFileItem::fromSyncJournalFileRecord(record);
            item->_type = ItemTypeVirtualFileDownload;
            isNotVirtualFileFailure = !params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
        } else {
            isNotVirtualFileFailure = true;
        }
    }
    if (requestedFileSize != record.size()) {
        // we are out of sync
        qCWarning(lcVfs) << u"The db size and the placeholder meta data are out of sync, request resync";
        Q_ASSERT(false); // this should not happen
        Q_EMIT hydrationRequestFailed(context.requestId);
        Q_EMIT needSync();
        return;
    }

    if (isNotVirtualFileFailure) {
        qCWarning(lcVfs) << u"Couldn't hydrate, the file is not virtual";
        Q_ASSERT(false); // this should not happen
        Q_EMIT hydrationRequestFailed(context.requestId);
        Q_EMIT needSync();
        return;
    }

    // All good, let's hydrate now
    scheduleHydrationJob(context, std::move(record));
}

void Vfs::scheduleHydrationJob(const OCC::CallBackContext &context, SyncJournalFileRecord &&record)
{
    // after a local move, the remotePath and the targetPath might not match
    if (findHydrationJob(context.requestId)) {
        qCWarning(lcVfs) << u"The OS submitted again a hydration request which is already on-going" << context;
        Q_EMIT hydrationRequestFailed(context.requestId);
        return;
    }
    Q_ASSERT(!std::any_of(std::cbegin(d->hydrationJobs), std::cend(d->hydrationJobs),
        [=](HydrationJob *job) { return job->requestId() == context.requestId || job->localFilePathAbs() == context.path; }));
    auto job = new HydrationJob(context);
    job->setAccount(params().account);
    job->setRemoteSyncRootPath(params().baseUrl());
    job->setLocalRoot(params().filesystemPath);
    job->setJournal(params().journal);
    job->setRemoteFilePathRel(record.path());
    job->setRecord(std::move(record));
    connect(job, &HydrationJob::finished, this, &Vfs::onHydrationJobFinished);
    d->hydrationJobs.insert(context.requestId, job);
    job->start();
    Q_EMIT hydrationRequestReady(context.requestId);
}

void Vfs::onHydrationJobFinished(HydrationJob *job)
{
    Q_ASSERT(findHydrationJob(job->requestId()));
    qCInfo(lcVfs) << u"Hydration job finished" << job->requestId() << job->localFilePathAbs() << job->status();
    Q_EMIT hydrationRequestFinished(job->requestId());
    if (!job->errorString().isEmpty()) {
        qCWarning(lcVfs) << job->errorString();
    }
}

HydrationJob::Status Vfs::finalizeHydrationJob(int64_t requestId)
{
    // Find matching hydration job for request id
    if (const auto hydrationJob = findHydrationJob(requestId)) {
        qCDebug(lcVfs) << u"Finalize hydration job" << hydrationJob->context();
        hydrationJob->finalize(this);
        d->hydrationJobs.take(hydrationJob->requestId());
        hydrationJob->deleteLater();
        return hydrationJob->status();
    }
    qCCritical(lcVfs) << u"Failed to finalize hydration job" << requestId << u". Job not found.";
    return HydrationJob::Status::Error;
}

// ===============================================================

Q_LOGGING_CATEGORY(lcPlugin, "sync.plugins", QtInfoMsg)

OCC::VfsPluginManager *OCC::VfsPluginManager::_instance = nullptr;

bool OCC::VfsPluginManager::isVfsPluginAvailable(Vfs::Mode mode) const
{
    {
        auto result = _pluginCache.constFind(mode);
        if (result != _pluginCache.cend()) {
            return *result;
        }
    }
    const bool out = [mode] {
        const QString name = Utility::enumToString(mode);
        if (!OC_ENSURE_NOT(name.isEmpty())) {
            return false;
        }
        auto pluginPath = pluginFileName(QStringLiteral("vfs"), name);
        QPluginLoader loader(pluginPath);

        auto basemeta = loader.metaData();
        if (basemeta.isEmpty() || !basemeta.contains(QStringLiteral("IID"))) {
            qCDebug(lcPlugin) << u"Plugin doesn't exist:" << loader.fileName() << u"LibraryPath:" << QCoreApplication::libraryPaths();
            return false;
        }
        if (basemeta[QStringLiteral("IID")].toString() != QLatin1String("eu.opencloud.PluginFactory")) {
            qCWarning(lcPlugin) << u"Plugin has wrong IID" << loader.fileName() << basemeta[QStringLiteral("IID")];
            return false;
        }

        auto metadata = basemeta[QStringLiteral("MetaData")].toObject();
        if (metadata[QStringLiteral("type")].toString() != QLatin1String("vfs")) {
            qCWarning(lcPlugin) << u"Plugin has wrong type" << loader.fileName() << metadata[QStringLiteral("type")];
            return false;
        }
        if (metadata[QStringLiteral("version")].toString() != OCC::Version::version().toString()) {
            qCWarning(lcPlugin) << u"Plugin has wrong version" << loader.fileName() << metadata[QStringLiteral("version")];
            return false;
        }

        // Attempting to load the plugin is essential as it could have dependencies that
        // can't be resolved and thus not be available after all.
        if (!loader.load()) {
            qCWarning(lcPlugin) << u"Plugin failed to load:" << loader.errorString();
            return false;
        }

        return true;
    }();
    _pluginCache[mode] = out;
    return out;
}

Vfs::Mode OCC::VfsPluginManager::bestAvailableVfsMode() const
{
    if (isVfsPluginAvailable(Vfs::WindowsCfApi)) {
        return Vfs::WindowsCfApi;
    } else if (isVfsPluginAvailable(Vfs::XAttr)) {
        return Vfs::XAttr;
    } else if (isVfsPluginAvailable(Vfs::Off)) {
        return Vfs::Off;
    }
    Q_UNREACHABLE();
}

std::unique_ptr<Vfs> OCC::VfsPluginManager::createVfsFromPlugin(Vfs::Mode mode) const
{
    auto name = Utility::enumToString(mode);
    if (name.isEmpty())
        return nullptr;
    auto pluginPath = pluginFileName(QStringLiteral("vfs"), name);

    if (!isVfsPluginAvailable(mode)) {
        qCCritical(lcPlugin) << u"Could not load plugin: not existant or bad metadata" << pluginPath;
        return nullptr;
    }

    QPluginLoader loader(pluginPath);
    auto plugin = loader.instance();
    if (!plugin) {
        qCCritical(lcPlugin) << u"Could not load plugin" << pluginPath << loader.errorString();
        return nullptr;
    }

    auto factory = qobject_cast<PluginFactory *>(plugin);
    if (!factory) {
        qCCritical(lcPlugin) << u"Plugin" << loader.fileName() << u"does not implement PluginFactory";
        return nullptr;
    }

    auto vfs = std::unique_ptr<Vfs>(qobject_cast<Vfs *>(factory->create(nullptr)));
    if (!vfs) {
        qCCritical(lcPlugin) << u"Plugin" << loader.fileName() << u"does not create a Vfs instance";
        return nullptr;
    }

    qCInfo(lcPlugin) << u"Created VFS instance from plugin" << pluginPath;
    return vfs;
}

const VfsPluginManager &VfsPluginManager::instance()
{
    if (!_instance) {
        _instance = new VfsPluginManager();
    }
    return *_instance;
}

VfsSetupParams::VfsSetupParams(const AccountPtr &account, const QUrl &baseUrl, const QString &spaceId, const QString &folderDisplayName, SyncEngine *syncEngine)
    : account(account)
    , _baseUrl(baseUrl)
    , _syncEngine(syncEngine)
    , _spaceId(spaceId)
    , _folderDisplayName(folderDisplayName)
{
}

QString VfsSetupParams::folderDisplayName() const
{
    return _folderDisplayName;
}

SyncEngine *VfsSetupParams::syncEngine() const
{
    return _syncEngine;
}
