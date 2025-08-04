/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vfs_cfapi.h"

#include <QDir>
#include <QFile>

#include "cfapiwrapper.h"
#include "common/filesystembase.h"
#include "common/syncjournaldb.h"
#include "config.h"
#include "filesystem.h"
#include "hydrationjob.h"
#include "syncfileitem.h"

// include order is important, this must be included before cfapi
#include <windows.h>
#include <winternl.h>

#include <cfapi.h>
#include <comdef.h>

Q_LOGGING_CATEGORY(lcCfApi, "sync.vfs.cfapi", QtInfoMsg)

using namespace Qt::Literals::StringLiterals;

namespace cfapi {
using namespace OCC::CfApiWrapper;

constexpr auto appIdRegKey = R"(Software\Classes\AppID\)";
constexpr auto clsIdRegKey = R"(Software\Classes\CLSID\)";
const auto rootKey = HKEY_CURRENT_USER;

#if 0
bool registerShellExtension()
{
    const QList<QPair<QString, QString>> listExtensions = {{CFAPI_SHELLEXT_THUMBNAIL_HANDLER_DISPLAY_NAME, CFAPI_SHELLEXT_THUMBNAIL_HANDLER_CLASS_ID_REG},
        {CFAPI_SHELLEXT_CUSTOM_STATE_HANDLER_DISPLAY_NAME, CFAPI_SHELLEXT_CUSTOM_STATE_HANDLER_CLASS_ID_REG}};
    // assume CFAPI_SHELL_EXTENSIONS_LIB_NAME is always in the same folder as the main executable
    // assume CFAPI_SHELL_EXTENSIONS_LIB_NAME is always in the same folder as the main executable
    const auto shellExtensionDllPath = QDir::toNativeSeparators(
        QString(QCoreApplication::applicationDirPath() + QStringLiteral("/") + CFAPI_SHELL_EXTENSIONS_LIB_NAME + QStringLiteral(".dll")));
    if (!OCC::FileSystem::fileExists(shellExtensionDllPath)) {
        Q_ASSERT(false);
        qCWarning(lcCfApi) << "Register CfAPI shell extensions failed. Dll does not exist in " << QCoreApplication::applicationDirPath();
        return false;
    }

    const QString appIdPath = QString() % appIdRegKey % CFAPI_SHELLEXT_APPID_REG;
    if (!OCC::Utility::registrySetKeyValue(rootKey, appIdPath, {}, REG_SZ, CFAPI_SHELLEXT_APPID_DISPLAY_NAME)) {
        return false;
    }
    if (!OCC::Utility::registrySetKeyValue(rootKey, appIdPath, QStringLiteral("DllSurrogate"), REG_SZ, {})) {
        return false;
    }

    for (const auto extension : listExtensions) {
        const QString clsidPath = QString() % clsIdRegKey % extension.second;
        const QString clsidServerPath = clsidPath % R"(\InprocServer32)";

        if (!OCC::Utility::registrySetKeyValue(rootKey, clsidPath, QStringLiteral("AppID"), REG_SZ, CFAPI_SHELLEXT_APPID_REG)) {
            return false;
        }
        if (!OCC::Utility::registrySetKeyValue(rootKey, clsidPath, {}, REG_SZ, extension.first)) {
            return false;
        }
        if (!OCC::Utility::registrySetKeyValue(rootKey, clsidServerPath, {}, REG_SZ, shellExtensionDllPath)) {
            return false;
        }
        if (!OCC::Utility::registrySetKeyValue(rootKey, clsidServerPath, QStringLiteral("ThreadingModel"), REG_SZ, QStringLiteral("Apartment"))) {
            return false;
        }
    }

    return true;
}

void unregisterShellExtensions()
{
    const QString appIdPath = QString() % appIdRegKey % CFAPI_SHELLEXT_APPID_REG;
    if (OCC::Utility::registryKeyExists(rootKey, appIdPath)) {
        OCC::Utility::registryDeleteKeyTree(rootKey, appIdPath);
    }

    const QStringList listExtensions = {CFAPI_SHELLEXT_CUSTOM_STATE_HANDLER_CLASS_ID_REG, CFAPI_SHELLEXT_THUMBNAIL_HANDLER_CLASS_ID_REG};

    for (const auto extension : listExtensions) {
        const QString clsidPath = QString() % clsIdRegKey % extension;
        if (OCC::Utility::registryKeyExists(rootKey, clsidPath)) {
            OCC::Utility::registryDeleteKeyTree(rootKey, clsidPath);
        }
    }
}

#endif
}

