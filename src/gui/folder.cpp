/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#include "folder.h"

#include "account.h"
#include "accountstate.h"
#include "application.h"
#include "common/checksums.h"
#include "common/depreaction.h"
#include "common/filesystembase.h"
#include "common/syncjournalfilerecord.h"
#include "common/version.h"
#include "common/vfs.h"
#include "configfile.h"
#include "filesystem.h"
#include "folderman.h"
#include "folderwatcher.h"
#include "gui/accountsettings.h"
#include "gui/folderdefinition.h"
#include "libsync/graphapi/spacesmanager.h"
#include "localdiscoverytracker.h"
#include "scheduling/syncscheduler.h"
#include "settingsdialog.h"
#include "socketapi/socketapi.h"
#include "syncengine.h"
#include "syncresult.h"
#include "syncrunfilelog.h"
#include "theme.h"

#ifdef Q_OS_WIN
#include "common/utility_win.h"
#endif

#include "fonticon.h"
#include "notifications/systemnotificationmanager.h"


#include <QDir>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <QApplication>
#include <QMessageBox>

using namespace Qt::Literals::StringLiterals;
using namespace std::chrono_literals;

namespace {
/* How often to retry a sync
 * Either due to _engine->isAnotherSyncNeeded or a sync error
 */
constexpr int retrySyncLimitC = 3;
}

