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


#pragma once

#include "gui/opencloudguilib.h"

#include "folder.h"
#include "scheduling/syncscheduler.h"

#include <QList>
#include <QObject>
#include <QQueue>

class TestFolderMigration;

namespace OCC {

class FolderMan;
namespace TestUtils {
    // prototype for test friend
    FolderMan *folderMan();
}

class Application;
class SyncResult;
class SocketApi;
class LockWatcher;

/**
 * @brief Return object for Folder::trayOverallStatus.
 * @ingroup gui
 */
class TrayOverallStatusResult
{
public:
    QDateTime lastSyncDone;

    void addResult(Folder *f);
    const SyncResult &overallStatus() const;

private:
    SyncResult _overallStatus;
};

class OPENCLOUD_GUI_EXPORT FolderMan : public QObject
{
    Q_OBJECT
public:
    /**
     * For a new folder, the type guides what kind of checks are done to ensure the new folder is not embedded in an existing one.
     * Or in case of a space folder, that if the new folder is in a Space sync root, it is the sync root of the same account.
     */
    enum class NewFolderType {
        SpacesSyncRoot,
        SpacesFolder,
    };

    struct SyncConnectionDescription
    {
        /***
-         * The WebDAV URL for the sync connection.
         */
        QUrl davUrl;

        /***
         * The id of the space
         */
        QString spaceId;

        /***
         * The local folder used for the sync.
         */
        QString localPath;

        /***
         * The Space name to display in the list of folders or an empty string.
         */
        QString displayName;

        /***
         * Wether to use virtual files.
         */
        bool useVirtualFiles;

        uint32_t priority;

        QSet<QString> selectiveSyncBlackList;
    };

    static QString suggestSyncFolder(NewFolderType folderType, const QUuid &accountUuid);

    static QString checkPathValidityRecursive(const QString &path, FolderMan::NewFolderType folderType, const QUuid &accountUuid);

    static std::unique_ptr<FolderMan> createInstance();
    ~FolderMan() override;

    /**
     * Helper to access the FolderMan instance
     * Warning: may be null in unit tests
     */
    // TODO: use acces throug ocApp and remove that instance pointer
    static FolderMan *instance();

    /// \returns empty if a downgrade of a folder was detected, otherwise it will return the number
    ///          of folders that were set up (note: this can be zero when no folders were configured).
    std::optional<qsizetype> loadFolders();

    const QVector<Folder *> &folders() const;

    /** Adds a folder for an account, ensures the journal is gone and saves it in the settings.
      */
    Folder *addFolder(const AccountStatePtr &accountState, const FolderDefinition &folderDefinition);

    /**
     * Adds a folder for an account. Used to be part of the wizard code base. Constructs the folder definition from the parameters.
     * In case Wizard::SyncMode::SelectiveSync is used, nullptr is returned.
     */
    Folder *addFolderFromWizard(const AccountStatePtr &accountStatePtr, FolderDefinition &&definition, bool useVfs);
    Folder *addFolderFromFolderWizardResult(const AccountStatePtr &accountStatePtr, const SyncConnectionDescription &config);

    /** Removes a folder */
    void removeFolder(Folder *);

    /**
     * Returns the folder which the file or directory stored in path is in
     *
     * Optionally, the path relative to the found folder is returned in
     * relativePath.
     */
    Folder *folderForPath(const QString &path, QString *relativePath = nullptr);

    /**
     * Ensures that a given directory does not contain a sync journal file.
     *
     * @returns false if the journal could not be removed, true otherwise.
     */
    static bool ensureJournalGone(const QString &journalDbFile);

    /// Produce text for use in the tray tooltip
    static QString trayTooltipStatusString(const SyncResult &result, bool paused);

    /**
     * Compute status summarizing multiple folders
     * @return tuple containing folders, status, unresolvedConflicts and lastSyncDone
     */
    static TrayOverallStatusResult trayOverallStatus(const QVector<Folder *> &folders);

    SocketApi *socketApi();

    /**
     * Check if @a path is a valid path for a new folder considering the already sync'ed items.
     * Make sure that this folder, or any subfolder is not sync'ed already.
     *
     * @returns an empty string if it is allowed, or an error if it is not allowed
     */
    QString checkPathValidityForNewFolder(const QString &path, NewFolderType folderType, const QUuid &accountUuid) const;