namespace OCC {

class VfsCfApiPrivate
{
public:
    QList<HydrationJob *> hydrationJobs;
    CF_CONNECTION_KEY connectionKey = {};
};

VfsCfApi::VfsCfApi(QObject *parent)
    : Vfs(parent)
    , d(new VfsCfApiPrivate)
{
}

VfsCfApi::~VfsCfApi() = default;

Vfs::Mode VfsCfApi::mode() const
{
    return WindowsCfApi;
}

void VfsCfApi::startImpl(const VfsSetupParams &params)
{
    // cfapi::registerShellExtension();

    cfapi::registerSyncRoot(params, [this](const QString &errorMessage) {
        if (errorMessage.isEmpty()) {
            auto connectResult = cfapi::connectSyncRoot(this->params().filesystemPath, this);
            if (!connectResult) {
                qCCritical(lcCfApi) << "Initialization failed, couldn't connect sync root:" << connectResult.error();
                return;
            }

            d->connectionKey = *std::move(connectResult);
            Q_EMIT started();
        } else {
            qCCritical(lcCfApi) << errorMessage;
            Q_ASSERT(false);
            Q_EMIT error(errorMessage);
        }
    });
}

void VfsCfApi::stop()
{
    if (d->connectionKey.Internal != 0) {
        const auto result = cfapi::disconnectSyncRoot(std::move(d->connectionKey));
        if (!result) {
            qCCritical(lcCfApi) << "Disconnect failed for" << params().filesystemPath << ":" << result.error();
        }
    }
}

void VfsCfApi::unregisterFolder()
{
    const auto result = cfapi::unregisterSyncRoot(params());
    if (!result) {
        qCCritical(lcCfApi) << "Unregistration failed for" << params().filesystemPath << ":" << result.error();
    }

#if 0
    if (!cfapi::isAnySyncRoot(params().providerName, params().account->displayName())) {
        cfapi::unregisterShellExtensions();
    }
#endif
}

bool VfsCfApi::socketApiPinStateActionsShown() const
{
    return true;
}

Result<Vfs::ConvertToPlaceholderResult, QString> VfsCfApi::updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile)
{
    const auto localPath = QDir::toNativeSeparators(filePath);
    const auto replacesPath = QDir::toNativeSeparators(replacesFile);

    if (syncItem._type == ItemTypeVirtualFileDehydration) {
        return cfapi::dehydratePlaceholder(localPath, syncItem._modtime, syncItem._size, syncItem._fileId);
    } else {
        if (cfapi::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(localPath)) {
            return cfapi::updatePlaceholderInfo(localPath, syncItem._modtime, syncItem._size, syncItem._fileId, replacesPath);
        } else {
            return cfapi::convertToPlaceholder(localPath, syncItem._modtime, syncItem._size, syncItem._fileId, replacesPath);
        }
    }
}

Result<void, QString> VfsCfApi::createPlaceholder(const SyncFileItem &item)
{
    Q_ASSERT(params().filesystemPath.endsWith('/'_L1));
    const auto localPath = QDir::toNativeSeparators(params().filesystemPath + item.localName());
    const auto result = cfapi::createPlaceholderInfo(localPath, item._modtime, item._size, item._fileId);
    return result;
}

bool VfsCfApi::needsMetadataUpdate(const SyncFileItem &item)
{
    const QString path = params().filesystemPath + item.localName();
    if (!QFileInfo::exists(path)) {
        return false;
    }
    return !cfapi::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(path).isValid();
}

bool VfsCfApi::isDehydratedPlaceholder(const QString &filePath)
{
    const auto path = QDir::toNativeSeparators(filePath);
    return cfapi::isSparseFile(path);
}