namespace OCC {

using namespace FileSystem::SizeLiterals;

Q_LOGGING_CATEGORY(lcFolder, "gui.folder", QtInfoMsg)

Folder::Folder(const FolderDefinition &definition, const AccountStatePtr &accountState, std::unique_ptr<Vfs> &&vfs, QObject *parent)
    : QObject(parent)
    , _accountState(accountState)
    , _definition(definition)
    , _journal(_definition.absoluteJournalPath())
    , _fileLog(new SyncRunFileLog)
    , _vfs(vfs.release())
{
    _timeSinceLastSyncStart.start();
    _timeSinceLastSyncDone.start();

    SyncResult::Status status = SyncResult::NotYetStarted;
    if (definition.paused) {
        status = SyncResult::Paused;
    }
    setSyncState(status);
    // check if the local path exists
    if (checkLocalPath()) {
        // those errors should not persist over sessions
        _journal.wipeErrorBlacklistCategory(SyncJournalErrorBlacklistRecord::Category::LocalSoftError);
        _engine.reset(new SyncEngine(_accountState->account(), webDavUrl(), path(), {}, &_journal));
        // pass the setting if hidden files are to be ignored, will be read in csync_update
        _engine->setIgnoreHiddenFiles(_definition.ignoreHiddenFiles);

        if (!_engine->loadDefaultExcludes()) {
            qCWarning(lcFolder, "Could not read system exclude file");
        }

        connect(_accountState.data(), &AccountState::isConnectedChanged, this, &Folder::canSyncChanged);

        connect(_engine.data(), &SyncEngine::started, this, &Folder::slotSyncStarted, Qt::QueuedConnection);
        connect(_engine.data(), &SyncEngine::finished, this, &Folder::slotSyncFinished, Qt::QueuedConnection);

        connect(_engine.data(), &SyncEngine::transmissionProgress, this,
            [this](const ProgressInfo &pi) { Q_EMIT ProgressDispatcher::instance()->progressInfo(this, pi); });
        connect(_engine.data(), &SyncEngine::itemCompleted, this, &Folder::slotItemCompleted);
        connect(_engine.data(), &SyncEngine::seenLockedFile, FolderMan::instance(), &FolderMan::slotSyncOnceFileUnlocks);
        connect(_engine.data(), &SyncEngine::aboutToPropagate,
            this, &Folder::slotLogPropagationStart);
        connect(_engine.data(), &SyncEngine::syncError, this, &Folder::slotSyncError);

        connect(ProgressDispatcher::instance(), &ProgressDispatcher::folderConflicts,
            this, &Folder::slotFolderConflicts);
        connect(_engine.data(), &SyncEngine::excluded, this, [this](const QString &path) { Q_EMIT ProgressDispatcher::instance()->excluded(this, path); });

        _localDiscoveryTracker.reset(new LocalDiscoveryTracker);
        connect(_engine.data(), &SyncEngine::finished,
            _localDiscoveryTracker.data(), &LocalDiscoveryTracker::slotSyncFinished);
        connect(_engine.data(), &SyncEngine::itemCompleted,
            _localDiscoveryTracker.data(), &LocalDiscoveryTracker::slotItemCompleted);

        connect(_accountState->account()->spacesManager(), &GraphApi::SpacesManager::spaceChanged, this, [this](GraphApi::Space *changedSpace) {
            if (_definition.spaceId() == changedSpace->id()) {
                prepareFolder(path(), displayName(), changedSpace->drive().getDescription());
                Q_EMIT spaceChanged();
            }
        });

        // Potentially upgrade suffix vfs to windows vfs
        OC_ENFORCE(_vfs);
        // Initialize the vfs plugin. Do this after the UI is running, so we can show a dialog when something goes wrong.
        QTimer::singleShot(0, this, &Folder::startVfs);
    }
}

Folder::~Folder()
{
    // If wipeForRemoval() was called the vfs has already shut down.
    if (_vfs)
        _vfs->stop();

    // Reset then engine first as it will abort and try to access members of the Folder
    _engine.reset();
}

Result<void, QString> Folder::checkPathLength(const QString &path)
{
#ifdef Q_OS_WIN
    if (path.size() > MAX_PATH) {
        if (!FileSystem::longPathsEnabledOnWindows()) {
            return tr("The path '%1' is too long. Please enable long paths in the Windows settings or choose a different folder.").arg(path);
        }
    }
#else
    Q_UNUSED(path)
#endif
    return {};
}

GraphApi::Space *Folder::space() const
{
    return _accountState->account()->spacesManager()->space(_definition.spaceId());
}

bool Folder::checkLocalPath()
{
#ifdef Q_OS_WIN
    Utility::NtfsPermissionLookupRAII ntfs_perm;
#endif
    const QFileInfo fi(_definition.localPath());
    _canonicalLocalPath = fi.canonicalFilePath();
#ifdef Q_OS_MAC
    // Workaround QTBUG-55896  (Should be fixed in Qt 5.8)
    _canonicalLocalPath = _canonicalLocalPath.normalized(QString::NormalizationForm_C);
#endif
    if (_canonicalLocalPath.isEmpty()) {
        qCWarning(lcFolder) << "Broken symlink:" << _definition.localPath();
        _canonicalLocalPath = _definition.localPath();
    } else if (!_canonicalLocalPath.endsWith(QLatin1Char('/'))) {
        _canonicalLocalPath.append(QLatin1Char('/'));
    }

    QString error;
    if (fi.isDir() && fi.isReadable() && fi.isWritable()) {
        auto pathLenghtCheck = checkPathLength(_canonicalLocalPath);
        if (!pathLenghtCheck) {
            error = pathLenghtCheck.error();
        }

        if (error.isEmpty()) {
            qCDebug(lcFolder) << "Checked local path ok";
            if (!_journal.open()) {
                error = tr("%1 failed to open the database.").arg(_definition.localPath());
            }
        }
    } else {
        // Check directory again
        if (!FileSystem::fileExists(_definition.localPath(), fi)) {
            error = tr("Local folder %1 does not exist.").arg(_definition.localPath());
        } else if (!fi.isDir()) {
            error = tr("%1 should be a folder but is not.").arg(_definition.localPath());
        } else if (!fi.isReadable()) {
            error = tr("%1 is not readable.").arg(_definition.localPath());
        } else if (!fi.isWritable()) {
            error = tr("%1 is not writable.").arg(_definition.localPath());
        }
    }
    if (!error.isEmpty()) {
        qCWarning(lcFolder) << error;
        _syncResult.appendErrorString(error);
        setSyncState(SyncResult::SetupError);
        return false;
    }
    return true;
}

SyncOptions Folder::loadSyncOptions()
{
    SyncOptions opt(_vfs);
    ConfigFile cfgFile;

    opt._moveFilesToTrash = cfgFile.moveToTrash();
    opt._vfs = _vfs;
    // account is currently a shared ptr and thus the lifetime of the account object is guaranteed
    opt._parallelNetworkJobs = [account = _accountState->account()] { return account->isHttp2Supported() ? 20 : 6; };

    return opt;
}

void Folder::setIsReady(bool b)
{
    if (b == _vfsIsReady) {
        return;
    }
    _vfsIsReady = b;
    Q_EMIT isReadyChanged();
}

void Folder::prepareFolder(const QString &path, const std::optional<QString> &displayName, const std::optional<QString> &description)
{
#ifdef Q_OS_WIN
    // First create a Desktop.ini so that the folder and favorite link show our application's icon.
    const QFileInfo desktopIniPath{u"%1/Desktop.ini"_s.arg(path)};
    {
        const QString updateIconKey = u"%1/UpdateIcon"_s.arg(Theme::instance()->appName());
        const QString localizedNameKey = u".ShellClassInfo/LocalizedResourcename"_s;
        QSettings desktopIni(desktopIniPath.absoluteFilePath(), QSettings::IniFormat);
        if (desktopIni.value(updateIconKey, true).toBool()) {
            qCInfo(lcFolder) << "Creating" << desktopIni.fileName() << "to set a folder icon in Explorer.";
            desktopIni.setValue(u".ShellClassInfo/IconResource"_s, QDir::toNativeSeparators(qApp->applicationFilePath()));
            desktopIni.setValue(u".ShellClassInfo/ConfirmFileOp"_s, 1);
            if (description.has_value()) {
                QString descriptionValue = description.value();
                // the description can still be empty
                if (descriptionValue.isEmpty()) {
                    const auto displayNameVal = displayName.has_value() ? displayName.value() : desktopIni.value(localizedNameKey).toString();
                    if (displayNameVal.isEmpty()) {
                        descriptionValue = Theme::instance()->appNameGUI();
                    } else {
                        descriptionValue = u"%1 - %2"_s.arg(Theme::instance()->appNameGUI(), displayNameVal);
                    }
                }
                desktopIni.setValue(u".ShellClassInfo/InfoTip"_s, descriptionValue);
            }
            // we got an actual displayName, update
            if (displayName.has_value()) {
                Q_ASSERT(!displayName->isEmpty());
                desktopIni.setValue(u".ShellClassInfo/LocalizedResourcename"_s, displayName.value());
            }
            desktopIni.setValue(updateIconKey, true);
        } else {
            qCInfo(lcFolder) << "Skip icon update for" << desktopIni.fileName() << "," << updateIconKey << "is disabled";
        }

        desktopIni.sync();
    }

    const QString longFolderPath = FileSystem::longWinPath(path);
    const QString longDesktopIniPath = FileSystem::longWinPath(desktopIniPath.absoluteFilePath());
    // Set the folder as system and Desktop.ini as hidden+system for explorer to pick it.
    // https://msdn.microsoft.com/en-us/library/windows/desktop/cc144102
    const DWORD folderAttrs = GetFileAttributesW(reinterpret_cast<const wchar_t *>(longFolderPath.utf16()));
    if (!SetFileAttributesW(reinterpret_cast<const wchar_t *>(longFolderPath.utf16()), folderAttrs | FILE_ATTRIBUTE_SYSTEM)) {
        const auto error = GetLastError();
        qCWarning(lcFolder) << "SetFileAttributesW failed on" << longFolderPath << Utility::formatWinError(error);
    }
    if (!SetFileAttributesW(reinterpret_cast<const wchar_t *>(longDesktopIniPath.utf16()), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
        const auto error = GetLastError();
        qCWarning(lcFolder) << "SetFileAttributesW failed on" << longDesktopIniPath << Utility::formatWinError(error);
    }
#else
    Q_UNUSED(path)
#endif
}

QString Folder::displayName() const
{
    if (auto *s = space()) {
        return s->displayName();
    }
    return _definition.displayName();
}

QString Folder::path() const
{
    return _canonicalLocalPath;
}

QString Folder::shortGuiLocalPath() const
{
    QString p = _definition.localPath();
    QString home = QDir::homePath();
    if (!home.endsWith(QLatin1Char('/'))) {
        home.append(QLatin1Char('/'));
    }
    if (p.startsWith(home)) {
        p = p.mid(home.length());
    }
    if (p.length() > 1 && p.endsWith(QLatin1Char('/'))) {
        p.chop(1);
    }
    return QDir::toNativeSeparators(p);
}


bool Folder::ignoreHiddenFiles()
{
    bool re(_definition.ignoreHiddenFiles);
    return re;
}

void Folder::setIgnoreHiddenFiles(bool ignore)
{
    _definition.ignoreHiddenFiles = ignore;
}

QString Folder::cleanPath() const
{
    QString cleanedPath = QDir::cleanPath(_canonicalLocalPath);

    if (cleanedPath.length() == 3 && cleanedPath.endsWith(QLatin1String(":/")))
        cleanedPath.remove(2, 1);

    return cleanedPath;
}

bool Folder::isSyncRunning() const
{
    return _syncResult.status() == SyncResult::SyncPrepare || _syncResult.status() == SyncResult::SyncRunning;
}

QUrl Folder::webDavUrl() const
{
    const QString spaceId = _definition.spaceId();
    if (!spaceId.isEmpty()) {
        if (auto *space = _accountState->account()->spacesManager()->space(spaceId)) {
            return QUrl(space->drive().getRoot().getWebDavUrl());
        }
    }
    return _definition.webDavUrl();
}

bool Folder::isSyncPaused() const
{
    return _definition.paused;
}

bool Folder::canSync() const
{
    return _engine && !isSyncPaused() && accountState()->readyForSync() && isReady() && _accountState->account()->hasCapabilities() && _folderWatcher;
}

bool Folder::isReady() const
{
    return _vfsIsReady;
}

void Folder::setSyncPaused(bool paused)
{
    if (hasSetupError()) {
        return;
    }
    if (paused == _definition.paused) {
        return;
    }

    _definition.paused = paused;

    Q_EMIT syncPausedChanged(this, paused);
    if (!paused) {
        setSyncState(SyncResult::NotYetStarted);
    } else {
        setSyncState(SyncResult::Paused);
    }
    Q_EMIT canSyncChanged();
}

void Folder::setSyncState(SyncResult::Status state)
{
    if (state != _syncResult.status()) {
        _syncResult.setStatus(state);
        Q_EMIT syncStateChange();
    }
}

SyncResult Folder::syncResult() const
{
    return _syncResult;
}

void Folder::showSyncResultPopup()
{
    if (_syncResult.firstItemNew()) {
        createGuiLog(_syncResult.firstItemNew()->destination(), LogStatusNew, _syncResult.numNewItems());
    }
    if (_syncResult.firstItemDeleted()) {
        createGuiLog(_syncResult.firstItemDeleted()->destination(), LogStatusRemove, _syncResult.numRemovedItems());
    }
    if (_syncResult.firstItemUpdated()) {
        createGuiLog(_syncResult.firstItemUpdated()->destination(), LogStatusUpdated, _syncResult.numUpdatedItems());
    }

    if (_syncResult.firstItemRenamed()) {
        LogStatus status(LogStatusRename);
        // if the path changes it's rather a move
        QDir renTarget = QFileInfo(_syncResult.firstItemRenamed()->_renameTarget).dir();
        QDir renSource = QFileInfo(_syncResult.firstItemRenamed()->localName()).dir();
        if (renTarget != renSource) {
            status = LogStatusMove;
        }
        createGuiLog(_syncResult.firstItemRenamed()->localName(), status, _syncResult.numRenamedItems(), _syncResult.firstItemRenamed()->_renameTarget);
    }

    if (_syncResult.firstNewConflictItem()) {
        createGuiLog(_syncResult.firstNewConflictItem()->destination(), LogStatusConflict, _syncResult.numNewConflictItems());
    }
    if (int errorCount = _syncResult.numErrorItems()) {
        createGuiLog(_syncResult.firstItemError()->localName(), LogStatusError, errorCount);
    }

    qCInfo(lcFolder) << "Folder" << path() << "sync result: " << _syncResult.status();
}

void Folder::createGuiLog(const QString &filename, LogStatus status, int count,
    const QString &renameTarget)
{
    if (count > 0) {
        QString file = QDir::toNativeSeparators(filename);
        QString text;

        switch (status) {
        case LogStatusRemove:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have been removed.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has been removed.", "%1 names a file.").arg(file);
            }
            break;
        case LogStatusNew:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have been added.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has been added.", "%1 names a file.").arg(file);
            }
            break;
        case LogStatusUpdated:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have been updated.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has been updated.", "%1 names a file.").arg(file);
            }
            break;
        case LogStatusRename:
            if (count > 1) {
                text = tr("%1 has been renamed to %2 and %n other file(s) have been renamed.", "", count - 1).arg(file, renameTarget);
            } else {
                text = tr("%1 has been renamed to %2.", "%1 and %2 name files.").arg(file, renameTarget);
            }
            break;
        case LogStatusMove:
            if (count > 1) {
                text = tr("%1 has been moved to %2 and %n other file(s) have been moved.", "", count - 1).arg(file, renameTarget);
            } else {
                text = tr("%1 has been moved to %2.").arg(file, renameTarget);
            }
            break;
        case LogStatusConflict:
            if (count > 1) {
                text = tr("%1 and %n other file(s) have sync conflicts.", "", count - 1).arg(file);
            } else {
                text = tr("%1 has a sync conflict. Please check the conflict file!").arg(file);
            }
            break;
        case LogStatusError:
            if (count > 1) {
                text = tr("%1 and %n other file(s) could not be synced due to errors. See the log for details.", "", count - 1).arg(file);
            } else {
                text = tr("%1 could not be synced due to an error. See the log for details.").arg(file);
            }
            break;
        }