    /**
     * Attempts to find a non-existing, acceptable path for creating a new sync folder.
     *
     * Uses \a basePath as the baseline. It'll return this path if it's acceptable.
     *
     * Note that this can fail. If someone syncs ~ and \a basePath is ~/OpenCloud, no
     * subfolder of ~ would be a good candidate. When that happens \a basePath
     * is returned.
     */
    static QString findGoodPathForNewSyncFolder(const QString &basePath, const QString &newFolder, NewFolderType folderType, const QUuid &accountUuid);

    /**
     * While ignoring hidden files can theoretically be switched per folder,
     * it's currently a global setting that users can only change for all folders
     * at once.
     * These helper functions can be removed once it's properly per-folder.
     */
    bool ignoreHiddenFiles() const;
    void setIgnoreHiddenFiles(bool ignore);

    /**
     * Returns true if any folder is currently syncing.
     *
     * This might be a FolderMan-scheduled sync, or a externally
     * managed sync like a placeholder hydration.
     */
    bool isAnySyncRunning() const;

    /** Removes all folders */
    void unloadAndDeleteAllFolders();

    /**
     * If enabled is set to false, no new folders will start to sync.
     * The current one will finish.
     */
    void setSyncEnabled(bool);

    SyncScheduler *scheduler() { return _scheduler; }


    /** Queues all folders for syncing. */
    void scheduleAllFolders();

    void setDirtyProxy();
    void setDirtyNetworkLimits();

    /** Whether or not vfs is supported in the location. */
    bool checkVfsAvailability(const QString &path, Vfs::Mode mode = VfsPluginManager::instance().bestAvailableVfsMode()) const;

    /** If the folder configuration is no longer supported this will return an error string */
    Result<void, QString> unsupportedConfiguration(const QString &path) const;

    [[nodiscard]] bool isSpaceSynced(GraphApi::Space *space) const;

Q_SIGNALS:
    /**
      * signal to indicate a folder has changed its sync state.
      *
      * Attention: The folder may be zero. Do a general update of the state then.
      */
    void folderSyncStateChange(Folder *);

    /**
     * Emitted whenever the list of configured folders changes.
     */
    void folderListChanged();
    void folderRemoved(Folder *folder);

public Q_SLOTS:

    /**
     * Schedules folders of newly connected accounts, terminates and
     * de-schedules folders of disconnected accounts.
     */
    void slotIsConnectedChanged();

    /**
     * Triggers a sync run once the lock on the given file is removed.
     *
     * Automatically detemines the folder that's responsible for the file.
     * See slotWatchedFileUnlocked().
     */
    void slotSyncOnceFileUnlocks(const QString &path, FileSystem::LockMode mode);

    /// This slot will tell all sync engines to reload the sync options.
    void slotReloadSyncOptions();

private Q_SLOTS:
    void slotFolderCanSyncChanged();

    void slotRemoveFoldersForAccount(const AccountStatePtr &accountState);

    void slotServerVersionChanged(Account *account);

private:
    explicit FolderMan();

    [[nodiscard]] static bool prepareFolder(const QString &folder);

    /** Adds a new folder, does not add it to the account settings and
     *  does not set an account on the new folder.
      */
    Folder *addFolderInternal(FolderDefinition folderDefinition,
        const AccountStatePtr &accountState, std::unique_ptr<Vfs> vfs);

    /* unloads a folder object, does not delete it */
    void unloadFolder(Folder *);

    void saveFolders();

    // finds all folder configuration files
    // and create the folders
    QString getBackupName(QString fullPathName) const;

    // makes the folder known to the socket api
    void registerFolderWithSocketApi(Folder *folder);

    QVector<Folder *> _folders;
    QString _folderConfigPath;

    /// Folder aliases from the settings that weren't read
    QSet<QString> _additionalBlockedFolderAliases;

    /// Watches files that couldn't be synced due to locks
    QScopedPointer<LockWatcher> _lockWatcher;

    /// Scheduled folders that should be synced as soon as possible
    SyncScheduler *_scheduler;

    std::unique_ptr<SocketApi> _socketApi;

    mutable QMap<QString, Result<void, QString>> _unsupportedConfigurationError;

    static FolderMan *_instance;
    friend class OCC::Application;
    friend class ::TestFolderMigration;
};

} // namespace OCC