LocalInfo VfsCfApi::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    // only get placeholder info if it's a file
    if (type == ItemTypeFile) {
        if (auto placeholderInfo = cfapi::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(FileSystem::fromFilesystemPath(path))) {
            Q_ASSERT(placeholderInfo.handle());
            FILE_ATTRIBUTE_TAG_INFO attributeInfo = {};
            if (!GetFileInformationByHandleEx(placeholderInfo.handle(), FileAttributeTagInfo, &attributeInfo, sizeof(attributeInfo))) {
                const auto error = GetLastError();
                qCCritical(lcFileSystem) << "GetFileInformationByHandle failed on" << path.path() << OCC::Utility::formatWinError(error);
                return {};
            }
            const CF_PLACEHOLDER_STATE placeholderState = CfGetPlaceholderStateFromAttributeTag(attributeInfo.FileAttributes, attributeInfo.ReparseTag);
            if (placeholderState & CF_PLACEHOLDER_STATE_PLACEHOLDER) {
                if (placeholderState & CF_PLACEHOLDER_STATE_PARTIAL) {
                    if (placeholderInfo.pinState() == PinState::AlwaysLocal) {
                        Q_ASSERT(attributeInfo.FileAttributes & FILE_ATTRIBUTE_PINNED);
                        type = ItemTypeVirtualFileDownload;
                    } else {
                        type = ItemTypeVirtualFile;
                    }
                } else {
                    if (placeholderInfo.pinState() == PinState::OnlineOnly) {
                        Q_ASSERT(attributeInfo.FileAttributes & FILE_ATTRIBUTE_UNPINNED);
                        type = ItemTypeVirtualFileDehydration;
                    } else {
                        // nothing to do
                        Q_ASSERT(type == ItemTypeFile);
                    }
                }
            }
        }
    }
    return LocalInfo(path, type);
}

bool VfsCfApi::setPinState(const QString &folderPath, PinState state)
{
    qCDebug(lcCfApi) << "setPinState" << folderPath << state;

    const auto localPath = QDir::toNativeSeparators(params().filesystemPath + folderPath);
    return static_cast<bool>(cfapi::setPinState(localPath, state, cfapi::Recurse));
}

Optional<PinState> VfsCfApi::pinState(const QString &folderPath)
{
    const auto localPath = QDir::toNativeSeparators(params().filesystemPath + folderPath);
    const auto info = cfapi::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(localPath);
    if (!info) {
        qCDebug(lcCfApi) << "Couldn't find pin state for regular non-placeholder file" << localPath;
        return {};
    }

    return info.pinState();
}

Vfs::AvailabilityResult VfsCfApi::availability(const QString &folderPath)
{
    const auto basePinState = pinState(folderPath);
    if (basePinState) {
        switch (*basePinState) {
        case OCC::PinState::AlwaysLocal:
            return VfsItemAvailability::AlwaysLocal;
            break;
        case OCC::PinState::Inherited:
            break;
        case OCC::PinState::OnlineOnly:
            return VfsItemAvailability::OnlineOnly;
            break;
        case OCC::PinState::Unspecified:
            break;
        case OCC::PinState::Excluded:
            break;
        };
        return VfsItemAvailability::Mixed;
    } else {
        return AvailabilityError::NoSuchItem;
    }
}

HydrationJob *VfsCfApi::findHydrationJob(const QString &requestId) const
{
    // Find matching hydration job for request id
    const auto hydrationJobsIter =
        std::find_if(d->hydrationJobs.cbegin(), d->hydrationJobs.cend(), [&](const HydrationJob *job) { return job->requestId() == requestId; });

    if (hydrationJobsIter != d->hydrationJobs.cend()) {
        return *hydrationJobsIter;
    }

    return nullptr;
}

void VfsCfApi::cancelHydration(const QString &requestId, const QString & /*path*/)
{
    // Find matching hydration job for request id
    const auto hydrationJob = findHydrationJob(requestId);
    // If found, cancel it
    if (hydrationJob) {
        qCInfo(lcCfApi) << "Cancel hydration";
        hydrationJob->cancel();
    }
}