        if (!text.isEmpty()) {
            ocApp()->systemNotificationManager()->notify({tr("Sync Activity"), text, Resources::FontIcon(u'')});
        }
    }
}

void Folder::startVfs()
{
    OC_ENFORCE(_vfs);
    OC_ENFORCE(_vfs->mode() == _definition.virtualFilesMode);

    const auto result = Vfs::checkAvailability(path(), _vfs->mode());
    if (!result) {
        _syncResult.appendErrorString(result.error());
        setSyncState(SyncResult::SetupError);
        return;
    }

    VfsSetupParams vfsParams(_accountState->account(), webDavUrl(), _definition.spaceId(), _engine.get());
    vfsParams.filesystemPath = path();
    vfsParams.journal = &_journal;
    vfsParams.providerDisplayName = Theme::instance()->appNameGUI();
    vfsParams.providerName = Theme::instance()->appName();
    vfsParams.providerVersion = Version::version();

    connect(&_engine->syncFileStatusTracker(), &SyncFileStatusTracker::fileStatusChanged,
        _vfs.data(), &Vfs::fileStatusChanged);


    connect(_vfs.data(), &Vfs::started, this, [this] {
        // Immediately mark the sqlite temporaries as excluded. They get recreated
        // on db-open and need to get marked again every time.
        QString stateDbFile = _journal.databaseFilePath();
        _vfs->fileStatusChanged(stateDbFile + QStringLiteral("-wal"), SyncFileStatus::StatusExcluded);
        _vfs->fileStatusChanged(stateDbFile + QStringLiteral("-shm"), SyncFileStatus::StatusExcluded);
        _engine->setSyncOptions(loadSyncOptions());
        registerFolderWatcher();

        connect(_vfs.get(), &Vfs::needSync, this, [this] {
            if (canSync()) {
                // the vfs plugin detected that its metadata is out of sync and requests a new sync
                // the request has a hight priority as it is probably issued after a user request
                FolderMan::instance()->scheduler()->enqueueFolder(this, SyncScheduler::Priority::High);
            }
        });
        setIsReady(true);
        Q_EMIT FolderMan::instance()->folderListChanged();
        // we are setup, schedule ourselves if we can
        // if not the scheduler will take care of it later.
        if (canSync()) {
            FolderMan::instance()->scheduler()->enqueueFolder(this);
        }
    });
    connect(_vfs.data(), &Vfs::error, this, [this](const QString &error) {
        _syncResult.appendErrorString(error);
        setSyncState(SyncResult::SetupError);
        setIsReady(false);
    });

    slotNextSyncFullLocalDiscovery();
    _vfs->start(vfsParams);
}

