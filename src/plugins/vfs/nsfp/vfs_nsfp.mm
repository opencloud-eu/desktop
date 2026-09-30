// VfsNSFP implementation -- macOS NSFileProvider-based virtual file system plugin.
//             fileStatusChanged, and eviction integration.

#include "vfs_nsfp.h"

#import <os/log.h>

#include "nsfpdomainmanager.h"
#include "nsfpxpchandler.h"

#include "common/pinstate.h"
#include "common/syncjournaldb.h"
#include "libsync/account.h"
#include "creds/abstractcredentials.h"
#include "creds/httpcredentials.h"
#include "syncengine.h"
#include "syncfileitem.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMap>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QVector>

#include <sys/mount.h>
#include <sys/xattr.h>

#include <sqlite3.h>

#import <Foundation/Foundation.h>

// Shared constants from the FileProvider extension header.
#import "FileProviderXPCService.h"

Q_LOGGING_CATEGORY(lcVfsNSFP, "sync.vfs.nsfp", QtInfoMsg)

using namespace OCC;

/// Writes the WebDAV URL and access token to the App Group shared container
/// so the FileProvider extension can download file contents directly from the server.
static void syncConfigToSharedContainer(const VfsSetupParams &params, const QString &domainId)
{
    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) {
        qCWarning(lcVfsNSFP) << "syncConfigToSharedContainer: cannot access App Group container";
        return;
    }

    // Per-domain config file so multiple accounts/spaces don't overwrite each other.
    NSString *configFilename = [NSString stringWithFormat:@"fileprovider_config_%@.plist",
                                domainId.toNSString()];

    // Extract access token from credentials.
    QString accessToken;
    if (auto *httpCreds = qobject_cast<OCC::HttpCredentials *>(params.account->credentials())) {
        accessToken = httpCreds->accessToken();
    }

    if (accessToken.isEmpty()) {
        NSURL *existingConfig = [containerURL URLByAppendingPathComponent:configFilename];
        NSData *existingData = [NSData dataWithContentsOfURL:existingConfig];
        if (existingData) {
            NSDictionary *existing = [NSPropertyListSerialization propertyListWithData:existingData
                                                                              options:NSPropertyListImmutable
                                                                               format:nil error:nil];
            NSString *existingToken = existing[@"accessToken"];
            if (existingToken && existingToken.length > 0) {
                qCInfo(lcVfsNSFP) << "syncConfigToSharedContainer: skipping write — token empty but existing config has valid token";
                return;
            }
        }
        qCWarning(lcVfsNSFP) << "syncConfigToSharedContainer: writing config with empty token (credentials not yet available)";
    }

    auto davUrl = params.baseUrl().toString(QUrl::FullyEncoded);
    davUrl.replace(QLatin1Char('$'), QStringLiteral("%24"));

    NSDictionary *config = @{
        @"davUrl": davUrl.toNSString(),
        @"accessToken": accessToken.toNSString(),
    };

    NSURL *configURL = [containerURL URLByAppendingPathComponent:configFilename];
    NSError *error = nil;
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:config
                                                             format:NSPropertyListBinaryFormat_v1_0
                                                            options:0
                                                              error:&error];
    if (!data || error) {
        qCWarning(lcVfsNSFP) << "syncConfigToSharedContainer: failed to serialize config:" << error.localizedDescription.UTF8String;
        return;
    }

    [data writeToURL:configURL atomically:YES];
    [[NSFileManager defaultManager] setAttributes:@{NSFilePosixPermissions: @0644}
                                     ofItemAtPath:configURL.path
                                            error:nil];
    qCInfo(lcVfsNSFP) << "syncConfigToSharedContainer: wrote config for domain" << domainId << "davUrl" << davUrl;

    // One-time cleanup: remove legacy global files (and their stale caches)
    // only if they still exist. Once removed, this block is a no-op.
    {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSURL *legacyConfig = [containerURL URLByAppendingPathComponent:@"fileprovider_config.plist"];
        NSURL *legacyItems = [containerURL URLByAppendingPathComponent:@"fileprovider_items.plist"];
        BOOL hasLegacy = [fm fileExistsAtPath:legacyConfig.path]
                      || [fm fileExistsAtPath:legacyItems.path];
        if (hasLegacy) {
            [fm removeItemAtURL:legacyConfig error:nil];
            [fm removeItemAtURL:legacyItems error:nil];
            qCInfo(lcVfsNSFP) << "Removed legacy plist files";
            // Also remove stale prevFileIds caches from the legacy era.
            NSArray *contents = [fm contentsOfDirectoryAtPath:containerURL.path error:nil];
            for (NSString *name in contents) {
                if ([name hasPrefix:@"prevFileIds_"]) {
                    [fm removeItemAtURL:[containerURL URLByAppendingPathComponent:name] error:nil];
                    qCInfo(lcVfsNSFP) << "Removed stale cache:" << QString::fromNSString(name);
                }
            }
        }
    }
}

