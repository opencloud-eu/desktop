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

#pragma once

#include "gui/opencloudguilib.h"

#include "accountstate.h"
#include "common/syncjournaldb.h"
#include "gui/folderdefinition.h"
#include "libsync/graphapi/space.h"
#include "networkjobs.h"
#include "progressdispatcher.h"
#include "syncoptions.h"
#include "syncresult.h"

#include <QDateTime>
#include <QObject>
#include <QtQml/QQmlEngine>

#include <chrono>
#include <set>

class QThread;
class QSettings;

namespace OCC {

class Vfs;
class SyncEngine;
class SyncRunFileLog;
class FolderWatcher;
class LocalDiscoveryTracker;

/**
 * @brief The Folder class
 * @ingroup gui
 */
class OPENCLOUD_GUI_EXPORT Folder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(GraphApi::Space *space READ space NOTIFY spaceChanged)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QUrl webDavUrl READ webDavUrl CONSTANT)
    Q_PROPERTY(bool isReady READ isReady NOTIFY isReadyChanged)
    Q_PROPERTY(bool isSyncPaused READ isSyncPaused NOTIFY syncPausedChanged)
    Q_PROPERTY(bool isSyncRunning READ isSyncRunning NOTIFY isSyncRunningChanged)
    Q_PROPERTY(bool isDeployed READ isDeployed CONSTANT)
    Q_PROPERTY(Vfs::Mode vfsMode READ vfsMode CONSTANT)
    QML_ELEMENT
    QML_UNCREATABLE("Folders can only be created by the FolderManager")

public:
    enum class ChangeReason {
        Other,
        UnLock
    };
    Q_ENUM(ChangeReason)

    static void prepareFolder(const QString &path, const QString &displayName, const QString &description, bool override);

    ~Folder() override;
    /**
     * The account the folder is configured on.
     */
    AccountStatePtr accountState() const { return _accountState; }

    QString displayName() const;

    /**
     * short local path to display on the GUI  (native separators)
     */
    QString shortGuiLocalPath() const;

    /**
     * canonical local folder path, always ends with /
     */
    QString path() const;

    /**
     * cleaned canonical folder path, like path() but never ends with a /
     *
     * Wrapper for QDir::cleanPath(path()) except for "Z:/",
     * where it returns "Z:" instead of "Z:/".
     */
    QString cleanPath() const;

    /**
     * The full remote WebDAV URL
     */
    QUrl webDavUrl() const;

    /**
     * switch sync on or off
     */
    void setSyncPaused(bool);

    bool isSyncPaused() const;

    /**
     * Returns true when the folder may sync.
     */
    bool canSync() const;

    /**
     * Whether the folder is ready
     */
    bool isReady() const;

    bool hasSetupError() const
    {
        return _syncResult.status() == SyncResult::SetupError;
    }

    /** True if the folder is currently synchronizing */
    bool isSyncRunning() const;

    /**
     * return the last sync result with error message and status
     */
    SyncResult syncResult() const;

    /**
      * This is called when the sync folder definition is removed. Do cleanups here.
      *
      * It removes the database, among other things.
      *
      * The folder is not in a valid state afterwards!
      */
    virtual void wipeForRemoval();

    void setSyncState(SyncResult::Status state);

    SyncResult::Status syncState() const;

    void setDirtyNetworkLimits();

    void reloadSyncOptions();

    /**
      * Ignore syncing of hidden files or not. This is defined in the
      * folder definition
      */
    bool ignoreHiddenFiles();
    void setIgnoreHiddenFiles(bool ignore);

    // TODO: don't expose
    SyncJournalDb *journalDb()
    {
        return &_journal;
    }
    // TODO: don't expose
    SyncEngine &syncEngine()
    {
        return *_engine;
    }

    Vfs &vfs()
    {
        OC_ENFORCE(_vfs);
        return *_vfs;
    }

    auto lastSyncTime() const { return QDateTime::currentDateTime().addMSecs(-msecSinceLastSync().count()); }
    std::chrono::milliseconds msecSinceLastSync() const { return std::chrono::milliseconds(_timeSinceLastSyncDone.elapsed()); }
    std::chrono::milliseconds msecLastSyncDuration() const { return _lastSyncDuration; }

    /**
      * Returns whether a file inside this folder should be excluded.
      */
    bool isFileExcludedAbsolute(const QString &fullPath) const;

    /**
      * Returns whether a file inside this folder should be excluded.
      */
    bool isFileExcludedRelative(const QString &relativePath) const;

    /** virtual files of some kind are enabled
     *
     * This is independent of whether new files will be virtual. It's possible to have this enabled
     * and never have an automatic virtual file. But when it's on, the shell context menu will allow
     * users to make existing files virtual.
     */
    bool virtualFilesEnabled() const;
    void setVirtualFilesEnabled(bool enabled);

    /** Whether this folder should show selective sync ui */
    bool supportsSelectiveSync() const;

    /**
     * The folder is deployed by an admin
     * We will hide the remove option and the disable/enable vfs option.
     */
    bool isDeployed() const;

    Vfs::Mode vfsMode() const;

    uint32_t priority();

    void setPriority(uint32_t p);

    static Result<void, QString> checkPathLength(const QString &path);

    /**
     *
     * @return The corresponding space object or null
     */
    GraphApi::Space *space() const;