void Folder::slotDiscardDownloadProgress()
{
    // Delete from journal and from filesystem.
    QDir folderpath(_definition.localPath());
    QSet<QString> keep_nothing;
    const QVector<SyncJournalDb::DownloadInfo> deleted_infos =
        _journal.getAndDeleteStaleDownloadInfos(keep_nothing);
    for (const auto &deleted_info : deleted_infos) {
        const QString tmppath = folderpath.filePath(deleted_info._tmpfile);
        qCInfo(lcFolder) << "Deleting temporary file: " << tmppath;
        FileSystem::remove(tmppath);
    }
}

int Folder::slotWipeErrorBlacklist()
{
    return _journal.wipeErrorBlacklist();
}

void Folder::slotWatchedPathsChanged(const QSet<QString> &paths, ChangeReason reason)
{
    if (!isReady()) {
        // we might be switching backend
        return;
    }
    bool needSync = false;
    for (const auto &path : paths) {
        Q_ASSERT(FileSystem::isChildPathOf(path, this->path()));

        const QString relativePath = path.mid(this->path().size());
        if (reason == ChangeReason::UnLock) {
            journalDb()->wipeErrorBlacklistEntry(relativePath, SyncJournalErrorBlacklistRecord::Category::LocalSoftError);

            {
                // horrible hack to compensate that we don't handle folder deletes on a per file basis
                qsizetype index = 0;
                QString p = relativePath;
                while ((index = p.lastIndexOf(QLatin1Char('/'))) != -1) {
                    p = p.left(index);
                    const auto rec = journalDb()->errorBlacklistEntry(p);
                    if (rec.isValid()) {
                        if (rec._errorCategory == SyncJournalErrorBlacklistRecord::Category::LocalSoftError) {
                            journalDb()->wipeErrorBlacklistEntry(p);
                        }
                    }
                }
            }
        }

        // Add to list of locally modified paths
        //
        // We do this before checking for our own sync-related changes to make
        // extra sure to not miss relevant changes.
        _localDiscoveryTracker->addTouchedPath(relativePath);

        const SyncJournalFileRecord record = _journal.getFileRecord(relativePath);
        if (reason != ChangeReason::UnLock) {
            // Check that the mtime/size actually changed or there was
            // an attribute change (pin state) that caused the notification
            bool spurious = false;
            if (record.isValid()
                && !FileSystem::fileChanged(FileSystem::toFilesystemPath(path), FileSystem::FileChangedInfo::fromSyncJournalFileRecord(record))) {
                spurious = true;

                if (auto pinState = _vfs->pinState(relativePath)) {
                    if (*pinState == PinState::AlwaysLocal && record.isVirtualFile())
                        spurious = false;
                    if (*pinState == PinState::OnlineOnly && record.isFile())
                        spurious = false;
                }
            }
            if (spurious) {
                qCInfo(lcFolder) << "Ignoring spurious notification for file" << relativePath;
                continue; // probably a spurious notification
            }
        }
        warnOnNewExcludedItem(record, relativePath);

        Q_EMIT watchedFileChangedExternally(path);
        needSync = true;
    }
    if (needSync && canSync()) {
        FolderMan::instance()->scheduler()->enqueueFolder(this);
    }
}