/// Writes all file records from the sync journal to a plist file in the
/// App Group shared container so the FileProvider extension can enumerate items
/// without needing an XPC connection to the main app.
static void syncMetadataToSharedContainer(SyncJournalDb *journal, const VfsSetupParams &params, const QString &domainId)
{
    if (!journal) {
        qCWarning(lcVfsNSFP) << "syncMetadataToSharedContainer: no journal";
        return;
    }

    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) {
        qCWarning(lcVfsNSFP) << "syncMetadataToSharedContainer: App Group container not accessible";
        return;
    }

    // Collect all file records from the journal.
    // First pass: collect records and build a path → fileId map.
    struct ItemInfo {
        QString path;
        QString name;
        QString fileId;
        QString parentPath;
        bool isDirectory;
        int64_t size;
        time_t modtime;
        QString etag;
        bool isVirtualFile;
    };

    QVector<ItemInfo> records;
    QMap<QString, QString> pathToFileId;
    int totalCallbacks = 0;
    int invalidCount = 0;
    int dirCount = 0;
    int virtualFileCount = 0;
    int otherCount = 0;

    journal->getFilesBelowPath(QString(), [&](const SyncJournalFileRecord &rec) {
        totalCallbacks++;
        if (!rec.isValid()) {
            invalidCount++;
            return;
        }

        ItemInfo info;
        info.path = rec.path();
        info.name = rec.name();
        info.fileId = QString::fromUtf8(rec.fileId());
        info.isDirectory = rec.isDirectory();
        info.size = rec.size();
        info.modtime = rec.modtime();
        info.etag = rec.etag();
        info.isVirtualFile = rec.isVirtualFile();

        if (info.isDirectory) {
            dirCount++;
        } else if (info.isVirtualFile) {
            virtualFileCount++;
        } else {
            otherCount++;
        }

        // Derive parent path.
        const auto lastSlash = info.path.lastIndexOf(QLatin1Char('/'));
        info.parentPath = (lastSlash > 0) ? info.path.left(lastSlash) : QString();

        pathToFileId[info.path] = info.fileId;
        records.append(info);
    });

    os_log_info(OS_LOG_DEFAULT, "syncMetadataToSharedContainer: callbacks=%d invalid=%d dirs=%d virtualFiles=%d other=%d records=%d",
                 totalCallbacks, invalidCount, dirCount, virtualFileCount, otherCount, (int)records.size());

    // If the journal query returned no virtual files, they may have been deleted
    // by WAL operations (e.g., discovery marking them as stale because no local
    // placeholder exists in NSFP mode). Fall back to reading the base DB directly
    // with immutable=1 to recover virtual file records.
    if (virtualFileCount == 0) {
        const auto dbPath = journal->databaseFilePath();
        const auto uri = QStringLiteral("file://%1?immutable=1").arg(dbPath);
        sqlite3 *db = nullptr;
        int rc = sqlite3_open_v2(uri.toUtf8().constData(), &db,
                                 SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
        if (rc == SQLITE_OK && db) {
            sqlite3_stmt *stmt = nullptr;
            rc = sqlite3_prepare_v2(db,
                "SELECT path, fileid, filesize, modtime, md5 FROM metadata WHERE type=4",
                -1, &stmt, nullptr);
            if (rc == SQLITE_OK && stmt) {
                int recoveredCount = 0;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    ItemInfo info;
                    info.path = QString::fromUtf8(
                        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
                    info.fileId = QString::fromUtf8(
                        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
                    info.size = sqlite3_column_int64(stmt, 2);
                    info.modtime = sqlite3_column_int64(stmt, 3);
                    info.etag = QString::fromUtf8(
                        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4)));
                    info.isDirectory = false;
                    info.isVirtualFile = true;

                    // Derive name and parent path from path.
                    const auto lastSlash = info.path.lastIndexOf(QLatin1Char('/'));
                    info.name = (lastSlash >= 0) ? info.path.mid(lastSlash + 1) : info.path;
                    info.parentPath = (lastSlash > 0) ? info.path.left(lastSlash) : QString();

                    // Only add if not already in records (avoid duplicates).
                    bool alreadyPresent = false;
                    for (const auto &existing : records) {
                        if (existing.path == info.path) {
                            alreadyPresent = true;
                            break;
                        }
                    }
                    if (!alreadyPresent) {
                        pathToFileId[info.path] = info.fileId;
                        records.append(info);
                        recoveredCount++;
                    }
                }
                sqlite3_finalize(stmt);
                os_log_fault(OS_LOG_DEFAULT,
                    "syncMetadataToSharedContainer: recovered %d virtual files from base DB",
                    recoveredCount);
            }
            sqlite3_close(db);
        } else {
            os_log_fault(OS_LOG_DEFAULT,
                "syncMetadataToSharedContainer: failed to open immutable DB: %{public}s",
                sqlite3_errmsg(db));
            if (db) sqlite3_close(db);
        }
    }

    // Compute the davUrl for this space so each item carries its own WebDAV
    // base URL. This prevents cross-space confusion when multiple spaces are
    // synced simultaneously (each space has a different davUrl).
    auto davUrl = params.baseUrl().toString(QUrl::FullyEncoded);
    davUrl.replace(QLatin1Char('$'), QStringLiteral("%24"));
    NSString *nsDavUrl = davUrl.toNSString();

    // Second pass: resolve parent file IDs and build the plist array.
    NSMutableArray<NSDictionary *> *items = [NSMutableArray arrayWithCapacity:records.size()];
    for (const auto &info : records) {
        NSString *parentId;
        if (info.parentPath.isEmpty()) {
            parentId = NSFileProviderRootContainerItemIdentifier;
        } else {
            const auto it = pathToFileId.find(info.parentPath);
            parentId = (it != pathToFileId.end()) ? it.value().toNSString()
                                                   : NSFileProviderRootContainerItemIdentifier;
        }

        NSDictionary *dict = @{
            @"fileId" : info.fileId.toNSString() ?: @"",
            @"filename" : info.name.toNSString() ?: @"",
            @"path" : info.path.toNSString() ?: @"",
            @"parentPath" : info.parentPath.toNSString() ?: @"",
            @"parentId" : parentId,
            @"isDirectory" : @(info.isDirectory),
            @"size" : @(info.size),
            @"modtime" : @(info.modtime),
            @"etag" : info.etag.toNSString() ?: @"",
            @"isVirtualFile" : @(info.isVirtualFile),
            @"isDownloaded" : @(info.isDirectory || !info.isVirtualFile),
            @"davUrl" : nsDavUrl,
        };
        [items addObject:dict];
    }

    // Preserve items that were added by the FileProvider extension (e.g. via
    // createItemBasedOnTemplate) but haven't been synced to the journal yet.
    // Without this merge, syncMetadataToSharedContainer would overwrite the
    // plist and the extension-created items would be reported as deleted by
    // the enumerator's change-detection diff.
    NSString *itemsFilename = [NSString stringWithFormat:@"fileprovider_items_%@.plist",
                               domainId.toNSString()];
    NSURL *metadataURL = [containerURL URLByAppendingPathComponent:itemsFilename];
    {
        NSData *existingData = [NSData dataWithContentsOfURL:metadataURL];
        if (existingData) {
            NSArray *existingItems = [NSPropertyListSerialization propertyListWithData:existingData
                                                                              options:NSPropertyListImmutable
                                                                               format:nil error:nil];
            if ([existingItems isKindOfClass:[NSArray class]]) {
                // Build a set of fileIds from the journal so we can quickly
                // check whether an existing plist entry is already covered.
                NSMutableSet<NSString *> *journalFileIds = [NSMutableSet setWithCapacity:items.count];
                // Also track paths to detect items at the same location.
                NSMutableSet<NSString *> *journalPaths = [NSMutableSet setWithCapacity:items.count];
                for (NSDictionary *item in items) {
                    [journalFileIds addObject:item[@"fileId"] ?: @""];
                    [journalPaths addObject:item[@"path"] ?: @""];
                }

                // Only preserve extension-created items that are recent (< 120s).
                // After that the sync engine should have discovered them. If they
                // are still not in the journal, they were deleted on the server.
                int64_t now = (int64_t)[[NSDate date] timeIntervalSince1970];
                static const int64_t MAX_PRESERVE_AGE = 120; // seconds

                int preservedCount = 0;
                for (NSDictionary *existing in existingItems) {
                    BOOL isExtCreated = [existing[@"extensionCreated"] boolValue];
                    int64_t movedAt = [existing[@"movedAt"] longLongValue];

                    if (!isExtCreated && movedAt == 0) {
                        // Regular journal item — not preserved.
                        continue;
                    }

                    // Check TTL for both extension-created and moved items.
                    int64_t timestamp = isExtCreated
                        ? [existing[@"extensionCreatedAt"] longLongValue]
                        : movedAt;
                    if (timestamp > 0 && (now - timestamp) > MAX_PRESERVE_AGE) {
                        continue;
                    }

                    NSString *existingId = existing[@"fileId"] ?: @"";
                    NSString *existingPath = existing[@"path"] ?: @"";
                    if (![journalFileIds containsObject:existingId]
                        && ![journalPaths containsObject:existingPath]) {
                        [items addObject:existing];
                        preservedCount++;
                    }
                }
                if (preservedCount > 0) {
                    qCInfo(lcVfsNSFP) << "syncMetadataToSharedContainer: preserved"
                                      << preservedCount << "extension-created items not yet in journal";
                }
            }
        }
    }

    NSError *writeError = nil;
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:items
                                                             format:NSPropertyListBinaryFormat_v1_0
                                                            options:0
                                                              error:&writeError];
    if (data && !writeError) {
        [data writeToURL:metadataURL atomically:YES];
        qCInfo(lcVfsNSFP) << "syncMetadataToSharedContainer: wrote" << items.count
                          << "items to" << QString::fromNSString(metadataURL.path);
    } else {
        qCWarning(lcVfsNSFP) << "syncMetadataToSharedContainer: write failed:"
                              << QString::fromNSString(writeError.localizedDescription);
    }
}