Q_SIGNALS:
    void syncStateChange();
    void syncStarted();
    void syncFinished(const SyncResult &result);
    void syncPausedChanged(Folder *, bool paused);
    void canSyncChanged();
    void spaceChanged();
    void isReadyChanged();
    void isSyncRunningChanged();


    /**
     * Fires for each change inside this folder that wasn't caused
     * by sync activity.
     */
    void watchedFileChangedExternally(const QString &path);

public Q_SLOTS:
    void openInWebBrowser();

    /**
      * Starts a sync operation
      *
      * If the list of changed files is known, it is passed.
      */
    void startSync();

    void slotDiscardDownloadProgress();
    int slotWipeErrorBlacklist();

    /**
       * Triggered by the folder watcher when a file/dir in this folder
       * changes. Needs to check whether this change should trigger a new
       * sync run to be scheduled.
       */
    void slotWatchedPathsChanged(const QSet<QString> &paths, ChangeReason reason);

    /** Ensures that the next sync performs a full local discovery. */
    void slotNextSyncFullLocalDiscovery();

    /** Adds the path to the local discovery list
     *
     * A weaker version of slotNextSyncFullLocalDiscovery() that just
     * schedules all parent and child items of the path for local
     * discovery.
     */
    void schedulePathForLocalDiscovery(const QString &relativePath);

    /// Reloads the excludes, used when changing the user-defined excludes after saving them to disk.
    bool reloadExcludes();

private Q_SLOTS:
    void slotSyncFinished(bool);

    /** Adds a error message that's not tied to a specific item.
     */
    void slotSyncError(const QString &message, ErrorCategory category = ErrorCategory::Normal);

    void slotItemCompleted(const SyncFileItemPtr &);

    /** Adjust sync result based on conflict data from IssuesWidget.
     *
     * This is pretty awkward, but IssuesWidget just keeps better track
     * of conflicts across partial local discovery.
     */
    void slotFolderConflicts(Folder *folder, const QStringList &conflictPaths);

    /** Warn users if they create a file or folder that is selective-sync excluded */
    void warnOnNewExcludedItem(const SyncJournalFileRecord &record, QStringView path);

    /** Warn users about an unreliable folder watcher */
    void slotWatcherUnreliable(const QString &message);

private:
    /** Create a new Folder
     */
    Folder(const FolderDefinition &definition, const AccountStatePtr &accountState, std::unique_ptr<Vfs> &&vfs, QObject *parent = nullptr);


    void showSyncResultPopup();

    bool checkLocalPath();

    SyncOptions loadSyncOptions();

    void setIsReady(bool b);

    /**
     * Sets up this folder's folderWatcher if possible.
     *
     * May be called several times.
     */
    void registerFolderWatcher();

    enum LogStatus {
        LogStatusRemove,
        LogStatusRename,
        LogStatusMove,
        LogStatusNew,
        LogStatusError,
        LogStatusConflict,
        LogStatusUpdated
    };

    void createGuiLog(const QString &filename, LogStatus status, int count,
        const QString &renameTarget = QString());

    void startVfs();

    AccountStatePtr _accountState;
    FolderDefinition _definition;
    QString _canonicalLocalPath; // As returned with QFileInfo:canonicalFilePath.  Always ends with "/"

    SyncResult _syncResult;
    QScopedPointer<SyncEngine> _engine;
    QElapsedTimer _timeSinceLastSyncDone;
    QElapsedTimer _timeSinceLastSyncStart;
    QElapsedTimer _timeSinceLastFullLocalDiscovery;
    std::chrono::milliseconds _lastSyncDuration = {};

    /// The number of syncs that failed in a row.
    /// Reset when a sync is successful.
    int _consecutiveFailingSyncs = 0;

    /// The number of requested follow-up syncs.
    /// Reset when no follow-up is requested.
    int _consecutiveFollowUpSyncs = 0;

    mutable SyncJournalDb _journal;

    QScopedPointer<SyncRunFileLog> _fileLog;

    QTimer _scheduleSelfTimer;

    /**
     * Setting up vfs is a async operation
     */
    bool _vfsIsReady = false;

    /**
     * Watches this folder's local directory for changes.
     *
     * Created by registerFolderWatcher(), triggers slotWatchedPathsChanged()
     */
    QScopedPointer<FolderWatcher> _folderWatcher;

    /**
     * Keeps track of locally dirty files so we can skip local discovery sometimes.
     */
    QScopedPointer<LocalDiscoveryTracker> _localDiscoveryTracker;

    /**
     * The vfs mode instance (created by plugin) to use. Never null.
     */
    QSharedPointer<Vfs> _vfs;

    friend class FolderMan;
};
}