void Folder::setVirtualFilesEnabled(bool enabled)
{
    Vfs::Mode newMode = _definition.virtualFilesMode;
    if (enabled && _definition.virtualFilesMode == Vfs::Off) {
        newMode = VfsPluginManager::instance().bestAvailableVfsMode();
    } else if (!enabled && _definition.virtualFilesMode != Vfs::Off) {
        newMode = Vfs::Off;
    }

    if (newMode != _definition.virtualFilesMode) {
        // This is tested in TestSyncVirtualFiles::testWipeVirtualSuffixFiles, so for changes here, have them reflected in that test.
        const bool isPaused = _definition.paused;
        if (!isPaused) {
            setSyncPaused(true);
        }
        auto finalizeVfsSwitch = [newMode, enabled, isPaused, this] {
            // Wipe selective sync blacklist
            bool ok = false;
            const auto oldBlacklist = journalDb()->getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok);
            journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, {});

            // Wipe the dehydrated files from the DB, they will get downloaded on the next sync. We need to do this, otherwise the files
            // are in the DB but not on disk, so the client assumes they are deleted, and removes them from the remote.
            _vfs->wipeDehydratedVirtualFiles();

            // Tear down the VFS
            setIsReady(false);
            _vfs->stop();
            _vfs->unregisterFolder();

            disconnect(_vfs.data(), nullptr, this, nullptr);
            disconnect(&_engine->syncFileStatusTracker(), nullptr, _vfs.data(), nullptr);

            // _vfs is a shared pointer...
            _vfs.reset(VfsPluginManager::instance().createVfsFromPlugin(newMode).release());

            // Restart VFS.
            _definition.virtualFilesMode = newMode;
            if (enabled) {
                connect(_vfs.data(), &Vfs::started, this, [oldBlacklist, this] {
                    for (const auto &entry : oldBlacklist) {
                        journalDb()->schedulePathForRemoteDiscovery(entry);
                        std::ignore = vfs().setPinState(entry, PinState::OnlineOnly);
                    }
                });
            }
            if (!isPaused) {
                setSyncPaused(isPaused);
            }
            startVfs();
        };
        if (isSyncRunning()) {
            connect(this, &Folder::syncFinished, this, finalizeVfsSwitch, Qt::SingleShotConnection);
            slotTerminateSync(tr("Switching VFS mode on folder '%1'").arg(displayName()));
        } else {
            finalizeVfsSwitch();
        }
    }
}

