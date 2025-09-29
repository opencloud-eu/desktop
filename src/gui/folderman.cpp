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

#include "folderman.h"

#include "account.h"
#include "accountmanager.h"
#include "accountstate.h"
#include "common/asserts.h"
#include "configfile.h"
#include "gui/folder.h"
#include "gui/folderdefinition.h"
#include "gui/networkinformation.h"
#include "guiutility.h"
#include "libsync/syncengine.h"
#include "lockwatcher.h"
#include "scheduling/syncscheduler.h"
#include "socketapi/socketapi.h"
#include "syncresult.h"
#include "theme.h"

#ifdef Q_OS_WIN
#include "common/utility_win.h"
#endif

#include <QMessageBox>
#include <QMutableSetIterator>
#include <QNetworkProxy>
#include <QtCore>

using namespace Qt::Literals::StringLiterals;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

QAnyStringView foldersC()
{
    return QAnyStringView("Folders");
}

qsizetype numberOfSyncJournals(const QString &path)
{
    return QDir(path).entryList({ QStringLiteral(".sync_*.db"), QStringLiteral("._sync_*.db") }, QDir::Hidden | QDir::Files).size();
}
}

namespace OCC {
Q_LOGGING_CATEGORY(lcFolderMan, "gui.folder.manager", QtInfoMsg)

void TrayOverallStatusResult::addResult(Folder *f)
{
    _overallStatus._numNewConflictItems += f->syncResult()._numNewConflictItems;
    _overallStatus._numErrorItems += f->syncResult()._numErrorItems;
    _overallStatus._numBlacklistErrors += f->syncResult()._numBlacklistErrors;

    auto time = f->lastSyncTime();
    if (time > lastSyncDone) {
        lastSyncDone = time;
    }

    auto status = f->isSyncPaused() || NetworkInformation::instance()->isBehindCaptivePortal() || NetworkInformation::instance()->isMetered()
        ? SyncResult::Paused
        : f->syncResult().status();
    if (status == SyncResult::Undefined) {
        status = SyncResult::Problem;
    }
    if (status > _overallStatus.status()) {
        _overallStatus.setStatus(status);
    }
}

const SyncResult &TrayOverallStatusResult::overallStatus() const
{
    return _overallStatus;
}

FolderMan *FolderMan::_instance = nullptr;

FolderMan::FolderMan()
    : _lockWatcher(new LockWatcher)
    , _scheduler(new SyncScheduler(this))
    , _socketApi(new SocketApi)
{
#ifdef Q_OS_WIN
    auto updateSyncRoots = [] {
        // set the metadata for a sync root, depending on the number of accounts
        for (const auto &accountStatePtr : AccountManager::instance()->accounts()) {
            if (accountStatePtr->account()->hasDefaultSyncRoot()) {
                Folder::prepareFolder(accountStatePtr->account()->defaultSyncRoot(),
                    AccountManager::instance()->accounts().size() == 1
                        ? Theme::instance()->appNameGUI()
                        : u"%1 - %2"_s.arg(Theme::instance()->appNameGUI(), accountStatePtr->account()->davDisplayName()),
                    accountStatePtr->account()->davDisplayName());
            }
        }
    };
    updateSyncRoots();
    connect(AccountManager::instance(), &AccountManager::accountAdded, this, updateSyncRoots);
#endif
    connect(AccountManager::instance(), &AccountManager::accountRemoved,
        this, &FolderMan::slotRemoveFoldersForAccount);

    connect(_lockWatcher.data(), &LockWatcher::fileUnlocked, this, [this](const QString &path, FileSystem::LockMode) {
        if (Folder *f = folderForPath(path)) {
            // Treat this equivalently to the file being reported by the file watcher
            f->slotWatchedPathsChanged({path}, Folder::ChangeReason::UnLock);
        }
    });
}

FolderMan *FolderMan::instance()
{
    Q_ASSERT(_instance);
    return _instance;
}

FolderMan::~FolderMan()
{
    unloadAndDeleteAllFolders();
    qDeleteAll(_folders);
    _instance = nullptr;
}

const QVector<Folder *> &FolderMan::folders() const
{
    return _folders;
}

void FolderMan::unloadFolder(Folder *f)
{
    Q_ASSERT(f);

    _folders.removeAll(f);
    _socketApi->slotUnregisterPath(f);


    if (!f->hasSetupError()) {
        disconnect(f, nullptr, _socketApi.get(), nullptr);
        disconnect(f, nullptr, this, nullptr);
        disconnect(f, nullptr, &f->syncEngine().syncFileStatusTracker(), nullptr);
        disconnect(&f->syncEngine(), nullptr, f, nullptr);
        disconnect(
            &f->syncEngine().syncFileStatusTracker(), &SyncFileStatusTracker::fileStatusChanged, _socketApi.get(), &SocketApi::broadcastStatusPushMessage);
    }
}

void FolderMan::unloadAndDeleteAllFolders()
{
    if (!_folders.isEmpty()) {
        // clear the list of existing folders.
        saveFolders();
        const auto folders = std::move(_folders);
        for (auto *folder : folders) {
            // unload and disconnect all signals
            unloadFolder(folder);
            folder->deleteLater();
        }
    }
}

void FolderMan::registerFolderWithSocketApi(Folder *folder)
{
    if (!folder)
        return;
    if (!QDir(folder->path()).exists())
        return;

    // register the folder with the socket API
    if (folder->canSync())
        _socketApi->slotRegisterPath(folder);
}

std::optional<qsizetype> FolderMan::loadFolders()
{
    qCInfo(lcFolderMan) << u"Setup folders from settings file";

    auto settings = ConfigFile::makeQSettings();
    const auto size = settings.beginReadArray(foldersC());

    for (auto i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        FolderDefinition folderDefinition = FolderDefinition::load(settings);

        if (SyncJournalDb::dbIsTooNewForClient(folderDefinition.absoluteJournalPath())) {
            continue;
        }

        auto vfs = VfsPluginManager::instance().createVfsFromPlugin(folderDefinition.virtualFilesMode);
        if (!vfs) {
            // TODO: Must do better error handling
            qFatal("Could not load plugin");
        }
        auto account = AccountManager::instance()->account(folderDefinition.accountUUID());
        if (account.isNull()) {
            qFatal("Could not load account");
        }
        addFolderInternal(std::move(folderDefinition), account, std::move(vfs));
    }
    settings.endArray();

    Q_EMIT folderListChanged();

    return _folders.size();
}

void FolderMan::saveFolders()
{
    auto settings = ConfigFile::makeQSettings();
    settings.remove(foldersC());
    settings.beginWriteArray(foldersC(), _folders.size());
    int i = 0;
    for (const auto folder : std::as_const(_folders)) {
        settings.setArrayIndex(i++);
        auto definitionToSave = folder->_definition;
        // with spaces we rely on the space id
        // we save the dav URL nevertheless to have it available during startup
        definitionToSave.setWebDavUrl(folder->webDavUrl());
        definitionToSave.setDisplayName(folder->displayName());
        FolderDefinition::save(settings, definitionToSave);
    }
    settings.endArray();
}

bool FolderMan::ensureJournalGone(const QString &journalDbFile)
{
    // remove the old journal file
    while (QFile::exists(journalDbFile) && !QFile::remove(journalDbFile)) {
        qCWarning(lcFolderMan) << u"Could not remove old db file at" << journalDbFile;
        int ret = QMessageBox::warning(nullptr, tr("Could not reset folder state"),
            tr("An old sync journal %1 was found, "
               "but could not be removed. Please make sure "
               "that no application is currently using it.")
                .arg(QDir::fromNativeSeparators(QDir::cleanPath(journalDbFile))),
            QMessageBox::Retry | QMessageBox::Abort);
        if (ret == QMessageBox::Abort) {
            return false;
        }
    }
    return true;
}

SocketApi *FolderMan::socketApi()
{
    return _socketApi.get();
}

void FolderMan::slotFolderSyncPaused(Folder *f, bool paused)
{
    if (!f) {
        qCCritical(lcFolderMan) << u"slotFolderSyncPaused called with empty folder";
        return;
    }

    if (!paused) {
        _disabledFolders.remove(f);
        if (f->canSync()) {
            scheduler()->enqueueFolder(f);
        }
    } else {
        _disabledFolders.insert(f);
    }
}

void FolderMan::slotFolderCanSyncChanged()
{
    Folder *f = qobject_cast<Folder *>(sender());
    OC_ASSERT(f);
    if (f->canSync()) {
        _socketApi->slotRegisterPath(f);
    } else {
        _socketApi->slotUnregisterPath(f);
    }
}

void FolderMan::scheduleAllFolders()
{
    for (auto *f : std::as_const(_folders)) {
        if (f && f->canSync()) {
            scheduler()->enqueueFolder(f);
        }
    }
}

void FolderMan::slotSyncOnceFileUnlocks(const QString &path, FileSystem::LockMode mode)
{
    _lockWatcher->addFile(path, mode);
}

void FolderMan::slotIsConnectedChanged()
{
    AccountStatePtr accountState(qobject_cast<AccountState *>(sender()));
    if (!accountState) {
        return;
    }
    QString accountName = accountState->account()->displayNameWithHost();

    if (accountState->isConnected()) {
        qCInfo(lcFolderMan) << u"Account" << accountName << u"connected, scheduling its folders";

        for (auto *f : std::as_const(_folders)) {
            if (f->accountState() == accountState && f->canSync()) {
                scheduler()->enqueueFolder(f);
            }
        }
    } else if (accountState->state() == AccountState::State::Disconnected || accountState->state() == AccountState::State::SignedOut) {
        qCInfo(lcFolderMan) << u"Account" << accountName
                            << u"disconnected or paused, "
                               "terminating or descheduling sync folders";

        if (scheduler()->currentSync() && scheduler()->currentSync()->accountState() == accountState) {
            scheduler()->terminateCurrentSync(tr("Account disconnected or paused"));
        }
    }
}

// only enable or disable foldermans will schedule and do syncs.
// this is not the same as Pause and Resume of folders.
void FolderMan::setSyncEnabled(bool enabled)
{
    if (enabled) {
        // We have things in our queue that were waiting for the connection to come back on.
        scheduler()->start();
    } else {
        scheduler()->stop();
    }
    qCInfo(lcFolderMan) << Q_FUNC_INFO << enabled;
    // force a redraw in case the network connect status changed
    Q_EMIT folderSyncStateChange(nullptr);
}

void FolderMan::slotRemoveFoldersForAccount(const AccountStatePtr &accountState)
{
    QList<Folder *> foldersToRemove;
    // reserve a magic number
    foldersToRemove.reserve(16);
    for (auto *folder : std::as_const(_folders)) {
        if (folder->accountState() == accountState) {
            foldersToRemove.append(folder);
        }
    }
    for (const auto &f : foldersToRemove) {
        removeFolder(f);
    }
}

void FolderMan::slotServerVersionChanged(Account *account)
{
    // Pause folders if the server version is unsupported
    if (account->serverSupportLevel() == Account::ServerSupportLevel::Unsupported) {
        qCWarning(lcFolderMan) << u"The server version is unsupported:" << account->capabilities().status().versionString()
                               << u"pausing all folders on the account";

        for (auto &f : std::as_const(_folders)) {
            if (f->accountState()->account().data() == account) {
                f->setSyncPaused(true);
            }
        }
    }
}

bool FolderMan::isAnySyncRunning() const
{
    if (_scheduler->hasCurrentRunningSyncRunning()) {
        return true;
    }

    for (auto f : _folders) {
        if (f->isSyncRunning())
            return true;
    }
    return false;
}

void FolderMan::slotFolderSyncStarted()
{
    auto f = qobject_cast<Folder *>(sender());
    OC_ASSERT(f);
    if (!f)
        return;

    qCInfo(lcFolderMan) << u">========== Sync started for folder [" << f->shortGuiLocalPath() << u"] of account ["
                        << f->accountState()->account()->displayNameWithHost() << u"]";
}

/*
  * a folder indicates that its syncing is finished.
  * Start the next sync after the system had some milliseconds to breath.
  * This delay is particularly useful to avoid late file change notifications
  * (that we caused ourselves by syncing) from triggering another spurious sync.
  */
void FolderMan::slotFolderSyncFinished(const SyncResult &)
{
    auto f = qobject_cast<Folder *>(sender());
    OC_ASSERT(f);
    if (!f)
        return;

    qCInfo(lcFolderMan) << u"<========== Sync finished for folder [" << f->shortGuiLocalPath() << u"] of account ["
                        << f->accountState()->account()->displayNameWithHost() << u"]";
}

Folder *FolderMan::addFolder(const AccountStatePtr &accountState, const FolderDefinition &folderDefinition)
{
    // Choose a db filename
    auto definition = folderDefinition;
    definition.journalPath = SyncJournalDb::makeDbName(folderDefinition.localPath());

    if (!ensureJournalGone(definition.absoluteJournalPath())) {
        return nullptr;
    }

    auto vfs = VfsPluginManager::instance().createVfsFromPlugin(folderDefinition.virtualFilesMode);
    if (!vfs) {
        qCWarning(lcFolderMan) << u"Could not load plugin for mode" << folderDefinition.virtualFilesMode;
        return nullptr;
    }

    auto folder = addFolderInternal(definition, accountState, std::move(vfs));

    if (folder) {
        Q_EMIT folderSyncStateChange(folder);
        Q_EMIT folderListChanged();
    }
    saveFolders();

    return folder;
}

Folder *FolderMan::addFolderInternal(
    FolderDefinition folderDefinition,
    const AccountStatePtr &accountState,
    std::unique_ptr<Vfs> vfs)
{
    auto folder = new Folder(folderDefinition, accountState, std::move(vfs), this);

    qCInfo(lcFolderMan) << u"Adding folder to Folder Map " << folder << folder->path();
    _folders.push_back(folder);
    if (folder->isSyncPaused()) {
        _disabledFolders.insert(folder);
    }

    // See matching disconnects in unloadFolder().
    if (!folder->hasSetupError()) {
        connect(folder, &Folder::syncStateChange, _socketApi.get(), [folder, this] { _socketApi->slotUpdateFolderView(folder); });
        connect(folder, &Folder::syncStarted, this, &FolderMan::slotFolderSyncStarted);
        connect(folder, &Folder::syncFinished, this, &FolderMan::slotFolderSyncFinished);
        connect(folder, &Folder::syncStateChange, this, [folder, this] { Q_EMIT folderSyncStateChange(folder); });
        connect(folder, &Folder::syncPausedChanged, this, &FolderMan::slotFolderSyncPaused);
        connect(folder, &Folder::canSyncChanged, this, &FolderMan::slotFolderCanSyncChanged);
        connect(
            &folder->syncEngine().syncFileStatusTracker(), &SyncFileStatusTracker::fileStatusChanged, _socketApi.get(), &SocketApi::broadcastStatusPushMessage);
        connect(folder, &Folder::watchedFileChangedExternally, &folder->syncEngine().syncFileStatusTracker(), &SyncFileStatusTracker::slotPathTouched);
        registerFolderWithSocketApi(folder);
    }
    return folder;
}

Folder *FolderMan::folderForPath(const QString &path, QString *relativePath)
{
    QString absolutePath = QDir::cleanPath(path) + QLatin1Char('/');

    for (auto *folder : std::as_const(_folders)) {
        const QString folderPath = folder->cleanPath() + QLatin1Char('/');

        if (absolutePath.startsWith(folderPath, (Utility::isWindows() || Utility::isMac()) ? Qt::CaseInsensitive : Qt::CaseSensitive)) {
            if (relativePath) {
                *relativePath = absolutePath.mid(folderPath.length());
                relativePath->chop(1); // we added a '/' above
            }
            return folder;
        }
    }

    if (relativePath)
        relativePath->clear();
    return nullptr;
}

void FolderMan::removeFolder(Folder *f)
{
    if (!OC_ENSURE(f)) {
        return;
    }

    qCInfo(lcFolderMan) << u"Removing " << f->path();

    if (scheduler()->currentSync() == f) {
        // abort the sync now
        scheduler()->terminateCurrentSync(tr("Folder is about to be removed"));
    }

    f->setSyncPaused(true);
    f->wipeForRemoval();

    unloadFolder(f);

    Q_EMIT folderRemoved(f);
    Q_EMIT folderListChanged();

    f->deleteLater();
    saveFolders();
}

QString FolderMan::getBackupName(QString fullPathName) const
{
    if (fullPathName.endsWith(QLatin1String("/")))
        fullPathName.chop(1);

    if (fullPathName.isEmpty())
        return QString();

    QString newName = fullPathName + tr(" (backup)");
    QFileInfo fi(newName);
    int cnt = 2;
    do {
        if (fi.exists()) {
            newName = fullPathName + tr(" (backup %1)").arg(cnt++);
            fi.setFile(newName);
        }
    } while (fi.exists());

    return newName;
}

void FolderMan::setDirtyProxy()
{
    for (auto *f : std::as_const(_folders)) {
        if (f) {
            if (f->accountState() && f->accountState()->account()
                && f->accountState()->account()->accessManager()) {
                // Need to do this so we do not use the old determined system proxy
                f->accountState()->account()->accessManager()->setProxy(
                    QNetworkProxy(QNetworkProxy::DefaultProxy));
            }
        }
    }
}

void FolderMan::setDirtyNetworkLimits()
{
    for (auto *f : std::as_const(_folders)) {
        // set only in busy folders. Otherwise they read the config anyway.
        if (f && f->isSyncRunning()) {
            f->setDirtyNetworkLimits();
        }
    }
}

TrayOverallStatusResult FolderMan::trayOverallStatus(const QVector<Folder *> &folders)
{
    TrayOverallStatusResult result;

    // if one of them has an error -> show error
    // if one is paused, but others ok, show ok
    //
    for (auto *folder : folders) {
        result.addResult(folder);
    }
    return result;
}

QString FolderMan::trayTooltipStatusString(
    const SyncResult &result, bool paused)
{
    QString folderMessage;
    switch (result.status()) {
    case SyncResult::Success:
        [[fallthrough]];
    case SyncResult::Problem:
        if (result.hasUnresolvedConflicts()) {
            folderMessage = tr("Sync was successful, unresolved conflicts.");
            break;
        }
        [[fallthrough]];
    default:
        return Utility::enumToDisplayName(result.status());
    }
    if (paused) {
        // sync is disabled.
        folderMessage = tr("%1 (Sync is paused)").arg(folderMessage);
    }
    return folderMessage;
}

// QFileInfo::canonicalPath returns an empty string if the file does not exist.
// This function also works with files that does not exist and resolve the symlinks in the
// parent directories.
static QString canonicalPath(const QString &path)
{
    QFileInfo selFile(path);
    if (!selFile.exists()) {
        const auto parentPath = selFile.dir().path();

        // It's possible for the parentPath to match the path
        // (possibly we've arrived at a non-existant drive root on Windows)
        // and recursing would be fatal.
        if (parentPath == path) {
            return path;
        }

        return canonicalPath(parentPath) + QLatin1Char('/') + selFile.fileName();
    }
    return selFile.canonicalFilePath();
}

static QString checkPathForSyncRootMarkingRecursive(const QString &path, FolderMan::NewFolderType folderType, const QUuid &accountUuid)
{
    std::pair<QString, QUuid> existingTags = Utility::getDirectorySyncRootMarkings(path);
    if (!existingTags.first.isEmpty()) {
        if (existingTags.first != Theme::instance()->orgDomainName()) {
            // another application uses this as spaces root folder
            return FolderMan::tr("The folder »%1« is already in use by application %2!").arg(path, existingTags.first);
        }

        // Looks good, it's our app, let's check the account tag:
        switch (folderType) {
        case FolderMan::NewFolderType::SpacesFolder:
            if (existingTags.second == accountUuid) {
                // Nice, that's what we like, the sync root for our account in our app. No error.
                return {};
            }
            [[fallthrough]];
        case FolderMan::NewFolderType::SpacesSyncRoot:
            // It's our application but we don't want to create a spaces folder, so it must be another space root
            return FolderMan::tr("The folder »%1« is already in use by another account.").arg(path);
        }
    }

    QString parent = QFileInfo(path).path();
    if (parent == path) { // root dir, stop recursing
        return {};
    }

    return checkPathForSyncRootMarkingRecursive(parent, folderType, accountUuid);
}

QString FolderMan::checkPathValidityRecursive(const QString &path, FolderMan::NewFolderType folderType, const QUuid &accountUuid)
{
    if (path.isEmpty()) {
        return FolderMan::tr("No valid folder selected!");
    }

#ifdef Q_OS_WIN
    QNtfsPermissionCheckGuard ntfs_perm;
#endif

    auto pathLenghtCheck = Folder::checkPathLength(path);
    if (!pathLenghtCheck) {
        return pathLenghtCheck.error();
    }

    const QFileInfo selectedPathInfo(path);
    if (!selectedPathInfo.exists()) {
        const QString parentPath = selectedPathInfo.path();
        if (parentPath != path) {
            return checkPathValidityRecursive(parentPath, folderType, accountUuid);
        }
        return FolderMan::tr("The selected path does not exist!");
    }

    if (numberOfSyncJournals(selectedPathInfo.filePath()) != 0) {
        return FolderMan::tr("The folder »%1« is used in a folder sync connection!").arg(QDir::toNativeSeparators(selectedPathInfo.filePath()));
    }

    // At this point we know there is no syncdb in the parent hyrarchy, check for spaces sync root.

    if (!selectedPathInfo.isDir()) {
        return FolderMan::tr("The selected path is not a folder!");
    }

    if (!selectedPathInfo.isWritable()) {
        return FolderMan::tr("You have no permission to write to the selected folder!");
    }

    return checkPathForSyncRootMarkingRecursive(path, folderType, accountUuid);
}

/*
 *  - spaces sync root not in syncdb folder
 *  - spaces sync root not in another spaces sync root
 *
 *  - space not in syncdb folder
 *  - space *can* be in sync root
 *  - space not in spaces sync root of other account (check with account uuid)
 */
QString FolderMan::checkPathValidityForNewFolder(const QString &path, NewFolderType folderType, const QUuid &accountUuid) const
{
    // check if the local directory isn't used yet in another sync
    const auto cs = Utility::fsCaseSensitivity();

    const QString userDir = QDir::cleanPath(canonicalPath(path)) + QLatin1Char('/');
    for (auto f : _folders) {
        const QString folderDir = QDir::cleanPath(canonicalPath(f->path())) + QLatin1Char('/');

        if (QString::compare(folderDir, userDir, cs) == 0) {
            return tr("There is already a sync from the server to this local folder. "
                      "Please pick another local folder!");
        }
        if (FileSystem::isChildPathOf(folderDir, userDir)) {
            return tr("The local folder »%1« already contains a folder used in a folder sync connection. "
                      "Please pick another local folder!")
                .arg(QDir::toNativeSeparators(path));
        }

        if (FileSystem::isChildPathOf(userDir, folderDir)) {
            return tr("The local folder »%1« is already contained in a folder used in a folder sync connection. "
                      "Please pick another local folder!")
                .arg(QDir::toNativeSeparators(path));
        }
    }

    const auto result = checkPathValidityRecursive(path, folderType, accountUuid);
    if (!result.isEmpty()) {
        return tr("Please pick another local folder for »%1«.").arg(result);
    }
    return {};
}

QString FolderMan::findGoodPathForNewSyncFolder(
    const QString &basePath, const QString &newFolder, FolderMan::NewFolderType folderType, const QUuid &accountUuid)
{
    OC_ASSERT(!accountUuid.isNull() || folderType == FolderMan::NewFolderType::SpacesSyncRoot);

    // reserve extra characters to allow appending of a number
    const QString normalisedPath = FileSystem::createPortableFileName(basePath, FileSystem::pathEscape(newFolder), std::string_view(" (100)").size());

    // If the parent folder is a sync folder or contained in one, we can't
    // possibly find a valid sync folder inside it.
    // Example: Someone syncs their home directory. Then ~/foobar is not
    // going to be an acceptable sync folder path for any value of foobar.
    if (FolderMan::instance()->folderForPath(QFileInfo(normalisedPath).canonicalPath())) {
        // Any path with that parent is going to be unacceptable,
        // so just keep it as-is.
        return canonicalPath(normalisedPath);
    }
    // Count attempts and give up eventually
    {
        QString folder = normalisedPath;
        for (int attempt = 2; attempt <= 100; ++attempt) {
            if (!QFileInfo::exists(folder) && FolderMan::instance()->checkPathValidityForNewFolder(folder, folderType, accountUuid).isEmpty()) {
                return canonicalPath(folder);
            }
            folder = normalisedPath + QStringLiteral(" (%1)").arg(attempt);
        }
    }
    // we failed to find a non existing path
    Q_ASSERT(false);
    return canonicalPath(normalisedPath);
}

bool FolderMan::ignoreHiddenFiles() const
{
    if (_folders.empty()) {
        return true;
    }
    return _folders.first()->ignoreHiddenFiles();
}

void FolderMan::setIgnoreHiddenFiles(bool ignore)
{
    // Note that the setting will revert to 'true' if all folders
    // are deleted...
    for (auto *folder : std::as_const(_folders)) {
        folder->setIgnoreHiddenFiles(ignore);
    }
    saveFolders();
}

Result<void, QString> FolderMan::unsupportedConfiguration(const QString &path) const
{
    auto it = _unsupportedConfigurationError.find(path);
    if (it == _unsupportedConfigurationError.end()) {
        it = _unsupportedConfigurationError.insert(path, [&]() -> Result<void, QString> {
            if (numberOfSyncJournals(path) > 1) {
                const QString error = tr("Multiple accounts are sharing the folder »%1«.\n"
                                         "This configuration is know to lead to dataloss and is no longer supported.\n"
                                         "Please consider removing this folder from the account and adding it again.")
                                          .arg(path);
                if (Theme::instance()->warnOnMultipleDb()) {
                    qCWarning(lcFolderMan) << error;
                    return error;
                } else {
                    qCWarning(lcFolderMan) << error << u"this error is not displayed to the user as this is a branded"
                                           << u"client and the error itself might be a false positive caused by a previous broken migration";
                }
            }
            return {};
        }());
    }
    return *it;
}

bool FolderMan::isSpaceSynced(GraphApi::Space *space) const
{
    auto it = std::find_if(_folders.cbegin(), _folders.cend(), [space](auto f) { return f->space() == space; });
    return it != _folders.cend();
}

void FolderMan::slotReloadSyncOptions()
{
    for (auto *f : std::as_const(_folders)) {
        if (f) {
            f->reloadSyncOptions();
        }
    }
}

bool FolderMan::checkVfsAvailability(const QString &path, Vfs::Mode mode) const
{
    return unsupportedConfiguration(path) && Vfs::checkAvailability(path, mode);
}

Folder *FolderMan::addFolderFromWizard(const AccountStatePtr &accountStatePtr, FolderDefinition &&folderDefinition, bool useVfs)
{
    if (!FolderMan::prepareFolder(folderDefinition.localPath())) {
        return {};
    }

    folderDefinition.ignoreHiddenFiles = ignoreHiddenFiles();

    if (useVfs) {
        folderDefinition.virtualFilesMode = VfsPluginManager::instance().bestAvailableVfsMode();
    }

    auto newFolder = addFolder(accountStatePtr, folderDefinition);

    if (newFolder) {
        qCDebug(lcFolderMan) << u"Local sync folder" << folderDefinition.localPath() << u"successfully created!";
    } else {
        qCWarning(lcFolderMan) << u"Failed to create local sync folder!";
    }
    return newFolder;
}

Folder *FolderMan::addFolderFromFolderWizardResult(const AccountStatePtr &accountStatePtr, const SyncConnectionDescription &description)
{
    FolderDefinition definition{accountStatePtr->account()->uuid(), description.davUrl, description.spaceId, description.displayName};
    definition.setLocalPath(description.localPath);
    auto f = addFolderFromWizard(accountStatePtr, std::move(definition), description.useVirtualFiles);
    if (f) {
        f->journalDb()->setSelectiveSyncList(SyncJournalDb::SelectiveSyncBlackList, description.selectiveSyncBlackList);
        f->setPriority(description.priority);
    }
    return f;
}

QString FolderMan::suggestSyncFolder(NewFolderType folderType, const QUuid &accountUuid)
{
    return FolderMan::instance()->findGoodPathForNewSyncFolder(QDir::homePath(), Theme::instance()->appName(), folderType, accountUuid);
}

bool FolderMan::prepareFolder(const QString &folder)
{
    if (!QFileInfo::exists(folder)) {
        if (!OC_ENSURE(QDir().mkpath(folder))) {
            return false;
        }
        FileSystem::setFolderMinimumPermissions(folder);
        Folder::prepareFolder(folder);
    }
    return true;
}

std::unique_ptr<FolderMan> FolderMan::createInstance()
{
    OC_ASSERT(!_instance);
    _instance = new FolderMan();
    return std::unique_ptr<FolderMan>(_instance);
}

} // namespace OCC
