/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vfs_cfapi.h"

#include <QDir>

#include "cfapiwrapper.h"
#include "common/filesystembase.h"
#include "common/syncjournaldb.h"
#include "filesystem.h"
#include "syncfileitem.h"

#include <QCoreApplication>
#include <QThread>

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
        qCWarning(lcCfApi) << u"Register CfAPI shell extensions failed. Dll does not exist in " << QCoreApplication::applicationDirPath();
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

VfsCfApi::VfsCfApi(QObject *parent)
    : Vfs(parent)
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
                qCCritical(lcCfApi) << u"Initialization failed, couldn't connect sync root:" << connectResult.error();
                return;
            }

            _connectionKey = *std::move(connectResult);
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
    if (_connectionKey.Internal != 0) {
        const auto result = cfapi::disconnectSyncRoot(std::move(_connectionKey));
        if (!result) {
            qCCritical(lcCfApi) << u"Disconnect failed for" << params().filesystemPath << u":" << result.error();
        }
    }
}

void VfsCfApi::unregisterFolder()
{
    const auto result = cfapi::unregisterSyncRoot(params());
    if (!result) {
        qCCritical(lcCfApi) << u"Unregistration failed for" << params().filesystemPath << u":" << result.error();
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
                qCCritical(lcFileSystem) << u"GetFileInformationByHandle failed on" << path.path() << OCC::Utility::formatWinError(error);
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
    qCDebug(lcCfApi) << u"setPinState" << folderPath << state;

    const auto localPath = QDir::toNativeSeparators(params().filesystemPath + folderPath);
    return static_cast<bool>(cfapi::setPinState(localPath, state, cfapi::Recurse));
}

Optional<PinState> VfsCfApi::pinState(const QString &folderPath)
{
    const auto localPath = QDir::toNativeSeparators(params().filesystemPath + folderPath);
    const auto info = cfapi::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(localPath);
    if (!info) {
        qCDebug(lcCfApi) << u"Couldn't find pin state for regular non-placeholder file" << localPath;
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

void VfsCfApi::cancelHydration(const OCC::CfApiWrapper::CallBackContext &context)
{
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    // Find matching hydration job for request id
    auto hydrationJob = _hydrationJobs.find(context.transferKey);
    if (hydrationJob != _hydrationJobs.end()) {
        qCInfo(lcCfApi) << u"Cancel hydration" << hydrationJob.value()->context();
        // the job itself will take care of _hydrationJobs and its deletion
        hydrationJob.value()->abort();
    }
}

void VfsCfApi::fileStatusChanged(const QString &systemFileName, SyncFileStatus fileStatus)
{
    if (!QFileInfo::exists(systemFileName)) {
        return;
    }
    if (fileStatus.tag() == SyncFileStatus::StatusUpToDate) {
        if (auto info = CfApiWrapper::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(systemFileName)) {
            std::ignore = cfapi::updatePlaceholderMarkInSync(info.handle());
            if (info.pinState() == PinState::Excluded) {
                // clear possible exclude flag
                // a file usually does not change from excluded to not excluded, but ...
                cfapi::setPinState(systemFileName, PinState::Inherited, CfApiWrapper::Recurse);
            }
        }
    } else if (fileStatus.tag() == SyncFileStatus::StatusExcluded) {
        cfapi::setPinState(systemFileName, PinState::Excluded, CfApiWrapper::Recurse);
    }
}


} // namespace OCC