bool Folder::supportsSelectiveSync() const
{
    return !virtualFilesEnabled() && isReady();
}

bool Folder::isDeployed() const
{
    return _definition.isDeployed();
}

Vfs::Mode Folder::vfsMode() const
{
    return _vfs->mode();
}

uint32_t Folder::priority()
{
    return _definition.priority();
}

void Folder::setPriority(uint32_t p)
{
    return _definition.setPriority(p);
}

bool Folder::isFileExcludedAbsolute(const QString &fullPath) const
{
    if (OC_ENSURE_NOT(_engine.isNull())) {
        return _engine->isExcluded(fullPath);
    }
    return true;
}

bool Folder::isFileExcludedRelative(const QString &relativePath) const
{
    return isFileExcludedAbsolute(path() + relativePath);
}

void Folder::openInWebBrowser()
{
    fetchPrivateLinkUrl(_accountState->account(), webDavUrl(), {}, this, [](const QUrl &url) { Utility::openBrowser(url, nullptr); });
}

void Folder::slotTerminateSync(const QString &reason)
{
    if (isReady()) {
        qCInfo(lcFolder) << "folder " << path() << " Terminating!";
        if (_engine->isSyncRunning()) {
            _engine->abort(reason);
            setSyncState(SyncResult::SyncAbortRequested);
        }
    }
}

void Folder::wipeForRemoval()
{
    // we can't acces those variables
    if (hasSetupError()) {
        return;
    }
    // prevent interaction with the db etc
    setIsReady(false);

    // stop reacting to changes
    // especially the upcoming deletion of the db
    _folderWatcher.reset();

    // Delete files that have been partially downloaded.
    slotDiscardDownloadProgress();

    // Unregister the socket API so it does not keep the .sync_journal file open
    FolderMan::instance()->socketApi()->slotUnregisterPath(this);
    _journal.close(); // close the sync journal

    // Remove db and temporaries
    const QString stateDbFile = _engine->journal()->databaseFilePath();

    QFile file(stateDbFile);
    if (file.exists()) {
        if (!file.remove()) {
            qCCritical(lcFolder) << "Failed to remove existing csync StateDB " << stateDbFile;
        } else {
            qCInfo(lcFolder) << "wipe: Removed csync StateDB " << stateDbFile;
        }
    } else {
        qCWarning(lcFolder) << "statedb is empty, can not remove.";
    }

    // Also remove other db related files
    QFile::remove(stateDbFile + QStringLiteral(".ctmp"));
    QFile::remove(stateDbFile + QStringLiteral("-shm"));
    QFile::remove(stateDbFile + QStringLiteral("-wal"));
    QFile::remove(stateDbFile + QStringLiteral("-journal"));

    // remove the sync log
    QFile::remove(u"%1/.OpenCloudSync.log"_s.arg(_canonicalLocalPath));

#ifdef Q_OS_WIN
    // remove the desktop ini
    QFile::remove(u"%1/Desktop.ini"_s.arg(_canonicalLocalPath));
#endif

    _vfs->stop();
    _vfs->unregisterFolder();
    _vfs.reset(nullptr); // warning: folder now in an invalid state
}

bool Folder::reloadExcludes()
{
    if (!_engine) {
        return true;
    }
    return _engine->reloadExcludes();
}

void Folder::startSync()
{
    Q_ASSERT(isReady());
    Q_ASSERT(_folderWatcher);

    if (!OC_ENSURE(!isSyncRunning())) {
        qCCritical(lcFolder) << "ERROR sync is still running and new sync requested.";
        return;
    }

    if (!OC_ENSURE(canSync())) {
        qCCritical(lcFolder) << "ERROR folder is currently not sync able.";
        return;
    }

    _timeSinceLastSyncStart.start();
    _syncResult.reset();
    setSyncState(SyncResult::SyncPrepare);

    qCInfo(lcFolder) << "*** Start syncing " << displayName() << "client version" << Theme::instance()->aboutVersions(Theme::VersionFormat::OneLiner);

    _fileLog->start(path());

    if (!reloadExcludes()) {
        slotSyncError(tr("Could not read system exclude file"));
        QMetaObject::invokeMethod(
            this, [this] { slotSyncFinished(false); }, Qt::QueuedConnection);
        return;
    }

    setDirtyNetworkLimits();

    // get the latest touched files
    // this will enque this folder again, it doesn't matter
    slotWatchedPathsChanged(_folderWatcher->popChangeSet(), Folder::ChangeReason::Other);

    const std::chrono::milliseconds fullLocalDiscoveryInterval = ConfigFile().fullLocalDiscoveryInterval();
    const bool hasDoneFullLocalDiscovery = _timeSinceLastFullLocalDiscovery.isValid();
    // negative fullLocalDiscoveryInterval means we don't require periodic full runs
    const bool periodicFullLocalDiscoveryNow =
        fullLocalDiscoveryInterval.count() >= 0 && _timeSinceLastFullLocalDiscovery.hasExpired(fullLocalDiscoveryInterval.count());
    if (_folderWatcher && _folderWatcher->isReliable()
        && hasDoneFullLocalDiscovery
        && !periodicFullLocalDiscoveryNow) {
        qCInfo(lcFolder) << "Allowing local discovery to read from the database";
        _engine->setLocalDiscoveryOptions(
            LocalDiscoveryStyle::DatabaseAndFilesystem,
            _localDiscoveryTracker->localDiscoveryPaths());
        _localDiscoveryTracker->startSyncPartialDiscovery();
    } else {
        qCInfo(lcFolder) << "Forbidding local discovery to read from the database";
        _engine->setLocalDiscoveryOptions(LocalDiscoveryStyle::FilesystemOnly);
        _localDiscoveryTracker->startSyncFullDiscovery();
    }

    _engine->setIgnoreHiddenFiles(_definition.ignoreHiddenFiles);
    QMetaObject::invokeMethod(_engine.data(), &SyncEngine::startSync, Qt::QueuedConnection);

    Q_EMIT syncStarted();
}