/// Reads the metadata plist for the given domain and returns the set of unique
/// parent container identifiers (folder fileIds). Used to determine which folder
/// enumerators need signalling after a sync cycle so that deletions in
/// subdirectories are detected.
static QSet<QString> collectParentContainerIds(const QString &domainId)
{
    QSet<QString> result;

    NSURL *containerURL = [[NSFileManager defaultManager]
        containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
    if (!containerURL) return result;

    NSString *filename = [NSString stringWithFormat:@"fileprovider_items_%@.plist",
                          domainId.toNSString()];
    NSURL *url = [containerURL URLByAppendingPathComponent:filename];
    NSData *data = [NSData dataWithContentsOfURL:url];
    if (!data) return result;

    NSArray *items = [NSPropertyListSerialization propertyListWithData:data
                                                              options:NSPropertyListImmutable
                                                               format:nil error:nil];
    if (![items isKindOfClass:[NSArray class]]) return result;

    for (NSDictionary *item in items) {
        NSString *parentId = item[@"parentId"];
        if (parentId.length > 0
            && ![parentId isEqualToString:NSFileProviderRootContainerItemIdentifier]) {
            result.insert(QString::fromNSString(parentId));
        }
    }

    return result;
}

VfsNSFP::VfsNSFP(QObject *parent)
    : Vfs(parent)
{
}

VfsNSFP::~VfsNSFP()
{
    stop();
}

Vfs::Mode VfsNSFP::mode() const
{
    return Vfs::Mode::MacOSNSFileProvider;
}

QString VfsNSFP::domainIdentifier() const
{
    return _domainId;
}

void VfsNSFP::stop()
{
    _pollTimer.stop();

    // Tear down the XPC handler first so the extension gets a clean disconnect.
    if (_xpcHandler) {
        _xpcHandler->stopListener();
        _xpcHandler.reset();
    }

    if (!_domainManager || _domainId.isEmpty()) {
        qCDebug(lcVfsNSFP) << "stop() called but no domain manager or domain ID set";
        return;
    }

    qCInfo(lcVfsNSFP) << "stop() — invalidating manager for domain:" << _domainId;
    _domainManager->invalidateManager(_domainId);
}

void VfsNSFP::unregisterFolder()
{
    if (!_domainManager || _domainId.isEmpty()) {
        qCDebug(lcVfsNSFP) << "unregisterFolder() called but no domain manager or domain ID set";
        return;
    }

    qCInfo(lcVfsNSFP) << "unregisterFolder() — removing domain:" << _domainId;

    // Capture a pointer to this for the completion handler
    QPointer<VfsNSFP> self(this);
    const auto domainId = _domainId;

    _domainManager->removeDomain(domainId, [self, domainId](const QString &errorMessage) {
        if (!self) {
            return;
        }

        if (errorMessage.isEmpty()) {
            QMetaObject::invokeMethod(self, [self, domainId]() {
                if (self) {
                    qCInfo(lcVfsNSFP) << "Domain removed successfully:" << domainId;
                }
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(self, [self, errorMessage]() {
                if (self) {
                    qCWarning(lcVfsNSFP) << "Failed to remove domain:" << errorMessage;
                    Q_EMIT self->error(errorMessage);
                }
            }, Qt::QueuedConnection);
        }
    });

    _domainId.clear();
}

bool VfsNSFP::socketApiPinStateActionsShown() const
{
    return true;
}

Result<void, QString> VfsNSFP::createPlaceholder(const SyncFileItem &item)
{
    qCInfo(lcVfsNSFP) << "createPlaceholder() for:" << item.localName()
                      << "fileId:" << item._fileId << "type:" << item._type;

    if (!_domainManager || _domainId.isEmpty()) {
        return {tr("Cannot create placeholder: domain not registered")};
    }

    // Write a journal record marking this item as a virtual file (dehydrated placeholder).
    auto *journal = params().journal;
    if (!journal) {
        return {tr("Cannot create placeholder: no sync journal available")};
    }

    // Create a journal record from the sync file item with virtual file type.
    auto record = SyncJournalFileRecord::fromSyncFileItem(item);
    const auto result = journal->setFileRecord(record);
    if (!result) {
        const auto errorMsg = result.error();
        qCWarning(lcVfsNSFP) << "Failed to write journal record:" << errorMsg;
        return {errorMsg};
    }

    // Determine the parent container identifier. If the file is at root level,
    // use an empty string which signalEnumerator maps to NSFileProviderRootContainerItemIdentifier.
    const auto localName = item.localName();
    const auto lastSlash = localName.lastIndexOf(QLatin1Char('/'));
    QString parentContainerId;

    if (lastSlash > 0) {
        // Has a parent folder -- look up its fileId from the journal.
        const auto parentPath = localName.left(lastSlash);
        const auto parentRecord = journal->getFileRecord(parentPath);
        if (parentRecord.isValid()) {
            parentContainerId = QString::fromUtf8(parentRecord.fileId());
        }
    }
    // If parentContainerId is empty, signalEnumerator will use root container.

    // Update the shared metadata file so the extension can see the new item.
    syncMetadataToSharedContainer(journal, params(), _domainId);

    // Signal the File Provider framework to re-enumerate the parent container
    // so Finder picks up the new placeholder.
    _domainManager->signalEnumerator(_domainId, parentContainerId);

    qCInfo(lcVfsNSFP) << "Placeholder created successfully for:" << item.localName();
    return {};
}

bool VfsNSFP::needsMetadataUpdate(const SyncFileItem &item)
{
    // Check the journal for the current record and compare metadata fields.
    auto *journal = params().journal;
    if (!journal) {
        return true;
    }

    const auto record = journal->getFileRecord(item.localName());
    if (!record.isValid()) {
        // No record means we need to create one.
        return true;
    }

    // If etag, modtime, or size differ from what the journal has, we need an update.
    if (record.etag() != item._etag) {
        return true;
    }
    if (record.modtime() != item._modtime) {
        return true;
    }
    if (record.size() != item._size) {
        return true;
    }

    return false;
}

bool VfsNSFP::isDehydratedPlaceholder(const QString &filePath)
{
    // For NSFP the journal is the source of truth for placeholder state.
    // Derive the relative path from the absolute filePath.
    auto *journal = params().journal;
    if (!journal) {
        return false;
    }

    const auto fsPath = params().filesystemPath();
    QString relPath = filePath;
    if (relPath.startsWith(fsPath)) {
        relPath = relPath.mid(fsPath.length());
        if (relPath.startsWith(QLatin1Char('/'))) {
            relPath = relPath.mid(1);
        }
    }

    const auto record = journal->getFileRecord(relPath);
    if (!record.isValid()) {
        return false;
    }

    // If the journal record says virtual file, it is a dehydrated placeholder.
    if (record.isVirtualFile()) {
        return true;
    }

    // If the record says it is a regular file, it is not dehydrated.
    return false;
}

LocalInfo VfsNSFP::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    // During local discovery, check the journal to determine if a file should
    // be treated as a virtual (dehydrated) file. For NSFP the journal is the
    // source of truth since the framework manages the on-disk state.
    if (type == ItemTypeFile) {
        auto *journal = params().journal;
        if (journal) {
            const auto fsPath = std::filesystem::path(params().filesystemPath().toStdString());
            const auto relStdPath = std::filesystem::relative(path.path(), fsPath);
            const auto relPath = QString::fromStdString(relStdPath.generic_string());

            const auto record = journal->getFileRecord(relPath);
            if (record.isValid()) {
                if (record.type() == ItemTypeVirtualFile) {
                    // Check pin state to decide if it wants to be downloaded.
                    const auto pinSt = pinState(relPath);
                    if (pinSt && *pinSt == PinState::AlwaysLocal) {
                        type = ItemTypeVirtualFileDownload;
                    } else {
                        type = ItemTypeVirtualFile;
                    }
                } else if (record.type() == ItemTypeFile) {
                    // Check if the file should be dehydrated.
                    const auto pinSt = pinState(relPath);
                    if (pinSt && *pinSt == PinState::OnlineOnly) {
                        type = ItemTypeVirtualFileDehydration;
                    }
                }
            }
        }
    }

    qCDebug(lcVfsNSFP) << "statTypeVirtualFile:" << path.path().c_str() << Utility::enumToString(type);
    return LocalInfo(path, type);
}

bool VfsNSFP::setPinState(const QString &relFilePath, PinState state)
{
    qCInfo(lcVfsNSFP) << "setPinState()" << relFilePath << static_cast<int>(state);

    // Store in the in-memory map.
    _pinStates[relFilePath] = state;

    if (!_domainManager || _domainId.isEmpty()) {
        qCWarning(lcVfsNSFP) << "setPinState: domain not registered";
        return false;
    }

    // For AlwaysLocal, trigger hydration of the file (if dehydrated).
    if (state == PinState::AlwaysLocal) {
        auto *journal = params().journal;
        if (journal) {
            const auto record = journal->getFileRecord(relFilePath);
            if (record.isValid() && record.isVirtualFile()) {
                qCInfo(lcVfsNSFP) << "setPinState: AlwaysLocal — triggering hydration for:" << relFilePath;
                // Signal the enumerator so the extension picks up the changed pin state
                // and can request hydration.
                QString parentContainerId;
                const auto lastSlash = relFilePath.lastIndexOf(QLatin1Char('/'));
                if (lastSlash > 0) {
                    const auto parentPath = relFilePath.left(lastSlash);
                    const auto parentRecord = journal->getFileRecord(parentPath);
                    if (parentRecord.isValid()) {
                        parentContainerId = QString::fromUtf8(parentRecord.fileId());
                    }
                }
                _domainManager->signalEnumerator(_domainId, parentContainerId);
            }
        }
    }

    // For OnlineOnly, trigger eviction of the file (free local data).
    if (state == PinState::OnlineOnly) {
        auto *journal = params().journal;
        if (journal) {
            const auto record = journal->getFileRecord(relFilePath);
            if (record.isValid() && !record.isVirtualFile()) {
                qCInfo(lcVfsNSFP) << "setPinState: OnlineOnly — triggering eviction for:" << relFilePath;
                const auto fileId = QString::fromUtf8(record.fileId());
                _domainManager->evictItem(_domainId, fileId, [relFilePath](const QString &errorMsg) {
                    if (errorMsg.isEmpty()) {
                        qCInfo(lcVfsNSFP) << "Eviction succeeded for:" << relFilePath;
                    } else {
                        qCWarning(lcVfsNSFP) << "Eviction failed for:" << relFilePath << errorMsg;
                    }
                });
            }
        }
    }

    return true;
}

Optional<PinState> VfsNSFP::pinState(const QString &relFilePath)
{
    // Walk up the path to find the effective pin state (inherited resolution).
    auto it = _pinStates.constFind(relFilePath);
    if (it != _pinStates.constEnd()) {
        const auto state = it.value();
        if (state != PinState::Inherited) {
            return state;
        }
    }

    // Walk up parent directories to resolve inheritance.
    QString path = relFilePath;
    while (true) {
        const auto lastSlash = path.lastIndexOf(QLatin1Char('/'));
        if (lastSlash <= 0) {
            // Check root
            auto rootIt = _pinStates.constFind(QString());
            if (rootIt != _pinStates.constEnd() && rootIt.value() != PinState::Inherited) {
                return rootIt.value();
            }
            break;
        }
        path = path.left(lastSlash);
        auto parentIt = _pinStates.constFind(path);
        if (parentIt != _pinStates.constEnd() && parentIt.value() != PinState::Inherited) {
            return parentIt.value();
        }
    }

    // No explicit state found -- default to Unspecified for NSFP.
    return PinState::Unspecified;
}

Vfs::AvailabilityResult VfsNSFP::availability(const QString &folderPath)
{
    // Check pin state first.
    const auto basePinSt = pinState(folderPath);
    if (basePinSt) {
        switch (*basePinSt) {
        case PinState::AlwaysLocal:
            return VfsItemAvailability::AlwaysLocal;
        case PinState::OnlineOnly:
            return VfsItemAvailability::OnlineOnly;
        case PinState::Inherited:
        case PinState::Unspecified:
        case PinState::Excluded:
            break;
        }
    }

    // Check the journal record for hydration status.
    auto *journal = params().journal;
    if (!journal) {
        return AvailabilityError::DbError;
    }

    const auto record = journal->getFileRecord(folderPath);
    if (!record.isValid()) {
        return AvailabilityError::NoSuchItem;
    }

    if (record.isDirectory()) {
        // For directories, check children.
        bool hasHydrated = false;
        bool hasDehydrated = false;
        journal->listFilesInPath(folderPath, [&hasHydrated, &hasDehydrated](const SyncJournalFileRecord &child) {
            if (child.isVirtualFile()) {
                hasDehydrated = true;
            } else if (child.isFile()) {
                hasHydrated = true;
            }
        });

        if (hasHydrated && hasDehydrated) {
            return VfsItemAvailability::Mixed;
        }
        if (hasDehydrated) {
            return VfsItemAvailability::AllDehydrated;
        }
        return VfsItemAvailability::AllHydrated;
    }

    // Single file
    if (record.isVirtualFile()) {
        return VfsItemAvailability::AllDehydrated;
    }
    return VfsItemAvailability::AllHydrated;
}

void VfsNSFP::fileStatusChanged(const QString &systemFileName, SyncFileStatus fileStatus)
{
    if (!_domainManager || _domainId.isEmpty()) {
        return;
    }

    qCDebug(lcVfsNSFP) << "fileStatusChanged:" << systemFileName << fileStatus.tag();

    // Derive the parent container identifier to signal the correct enumerator.
    auto *journal = params().journal;
    if (!journal) {
        return;
    }

    // Convert system path to relative path.
    const auto filesystemPath = params().filesystemPath();
    QString relPath = systemFileName;
    if (relPath.startsWith(filesystemPath)) {
        relPath = relPath.mid(filesystemPath.length());
        if (relPath.startsWith(QLatin1Char('/'))) {
            relPath = relPath.mid(1);
        }
    }

    // Determine the parent container identifier for signalling.
    QString parentContainerId;
    const auto lastSlash = relPath.lastIndexOf(QLatin1Char('/'));
    if (lastSlash > 0) {
        const auto parentPath = relPath.left(lastSlash);
        const auto parentRecord = journal->getFileRecord(parentPath);
        if (parentRecord.isValid()) {
            parentContainerId = QString::fromUtf8(parentRecord.fileId());
        }
    }

    switch (fileStatus.tag()) {
    case SyncFileStatus::StatusSync:
        // File is syncing -- signal enumerator so Finder shows a progress indicator.
        qCDebug(lcVfsNSFP) << "StatusSync — signalling enumerator for:" << relPath;
        _domainManager->signalEnumerator(_domainId, parentContainerId);
        break;

    case SyncFileStatus::StatusUpToDate:
        // File is synced -- signal enumerator so Finder shows a checkmark badge.
        qCDebug(lcVfsNSFP) << "StatusUpToDate — signalling enumerator for:" << relPath;
        _domainManager->signalEnumerator(_domainId, parentContainerId);
        break;

    case SyncFileStatus::StatusError:
        // File has an error -- signal enumerator so Finder shows an error badge.
        qCDebug(lcVfsNSFP) << "StatusError — signalling enumerator for:" << relPath;
        _domainManager->signalEnumerator(_domainId, parentContainerId);
        break;

    case SyncFileStatus::StatusExcluded:
        // Mark excluded files with the Excluded pin state.
        setPinState(relPath, PinState::Excluded);
        break;

    case SyncFileStatus::StatusWarning:
        // File has a warning -- signal enumerator so Finder shows a warning badge.
        qCDebug(lcVfsNSFP) << "StatusWarning — signalling enumerator for:" << relPath;
        _domainManager->signalEnumerator(_domainId, parentContainerId);
        break;

    case SyncFileStatus::StatusNone:
        // No specific action for StatusNone.
        break;
    }
}

Result<Vfs::ConvertToPlaceholderResult, QString> VfsNSFP::updateMetadata(
    const SyncFileItem &item, const QString &filePath, const QString &replacesFile)
{
    Q_UNUSED(replacesFile)

    qCInfo(lcVfsNSFP) << "updateMetadata() for:" << item.localName()
                      << "filePath:" << filePath << "fileId:" << item._fileId;

    if (!_domainManager || _domainId.isEmpty()) {
        return {tr("Cannot update metadata: domain not registered")};
    }

    // Update the journal record with the latest metadata.
    auto *journal = params().journal;
    if (!journal) {
        return {tr("Cannot update metadata: no sync journal available")};
    }

    auto record = SyncJournalFileRecord::fromSyncFileItem(item);
    const auto result = journal->setFileRecord(record);
    if (!result) {
        const auto errorMsg = result.error();
        qCWarning(lcVfsNSFP) << "Failed to update journal record:" << errorMsg;
        return {errorMsg};
    }

    // Determine parent container for the signal.
    const auto localName = item.localName();
    const auto lastSlash = localName.lastIndexOf(QLatin1Char('/'));
    QString parentContainerId;

    if (lastSlash > 0) {
        const auto parentPath = localName.left(lastSlash);
        const auto parentRecord = journal->getFileRecord(parentPath);
        if (parentRecord.isValid()) {
            parentContainerId = QString::fromUtf8(parentRecord.fileId());
        }
    }

    // Update the shared metadata so the extension sees the changes.
    syncMetadataToSharedContainer(journal, params(), _domainId);

    // Signal the File Provider framework to refresh Finder's view.
    _domainManager->signalEnumerator(_domainId, parentContainerId);

    qCInfo(lcVfsNSFP) << "Metadata updated successfully for:" << item.localName();
    return Vfs::ConvertToPlaceholderResult::Ok;
}

void VfsNSFP::startImpl(const VfsSetupParams &params)
{
    qCInfo(lcVfsNSFP) << "startImpl() — registering NSFileProvider domain";

    // Volume type check: NSFileProvider requires APFS or HFS+ filesystem.
    const auto syncRoot = params.filesystemPath();
    struct statfs fsInfo;
    if (statfs(syncRoot.toUtf8().constData(), &fsInfo) == 0) {
        const auto fsType = QString::fromUtf8(fsInfo.f_fstypename);
        if (fsType.compare(QLatin1String("apfs"), Qt::CaseInsensitive) != 0
            && fsType.compare(QLatin1String("hfs"), Qt::CaseInsensitive) != 0) {
            const auto errorMsg = tr("NSFileProvider requires APFS or HFS+ volume, but sync root is on %1").arg(fsType);
            qCWarning(lcVfsNSFP) << errorMsg;
            Q_EMIT error(errorMsg);
            return;
        }
        qCInfo(lcVfsNSFP) << "Volume type check passed:" << fsType;
    } else {
        qCWarning(lcVfsNSFP) << "Failed to stat filesystem for sync root:" << syncRoot;
    }

    // Detect existing xattr placeholders from a prior xattr VFS mode.
    // Check if the sync root directory has extended attributes that indicate
    // it was previously managed by the xattr VFS plugin.
    {
        char attrList[1024];
        const auto listSize = ::listxattr(syncRoot.toUtf8().constData(), attrList, sizeof(attrList), 0);
        if (listSize > 0) {
            // Check if any of the xattrs look like openvfs markers.
            const char *ptr = attrList;
            const char *end = attrList + listSize;
            bool xattrPlaceholderDetected = false;
            while (ptr < end) {
                const auto attrName = QString::fromUtf8(ptr);
                if (attrName.contains(QLatin1String("openvfs"), Qt::CaseInsensitive)
                    || attrName.contains(QLatin1String("opencloud"), Qt::CaseInsensitive)) {
                    xattrPlaceholderDetected = true;
                    break;
                }
                ptr += strlen(ptr) + 1;
            }
            if (xattrPlaceholderDetected) {
                qCWarning(lcVfsNSFP) << "Existing xattr placeholders detected — manual resync may be required after switching to NSFileProvider mode";
            }
        }
    }

    // Instantiate the domain manager if not already present
    if (!_domainManager) {
        _domainManager = std::make_unique<NsfpDomainManager>();
    }

    // Derive a stable domain identifier from account UUID + space ID.
    // Format: "opencloud-{accountUUID}-{spaceId}" (braces stripped from UUID).
    const auto accountUuid = params.account->uuid().toString(QUuid::WithoutBraces);
    const auto spaceId = params.spaceId();
    _domainId = QStringLiteral("opencloud-%1-%2").arg(accountUuid, spaceId);

    // Use the folder display name for the Finder sidebar
    const auto displayName = params.folderDisplayName();

    qCInfo(lcVfsNSFP) << "Domain identifier:" << _domainId << "displayName:" << displayName;

    // Register the domain asynchronously. Bridge result back to Qt thread.
    QPointer<VfsNSFP> self(this);

    // Connect to credential updates BEFORE the async domain registration so we
    // never miss the fetched() signal (it may fire while addDomain is in progress).
    QObject::connect(params.account->credentials(), &AbstractCredentials::fetched,
                     this, [self]() {
        if (self) {
            qCInfo(lcVfsNSFP) << "Credentials fetched — updating extension config";
            syncConfigToSharedContainer(self->params(), self->_domainId);
        }
    });

    // One-time corruption recovery: clear a possibly-corrupted fileproviderd replica
    // (FPCK failures, stuck pending import operations that jam new Finder ops) by
    // force-recreating the domain ONCE after this update. Gated by a per-domain marker
    // in the App Group container so it happens at most once.
    BOOL forceRecreate = NO;
    NSURL *resetMarker = nil;
    {
        NSURL *grp = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:kOpenCloudAppGroupIdentifier];
        if (grp) {
            resetMarker = [grp URLByAppendingPathComponent:
                [NSString stringWithFormat:@"fp_reset_v3_%@", _domainId.toNSString()]];
            forceRecreate = ![[NSFileManager defaultManager] fileExistsAtPath:resetMarker.path];
            if (forceRecreate) {
                qCWarning(lcVfsNSFP) << "One-time domain reset (clearing corrupted state) for:" << _domainId;
            }
        }
    }

    _domainManager->addDomain(_domainId, displayName, [self, resetMarker](const QString &errorMessage) {
        if (!self) {
            return;
        }

        if (errorMessage.isEmpty()) {
            // Mark the one-time reset as done so it never repeats.
            if (resetMarker) {
                [@"done" writeToURL:resetMarker atomically:YES encoding:NSUTF8StringEncoding error:nil];
            }
            QMetaObject::invokeMethod(self, [self]() {
                if (self) {
                    qCInfo(lcVfsNSFP) << "NSFileProvider domain registered successfully";

                    // Start the XPC handler so the extension can reach us.
                    self->_xpcHandler = std::make_unique<NsfpXpcHandler>(self, self);
                    self->_xpcHandler->startListener();

                    // Write initial file metadata to the shared container
                    // so the extension can enumerate items immediately.
                    syncMetadataToSharedContainer(self->params().journal, self->params(), self->_domainId);

                    // Write WebDAV URL + access token so extension can download directly.
                    // The fetched() signal connection was already established before
                    // addDomain to avoid race conditions.
                    syncConfigToSharedContainer(self->params(), self->_domainId);

                    // Signal the enumerator so fileproviderd picks up the new items.
                    self->_domainManager->signalEnumerator(self->_domainId, QString());

                    // After each sync cycle, refresh the shared metadata plist and
                    // signal the extension to re-enumerate. This ensures deleted or
                    // changed files on the server are reflected in Finder.
                    auto *engine = self->params().syncEngine();
                    if (engine) {
                        QObject::connect(engine, &SyncEngine::finished, self, [self](bool success) {
                            if (!self || !self->_domainManager || self->_domainId.isEmpty()) {
                                return;
                            }
                            qCInfo(lcVfsNSFP) << "Sync finished (success=" << success << ") — refreshing shared metadata";

                            // Collect parent container IDs BEFORE updating the plist.
                            // This captures containers that currently have items — if items
                            // are removed (remote delete), the container's enumerator must
                            // be signalled so it can detect the deletion via its prevFileIds diff.
                            const auto oldParentIds = collectParentContainerIds(self->_domainId);

                            syncMetadataToSharedContainer(self->params().journal, self->params(), self->_domainId);
                            syncConfigToSharedContainer(self->params(), self->_domainId);

                            // Collect parent container IDs AFTER updating the plist.
                            const auto newParentIds = collectParentContainerIds(self->_domainId);

                            // Signal all affected parent containers (union of old and new).
                            // Old containers need signalling to detect item deletions/moves-out.
                            // New containers need signalling to detect item additions/moves-in.
                            auto allParentIds = oldParentIds;
                            allParentIds.unite(newParentIds);

                            if (!allParentIds.isEmpty()) {
                                qCInfo(lcVfsNSFP) << "Signalling" << allParentIds.size()
                                                  << "parent container enumerators after sync";
                            }
                            for (const auto &parentId : allParentIds) {
                                self->_domainManager->signalEnumerator(self->_domainId, parentId);
                            }

                            // Signal root container.
                            self->_domainManager->signalEnumerator(self->_domainId, QString());

                            // Signal working set — this enumerator covers ALL items across
                            // all folders and is the most reliable way to detect deletions
                            // in subdirectories (its prevFileIds cache spans everything).
                            self->_domainManager->signalWorkingSet(self->_domainId);

                            self->_domainManager->requestSystemEviction(self->_domainId);
                        });
                    }

                    // Start a periodic poll timer that requests a sync cycle
                    // so the Finder view stays current with server-side changes
                    // (deletions, renames, new files). Similar to how iCloud and
                    // OneDrive keep their views updated.
                    self->_pollTimer.setInterval(30 * 1000); // 30 seconds
                    QObject::connect(&self->_pollTimer, &QTimer::timeout, self, [self]() {
                        if (!self || !self->_domainManager || self->_domainId.isEmpty()) {
                            return;
                        }
                        // Keep the access token up to date for the extension.
                        syncConfigToSharedContainer(self->params(), self->_domainId);
                        // Ask the sync scheduler to run a sync cycle.
                        Q_EMIT self->needSync();
                    });
                    self->_pollTimer.start();

                    Q_EMIT self->started();
                }
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(self, [self, errorMessage]() {
                if (self) {
                    qCWarning(lcVfsNSFP) << "Failed to register NSFileProvider domain:" << errorMessage;
                    Q_EMIT self->error(errorMessage);
                }
            }, Qt::QueuedConnection);
        }
    }, forceRecreate);
}

Result<void, QString> NsfpVfsPluginFactory::prepare(const QString &path, const QUuid &accountUuid) const
{
    Q_UNUSED(path)
    Q_UNUSED(accountUuid)
    // No special preparation needed yet
    return {};
}