void VfsCfApi::requestHydration(const QString &requestId, const QString &targetPath, const QByteArray &fileId, qint64 requestedFileSize)
{
    qCInfo(lcCfApi) << "Received request to hydrate" << targetPath << requestId;
    const auto root = QDir::toNativeSeparators(params().filesystemPath);
    Q_ASSERT(targetPath.startsWith(root));


    // Set in the database that we should download the file
    SyncJournalFileRecord record;
    params().journal->getFileRecordsByFileId(fileId, [&record](const auto &r) {
        Q_ASSERT(!record.isValid());
        record = r;
    });
    if (!record.isValid()) {
        qCInfo(lcCfApi) << "Couldn't hydrate, did not find file in db";
        Q_ASSERT(false); // how did we end up here if it's not  a cloud file
        Q_EMIT hydrationRequestFailed(requestId);
        Q_EMIT needSync();
        return;
    }

    bool isNotVirtualFileFailure = false;
    if (!record.isVirtualFile()) {
        if (isDehydratedPlaceholder(targetPath)) {
            qCWarning(lcCfApi) << "Hydration requested for a placeholder file that is incorrectly not marked as a virtual file in the local database. Attempting to correct this inconsistency...";
            auto item = SyncFileItem::fromSyncJournalFileRecord(record);
            item->_type = ItemTypeVirtualFileDownload;
            isNotVirtualFileFailure = !params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
        } else {
            isNotVirtualFileFailure = true;
        }
    }
    if (requestedFileSize != record.size()) {
        // we are out of sync
        qCWarning(lcCfApi) << "The db size and the placeholder meta data are out of sync, request resync";
        Q_ASSERT(false); // this should not happen
        Q_EMIT hydrationRequestFailed(requestId);
        Q_EMIT needSync();
        return;
    }

    if (isNotVirtualFileFailure) {
        qCWarning(lcCfApi) << "Couldn't hydrate, the file is not virtual";
        Q_ASSERT(false); // this should not happen
        Q_EMIT hydrationRequestFailed(requestId);
        Q_EMIT needSync();
        return;
    }

    // All good, let's hydrate now
    scheduleHydrationJob(requestId, std::move(record), targetPath);
}

void VfsCfApi::fileStatusChanged(const QString &systemFileName, SyncFileStatus fileStatus)
{
    if (!QFileInfo::exists(systemFileName)) {
        return;
    }
    if (fileStatus.tag() == SyncFileStatus::StatusUpToDate) {
        std::ignore = cfapi::updatePlaceholderMarkInSync(systemFileName);
    } else if (fileStatus.tag() == SyncFileStatus::StatusExcluded) {
        cfapi::setPinState(systemFileName, PinState::Excluded, CfApiWrapper::Recurse);
    }
}

void VfsCfApi::scheduleHydrationJob(const QString &requestId, SyncJournalFileRecord &&record, const QString &targetPath)
{
    // after a local move, the remotePath and the targetPath might not match
    const auto jobAlreadyScheduled = std::any_of(std::cbegin(d->hydrationJobs), std::cend(d->hydrationJobs),
        [=](HydrationJob *job) { return job->requestId() == requestId || job->localFilePathAbs() == targetPath; });

    if (jobAlreadyScheduled) {
        qCWarning(lcCfApi) << "The OS submitted again a hydration request which is already on-going" << requestId << record.path();
        Q_EMIT hydrationRequestFailed(requestId);
        return;
    }

    auto job = new HydrationJob(this);
    job->setAccount(params().account);
    job->setRemoteSyncRootPath(params().baseUrl());
    job->setLocalRoot(params().filesystemPath);
    job->setJournal(params().journal);
    job->setRequestId(requestId);
    job->setLocalFilePathAbs(targetPath);
    job->setRemoteFilePathRel(record.path());
    job->setRecord(std::move(record));
    connect(job, &HydrationJob::finished, this, &VfsCfApi::onHydrationJobFinished);
    d->hydrationJobs << job;
    job->start();
    Q_EMIT hydrationRequestReady(requestId);
}

void VfsCfApi::onHydrationJobFinished(HydrationJob *job)
{
    Q_ASSERT(d->hydrationJobs.contains(job));
    qCInfo(lcCfApi) << "Hydration job finished" << job->requestId() << job->localFilePathAbs() << job->status();
    Q_EMIT hydrationRequestFinished(job->requestId());
    if (!job->errorString().isEmpty()) {
        qCWarning(lcCfApi) << job->errorString();
    }
}

HydrationJob::Status VfsCfApi::finalizeHydrationJob(const QString &requestId)
{
    // Find matching hydration job for request id
    if (const auto hydrationJob = findHydrationJob(requestId)) {
        qCDebug(lcCfApi) << "Finalize hydration job" << requestId << hydrationJob->localFilePathAbs();
        hydrationJob->finalize(this);
        d->hydrationJobs.removeAll(hydrationJob);
        hydrationJob->deleteLater();
        return hydrationJob->status();
    }
    qCCritical(lcCfApi) << "Failed to finalize hydration job" << requestId << ". Job not found.";
    return HydrationJob::Status::Error;
}


} // namespace OCC