void Folder::setDirtyNetworkLimits()
{
    Q_ASSERT(isReady());
    ConfigFile cfg;
    int downloadLimit = -75; // 75%
    int useDownLimit = cfg.useDownloadLimit();
    if (useDownLimit >= 1) {
        downloadLimit = cfg.downloadLimit() * 1000;
    } else if (useDownLimit == 0) {
        downloadLimit = 0;
    }

    int uploadLimit = -75; // 75%
    int useUpLimit = cfg.useUploadLimit();
    if (useUpLimit >= 1) {
        uploadLimit = cfg.uploadLimit() * 1000;
    } else if (useUpLimit == 0) {
        uploadLimit = 0;
    }

    _engine->setNetworkLimits(uploadLimit, downloadLimit);
}

void Folder::reloadSyncOptions()
{
    _engine->setSyncOptions(loadSyncOptions());
}

void Folder::slotSyncError(const QString &message, ErrorCategory category)
{
    _syncResult.appendErrorString(message);
    Q_EMIT ProgressDispatcher::instance()->syncError(this, message, category);
}

void Folder::slotSyncStarted()
{
    qCInfo(lcFolder) << "#### Propagation start ####################################################";
    setSyncState(SyncResult::SyncRunning);
    Q_EMIT isSyncRunningChanged();
}

void Folder::slotSyncFinished(bool success)
{
    if (!isReady()) {
        // probably removing the folder
        return;
    }
    qCInfo(lcFolder) << "Client version" << Theme::instance()->aboutVersions(Theme::VersionFormat::OneLiner);
    Q_EMIT isSyncRunningChanged();

    bool syncError = !_syncResult.errorStrings().isEmpty();
    if (syncError) {
        qCWarning(lcFolder) << "SyncEngine finished with ERROR";
    } else {
        qCInfo(lcFolder) << "SyncEngine finished without problem.";
    }
    _fileLog->finish();
    showSyncResultPopup();

    auto anotherSyncNeeded = false;

    auto syncStatus = SyncResult::Status::Undefined;

    if (syncError) {
        syncStatus = SyncResult::Error;
    } else if (_syncResult.foundFilesNotSynced()) {
        syncStatus = SyncResult::Problem;
    } else if (_definition.paused) {
        // Maybe the sync was terminated because the user paused the folder
        syncStatus = SyncResult::Paused;
    } else {
        syncStatus = SyncResult::Success;
    }

    // Count the number of syncs that have failed in a row.
    if (syncStatus == SyncResult::Success || syncStatus == SyncResult::Problem) {
        _consecutiveFailingSyncs = 0;
    } else {
        _consecutiveFailingSyncs++;
        anotherSyncNeeded |= _consecutiveFailingSyncs <= retrySyncLimitC;
        qCInfo(lcFolder) << "the last" << _consecutiveFailingSyncs << "syncs failed";
    }

    if (syncStatus == SyncResult::Success && success) {
        // Clear the white list as all the folders that should be on that list are sync-ed
        journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncWhiteList, {});
    }

    if ((syncStatus == SyncResult::Success || syncStatus == SyncResult::Problem) && success) {
        if (_engine->lastLocalDiscoveryStyle() == LocalDiscoveryStyle::FilesystemOnly) {
            _timeSinceLastFullLocalDiscovery.start();
        }
    }

    if (syncStatus != SyncResult::Undefined) {
        setSyncState(syncStatus);
    }

    // syncStateChange from setSyncState needs to be emitted first
    QTimer::singleShot(0, this, [this] { Q_EMIT syncFinished(_syncResult); });

    _lastSyncDuration = std::chrono::milliseconds(_timeSinceLastSyncStart.elapsed());
    _timeSinceLastSyncDone.start();

    // Increment the follow-up sync counter if necessary.
    if (_engine->isAnotherSyncNeeded()) {
        _consecutiveFollowUpSyncs++;
        anotherSyncNeeded |= _consecutiveFollowUpSyncs <= retrySyncLimitC;
        qCInfo(lcFolder) << "another sync was requested by the finished sync, this has"
                         << "happened" << _consecutiveFollowUpSyncs << "times";
    } else {
        _consecutiveFollowUpSyncs = 0;
    }

    // Maybe force a follow-up sync to take place, but only a couple of times.
    if (anotherSyncNeeded && canSync()) {
        // Sometimes another sync is requested because a local file is still
        // changing, so wait at least a small amount of time before syncing
        // the folder again.
        QTimer::singleShot(SyncEngine::minimumFileAgeForUpload, this, [this] { FolderMan::instance()->scheduler()->enqueueFolder(this); });
    }
}

// a item is completed: count the errors and forward to the ProgressDispatcher
void Folder::slotItemCompleted(const SyncFileItemPtr &item)
{
    if (item->_status == SyncFileItem::Success && (item->instruction() & (CSYNC_INSTRUCTION_NONE | CSYNC_INSTRUCTION_UPDATE_METADATA))) {
        // We only care about the updates that deserve to be shown in the UI
        return;
    }

    _syncResult.processCompletedItem(item);

    _fileLog->logItem(*item);
    Q_EMIT ProgressDispatcher::instance()->itemCompleted(this, item);
}

void Folder::slotLogPropagationStart()
{
    _fileLog->logLap(QStringLiteral("Propagation starts"));
}

void Folder::slotNextSyncFullLocalDiscovery()
{
    _timeSinceLastFullLocalDiscovery.invalidate();
}

void Folder::schedulePathForLocalDiscovery(const QString &relativePath)
{
    _localDiscoveryTracker->addTouchedPath(relativePath);
}

void Folder::slotFolderConflicts(Folder *folder, const QStringList &conflictPaths)
{
    if (folder != this)
        return;
    auto &r = _syncResult;

    // If the number of conflicts is too low, adjust it upwards
    if (conflictPaths.size() > r.numNewConflictItems() + r.numOldConflictItems())
        r.setNumOldConflictItems(conflictPaths.size() - r.numNewConflictItems());
}

void Folder::warnOnNewExcludedItem(const SyncJournalFileRecord &record, QStringView path)
{
    // Never warn for items in the database
    if (record.isValid())
        return;

    bool ok = false;
    auto blacklist = _journal.getSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, &ok);
    if (!ok)
        return;
    if (!blacklist.contains(path + QLatin1Char('/')))
        return;

    // Don't warn for items that no longer exist.
    // Note: This assumes we're getting file watcher notifications
    // for folders only on creation and deletion - if we got a notification
    // on content change that would create spurious warnings.
    QFileInfo fi(_canonicalLocalPath + path);
    if (!fi.exists())
        return;

    const QString message = fi.isDir() ? tr("The folder %1 was created but was excluded from synchronization previously. "
                                            "Data inside it will not be synchronized.")
                                             .arg(fi.filePath())
                                       : tr("The file %1 was created but was excluded from synchronization previously. "
                                            "It will not be synchronized.")
                                             .arg(fi.filePath());

    ocApp()->systemNotificationManager()->notify({tr("%1 is not synchronized").arg(fi.fileName()), message, Resources::FontIcon(u'')});
}

void Folder::slotWatcherUnreliable(const QString &message)
{
    qCWarning(lcFolder) << "Folder watcher for" << path() << "became unreliable:" << message;

    QMessageBox *msgBox = new QMessageBox(QMessageBox::Information, Theme::instance()->appNameGUI(),
        tr("Changes in synchronized folders could not be tracked reliably.\n"
           "\n"
           "This means that the synchronization client might not upload local changes "
           "immediately and will instead only scan for local changes and upload them "
           "occasionally (every two hours by default).\n"
           "\n"
           "%1")
            .arg(message),
        {}, ocApp()->settingsDialog());

    msgBox->setAttribute(Qt::WA_DeleteOnClose);
    ocApp()->showSettings();
    msgBox->open();
}

void Folder::registerFolderWatcher()
{
    if (!_folderWatcher.isNull()) {
        return;
    }

    _folderWatcher.reset(new FolderWatcher(this));
    connect(_folderWatcher.data(), &FolderWatcher::pathChanged, this,
        [this](const QSet<QString> &paths) { slotWatchedPathsChanged(paths, Folder::ChangeReason::Other); });
    connect(_folderWatcher.data(), &FolderWatcher::changesDetected, this, [this] { setSyncState(SyncResult::NotYetStarted); });
    connect(_folderWatcher.data(), &FolderWatcher::lostChanges,
        this, &Folder::slotNextSyncFullLocalDiscovery);
    connect(_folderWatcher.data(), &FolderWatcher::becameUnreliable,
        this, &Folder::slotWatcherUnreliable);
    _folderWatcher->init(path());
    _folderWatcher->startNotificatonTest(path() + QLatin1String(".OpenCloudSync.log"));
}

bool Folder::virtualFilesEnabled() const
{
    return _definition.virtualFilesMode != Vfs::Off;
}

} // namespace OCC
