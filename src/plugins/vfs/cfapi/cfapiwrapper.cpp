/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "cfapiwrapper.h"

#include "common/filesystembase.h"
#include "common/utility_win.h"
#include "filesystem.h"
#include "hydrationjob.h"
#include "theme.h"
#include "vfs_cfapi.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUuid>

#include <comdef.h>
#include <ntstatus.h>
#include <sddl.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/windows.Storage.Streams.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.storage.provider.h>

Q_LOGGING_CATEGORY(lcCfApiWrapper, "sync.vfs.cfapi.wrapper", QtDebugMsg)

using namespace Qt::Literals::StringLiterals;
using namespace std::chrono_literals;

namespace winrt {
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage::Provider;
using namespace Windows::Foundation::Collections;
using namespace Windows::Security::Cryptography;
}


#define FIELD_SIZE(type, field) (sizeof(((type *)0)->field))
#define CF_SIZE_OF_OP_PARAM(field) (FIELD_OFFSET(CF_OPERATION_PARAMETERS, field) + FIELD_SIZE(CF_OPERATION_PARAMETERS, field))

namespace {

constexpr auto forbiddenLeadingCharacterInPath = '#'_L1;

QString createErrorMessageForPlaceholderUpdateAndCreate(const QString &path, const QString &originalErrorMessage)
{
    const auto pathFromNativeSeparators = QDir::fromNativeSeparators(path);
    if (!pathFromNativeSeparators.contains(QStringLiteral("/%1").arg(forbiddenLeadingCharacterInPath))) {
        return originalErrorMessage;
    }
    const auto fileComponents = pathFromNativeSeparators.split('/'_L1);
    for (const auto &fileComponent : fileComponents) {
        if (fileComponent.startsWith(forbiddenLeadingCharacterInPath)) {
            qCInfo(lcCfApiWrapper) << "Failed to create/update a placeholder for path \"" << pathFromNativeSeparators << "\" that has a leading '#'.";
            return {u"%1: %2"_s.arg(originalErrorMessage, QObject::tr("Paths beginning with '#' character are not supported in VFS mode."))};
        }
    }
    return originalErrorMessage;
}

// retreive the pllaceholder info, by default we don't request the full FileIdentity
OCC::Result<std::vector<char>, int64_t> getPlaceholderInfo(
    const OCC::Utility::Handle &handle, CF_PLACEHOLDER_INFO_CLASS infoClass = CF_PLACEHOLDER_INFO_BASIC, bool withFileIdentity = false)
{
    std::vector<char> buffer(
        withFileIdentity ? 512 : (infoClass == CF_PLACEHOLDER_INFO_BASIC ? sizeof(CF_PLACEHOLDER_BASIC_INFO) : sizeof(CF_PLACEHOLDER_STANDARD_INFO)));
    DWORD actualSize = {};
    const int64_t result = CfGetPlaceholderInfo(handle.handle(), infoClass, buffer.data(), static_cast<DWORD>(buffer.size()), &actualSize);
    if (result == S_OK || (!withFileIdentity && result == HRESULT_FROM_WIN32(ERROR_MORE_DATA))) {
        if (withFileIdentity) {
            buffer.resize(actualSize);
        }
        return std::move(buffer);
    } else if (result == HRESULT_FROM_WIN32(ERROR_NOT_A_CLOUD_FILE)) {
        // native file, not yet converted
        return std::vector<char>{};
    } else {
        qCWarning(lcCfApiWrapper) << "Failed to retrieve placeholder info:" << OCC::Utility::formatWinError(result);
        Q_ASSERT(false);
    }
    return {result};
}

void cfApiSendTransferInfo(const CF_CONNECTION_KEY &connectionKey, const CF_TRANSFER_KEY &transferKey, NTSTATUS status, void *buffer, qint64 offset,
    qint64 currentBlockLength, qint64 totalLength)
{
    CF_OPERATION_INFO opInfo = {0};
    CF_OPERATION_PARAMETERS opParams = {0};

    opInfo.StructSize = sizeof(opInfo);
    opInfo.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
    opInfo.ConnectionKey = connectionKey;
    opInfo.TransferKey = transferKey;
    opParams.ParamSize = CF_SIZE_OF_OP_PARAM(TransferData);
    opParams.TransferData.CompletionStatus = status;
    opParams.TransferData.Buffer = buffer;
    opParams.TransferData.Offset.QuadPart = offset;
    opParams.TransferData.Length.QuadPart = currentBlockLength;

    const qint64 cfExecuteresult = CfExecute(&opInfo, &opParams);
    if (cfExecuteresult != S_OK) {
        qCCritical(lcCfApiWrapper) << "Couldn't send transfer info" << QString::number(transferKey.QuadPart, 16) << ":" << cfExecuteresult
                                   << OCC::Utility::formatWinError(cfExecuteresult);
    }

    const auto isDownloadFinished = ((offset + currentBlockLength) == totalLength);
    if (isDownloadFinished) {
        return;
    }

    // refresh Windows Copy Dialog progress
    LARGE_INTEGER progressTotal;
    progressTotal.QuadPart = totalLength;

    LARGE_INTEGER progressCompleted;
    progressCompleted.QuadPart = offset;

    const qint64 cfReportProgressresult = CfReportProviderProgress(connectionKey, transferKey, progressTotal, progressCompleted);

    if (cfReportProgressresult != S_OK) {
        qCCritical(lcCfApiWrapper) << "Couldn't report provider progress" << QString::number(transferKey.QuadPart, 16) << ":" << cfReportProgressresult
                                   << OCC::Utility::formatWinError(cfReportProgressresult);
    }
}

void CALLBACK cfApiFetchDataCallback(const CF_CALLBACK_INFO *callbackInfo, const CF_CALLBACK_PARAMETERS *callbackParameters)
{
    const qint64 requestedFileSize = callbackInfo->FileSize.QuadPart;
    qDebug(lcCfApiWrapper) << "Fetch data callback called. File size:" << requestedFileSize;
    qDebug(lcCfApiWrapper) << "Desktop client process id:" << QCoreApplication::applicationPid();
    qDebug(lcCfApiWrapper) << "Fetch data requested by process id:" << callbackInfo->ProcessInfo->ProcessId;
    qDebug(lcCfApiWrapper) << "Fetch data requested by application id:" << QString::fromWCharArray(callbackInfo->ProcessInfo->ApplicationId);
    qDebug(lcCfApiWrapper) << "Fetch data requested by application:" << QString::fromWCharArray(callbackInfo->ProcessInfo->ImagePath);

    const auto sendTransferError = [=] {
        cfApiSendTransferInfo(callbackInfo->ConnectionKey, callbackInfo->TransferKey, STATUS_UNSUCCESSFUL, nullptr,
            callbackParameters->FetchData.RequiredFileOffset.QuadPart, callbackParameters->FetchData.RequiredLength.QuadPart, callbackInfo->FileSize.QuadPart);
    };

    const auto sendTransferInfo = [=](QByteArray &data, qint64 offset) {
        cfApiSendTransferInfo(
            callbackInfo->ConnectionKey, callbackInfo->TransferKey, STATUS_SUCCESS, data.data(), offset, data.length(), callbackInfo->FileSize.QuadPart);
    };

    auto vfs = reinterpret_cast<OCC::VfsCfApi *>(callbackInfo->CallbackContext);
    Q_ASSERT(vfs->metaObject()->className() == QByteArrayLiteral("OCC::VfsCfApi"));
    const auto path = QString(QString::fromWCharArray(callbackInfo->VolumeDosName) + QString::fromWCharArray(callbackInfo->NormalizedPath));
    const auto requestId = QString::number(callbackInfo->TransferKey.QuadPart, 16);
    const auto fileId = QByteArray(reinterpret_cast<const char *>(callbackInfo->FileIdentity), callbackInfo->FileIdentityLength);

    if (QCoreApplication::applicationPid() == callbackInfo->ProcessInfo->ProcessId) {
        qCCritical(lcCfApiWrapper) << "implicit hydration triggered by the client itself. Will lead to a deadlock. Cancel" << path << requestId;
        Q_ASSERT(false);
        sendTransferError();
        return;
    }

    qCDebug(lcCfApiWrapper) << "Request hydration for" << path << requestId;

    const auto invokeResult = QMetaObject::invokeMethod(vfs, [=] { vfs->requestHydration(requestId, path, fileId, requestedFileSize); }, Qt::QueuedConnection);
    if (!invokeResult) {
        qCCritical(lcCfApiWrapper) << "Failed to trigger hydration for" << path << requestId;
        sendTransferError();
        return;
    }

    qCDebug(lcCfApiWrapper) << "Successfully triggered hydration for" << path << requestId;

    // Block and wait for vfs to signal back the hydration is ready
    bool hydrationRequestResult = false;
    QEventLoop loop;
    QObject::connect(vfs, &OCC::VfsCfApi::hydrationRequestReady, &loop, [&](const QString &id) {
        if (requestId == id) {
            hydrationRequestResult = true;
            qCDebug(lcCfApiWrapper) << "Hydration request ready for" << path << requestId;
            loop.quit();
        }
    });
    QObject::connect(vfs, &OCC::VfsCfApi::hydrationRequestFailed, &loop, [&](const QString &id) {
        if (requestId == id) {
            hydrationRequestResult = false;
            qCWarning(lcCfApiWrapper) << "Hydration request failed for" << path << requestId;
            loop.quit();
        }
    });

    qCDebug(lcCfApiWrapper) << "Starting event loop 1";
    loop.exec();
    QObject::disconnect(vfs, nullptr, &loop, nullptr); // Ensure we properly cancel hydration on server errors

    qCInfo(lcCfApiWrapper) << "VFS replied for hydration of" << path << requestId << "status was:" << hydrationRequestResult;
    if (!hydrationRequestResult) {
        qCCritical(lcCfApiWrapper) << "Failed to trigger hydration for" << path << requestId;
        sendTransferError();
        return;
    }

    QLocalSocket socket;
    socket.connectToServer(requestId);
    const auto connectResult = socket.waitForConnected();
    if (!connectResult) {
        qCWarning(lcCfApiWrapper) << "Couldn't connect the socket" << requestId << socket.error() << socket.errorString();
        sendTransferError();
        return;
    }

    QLocalSocket signalSocket;
    const QString signalSocketName = requestId + u":cancellation"_s;
    signalSocket.connectToServer(signalSocketName);
    const auto cancellationSocketConnectResult = signalSocket.waitForConnected();
    if (!cancellationSocketConnectResult) {
        qCWarning(lcCfApiWrapper) << "Couldn't connect the socket" << signalSocketName << signalSocket.error() << signalSocket.errorString();
        sendTransferError();
        return;
    }

    auto hydrationRequestCancelled = false;
    QObject::connect(&signalSocket, &QLocalSocket::readyRead, &loop, [&] {
        hydrationRequestCancelled = true;
        qCCritical(lcCfApiWrapper) << "Hydration canceled for " << path << requestId;
    });

    // CFAPI expects sent blocks to be of a multiple of a block size.
    // Only the last sent block is allowed to be of a different size than
    // a multiple of a block size

    // TODO: this looks like it has optimisation potential
    constexpr auto cfapiBlockSize = 4096;
    qint64 dataOffset = 0;
    QByteArray protrudingData;

    const auto alignAndSendData = [&](const QByteArray &receivedData) {
        QByteArray data = protrudingData + receivedData;
        protrudingData.clear();
        if (data.size() < cfapiBlockSize) {
            protrudingData = data;
            return;
        }
        const auto protudingSize = data.size() % cfapiBlockSize;
        protrudingData = data.right(protudingSize);
        data.chop(protudingSize);
        sendTransferInfo(data, dataOffset);
        dataOffset += data.size();
    };

    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        if (hydrationRequestCancelled) {
            qCDebug(lcCfApiWrapper) << "Don't transfer data because request" << requestId << "was cancelled";
            return;
        }

        const auto receivedData = socket.readAll();
        if (receivedData.isEmpty()) {
            qCWarning(lcCfApiWrapper) << "Unexpected empty data received" << requestId;
            sendTransferError();
            protrudingData.clear();
            loop.quit();
            return;
        }
        alignAndSendData(receivedData);
    });

    QObject::connect(vfs, &OCC::VfsCfApi::hydrationRequestFinished, &loop, [&](const QString &id) {
        qDebug(lcCfApiWrapper) << "Hydration finished for request" << id;
        if (requestId == id) {
            loop.quit();
        }
    });

    qCDebug(lcCfApiWrapper) << "Starting event loop 2";
    loop.exec();

    if (!hydrationRequestCancelled && !protrudingData.isEmpty()) {
        qDebug(lcCfApiWrapper) << "Send remaining protruding data. Size:" << protrudingData.size();
        sendTransferInfo(protrudingData, dataOffset);
    }

    OCC::HydrationJob::Status hydrationJobResult = OCC::HydrationJob::Status::Error;
    const auto invokeFinalizeResult = QMetaObject::invokeMethod(
        vfs, [&hydrationJobResult, vfs, requestId] { hydrationJobResult = vfs->finalizeHydrationJob(requestId); }, Qt::BlockingQueuedConnection);
    if (!invokeFinalizeResult) {
        qCritical(lcCfApiWrapper) << "Failed to finalize hydration job for" << path << requestId;
    }

    if (hydrationJobResult != OCC::HydrationJob::Status::Success) {
        sendTransferError();
    }
}

enum class CfApiUpdateMetadataType {
    OnlyBasicMetadata,
    AllMetadata,
};

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> updatePlaceholderState(
    const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath, CfApiUpdateMetadataType updateType)
{
    if (updateType == CfApiUpdateMetadataType::AllMetadata && modtime <= 0) {
        return {u"Could not update metadata due to invalid modification time for %1: %2"_s.arg(path, modtime)};
    }

    OCC::CfApiWrapper::PlaceHolderInfo<CF_PLACEHOLDER_BASIC_INFO> info;
    if (!replacesPath.isEmpty()) {
        info = OCC::CfApiWrapper::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(replacesPath);
    }
    if (!info) {
        info = OCC::CfApiWrapper::findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(path);
    }
    if (!info) {
        Q_ASSERT(false);
        return {u"Can't update non existing placeholder info"_s};
    }

    const auto previousPinState = info.pinState();

    CF_FS_METADATA metadata = {};
    if (updateType == CfApiUpdateMetadataType::AllMetadata) {
        metadata.FileSize.QuadPart = size;
        OCC::Utility::UnixTimeToLargeIntegerFiletime(modtime, &metadata.BasicInfo.CreationTime);
        metadata.BasicInfo.LastWriteTime = metadata.BasicInfo.CreationTime;
        metadata.BasicInfo.LastAccessTime = metadata.BasicInfo.CreationTime;
        metadata.BasicInfo.ChangeTime = metadata.BasicInfo.CreationTime;
    }


    qCInfo(lcCfApiWrapper) << "updatePlaceholderState" << path << modtime;
    const qint64 result = CfUpdatePlaceholder(OCC::CfApiWrapper::handleForPath(path).handle(), &metadata, fileId.data(), static_cast<DWORD>(fileId.size()),
        nullptr, 0, CF_UPDATE_FLAG_MARK_IN_SYNC, nullptr, nullptr);

    if (result != S_OK) {
        const QString errorMessage = createErrorMessageForPlaceholderUpdateAndCreate(path, u"Couldn't update placeholder info"_s);
        qCWarning(lcCfApiWrapper) << errorMessage << path << ":" << OCC::Utility::formatWinError(result) << replacesPath;
        return errorMessage;
    }

    // Pin state tends to be lost on updates, so restore it every time
    if (!setPinState(path, previousPinState, OCC::CfApiWrapper::NoRecurse)) {
        return {u"Couldn't restore pin state"_s};
    }

    return OCC::Vfs::ConvertToPlaceholderResult::Ok;
}
}

void CALLBACK cfApiCancelFetchData(const CF_CALLBACK_INFO *callbackInfo, const CF_CALLBACK_PARAMETERS * /*callbackParameters*/)
{
    const auto path = QString(QString::fromWCharArray(callbackInfo->VolumeDosName) + QString::fromWCharArray(callbackInfo->NormalizedPath));

    qInfo(lcCfApiWrapper) << "Cancel fetch data of" << path;

    auto vfs = reinterpret_cast<OCC::VfsCfApi *>(callbackInfo->CallbackContext);
    Q_ASSERT(vfs->metaObject()->className() == QByteArrayLiteral("OCC::VfsCfApi"));
    const auto requestId = QString::number(callbackInfo->TransferKey.QuadPart, 16);

    const auto invokeResult = QMetaObject::invokeMethod(vfs, [=] { vfs->cancelHydration(requestId, path); }, Qt::QueuedConnection);
    if (!invokeResult) {
        qCritical(lcCfApiWrapper) << "Failed to cancel hydration for" << path << requestId;
    }
}

void CALLBACK cfApiNotifyFileOpenCompletion(const CF_CALLBACK_INFO *callbackInfo, const CF_CALLBACK_PARAMETERS * /*callbackParameters*/)
{
    const auto path = QString(QString::fromWCharArray(callbackInfo->VolumeDosName) + QString::fromWCharArray(callbackInfo->NormalizedPath));

    auto vfs = reinterpret_cast<OCC::VfsCfApi *>(callbackInfo->CallbackContext);
    Q_ASSERT(vfs->metaObject()->className() == QByteArrayLiteral("OCC::VfsCfApi"));
    const auto requestId = QString::number(callbackInfo->TransferKey.QuadPart, 16);

    qCDebug(lcCfApiWrapper) << "Open file completion:" << path << requestId;
}

void CALLBACK cfApiValidateData(const CF_CALLBACK_INFO *callbackInfo, const CF_CALLBACK_PARAMETERS * /*callbackParameters*/)
{
    const auto path = QString(QString::fromWCharArray(callbackInfo->VolumeDosName) + QString::fromWCharArray(callbackInfo->NormalizedPath));

    auto vfs = reinterpret_cast<OCC::VfsCfApi *>(callbackInfo->CallbackContext);
    Q_ASSERT(vfs->metaObject()->className() == QByteArrayLiteral("OCC::VfsCfApi"));
    const auto requestId = QString::number(callbackInfo->TransferKey.QuadPart, 16);

    qCDebug(lcCfApiWrapper) << "Validate data:" << path << requestId;
}

void CALLBACK cfApiCancelFetchPlaceHolders(const CF_CALLBACK_INFO *callbackInfo, const CF_CALLBACK_PARAMETERS * /*callbackParameters*/)
{
    const auto path = QString(QString::fromWCharArray(callbackInfo->VolumeDosName) + QString::fromWCharArray(callbackInfo->NormalizedPath));

    auto vfs = reinterpret_cast<OCC::VfsCfApi *>(callbackInfo->CallbackContext);
    Q_ASSERT(vfs->metaObject()->className() == QByteArrayLiteral("OCC::VfsCfApi"));
    const auto requestId = QString::number(callbackInfo->TransferKey.QuadPart, 16);

    qCDebug(lcCfApiWrapper) << "Cancel fetch placeholder:" << path << requestId;
}

void CALLBACK cfApiNotifyFileCloseCompletion(const CF_CALLBACK_INFO *callbackInfo, const CF_CALLBACK_PARAMETERS * /*callbackParameters*/)
{
    const auto path = QString(QString::fromWCharArray(callbackInfo->VolumeDosName) + QString::fromWCharArray(callbackInfo->NormalizedPath));

    auto vfs = reinterpret_cast<OCC::VfsCfApi *>(callbackInfo->CallbackContext);
    Q_ASSERT(vfs->metaObject()->className() == QByteArrayLiteral("OCC::VfsCfApi"));
    const auto requestId = QString::number(callbackInfo->TransferKey.QuadPart, 16);

    qCDebug(lcCfApiWrapper) << "Close file completion:" << path << requestId;
}

CF_CALLBACK_REGISTRATION cfApiCallbacks[] = {{CF_CALLBACK_TYPE_FETCH_DATA, cfApiFetchDataCallback}, {CF_CALLBACK_TYPE_CANCEL_FETCH_DATA, cfApiCancelFetchData},
    {CF_CALLBACK_TYPE_NOTIFY_FILE_OPEN_COMPLETION, cfApiNotifyFileOpenCompletion},
    {CF_CALLBACK_TYPE_NOTIFY_FILE_CLOSE_COMPLETION, cfApiNotifyFileCloseCompletion}, {CF_CALLBACK_TYPE_VALIDATE_DATA, cfApiValidateData},
    {CF_CALLBACK_TYPE_CANCEL_FETCH_PLACEHOLDERS, cfApiCancelFetchPlaceHolders}, CF_CALLBACK_REGISTRATION_END};


CF_PIN_STATE pinStateToCfPinState(OCC::PinState state)
{
    switch (state) {
    case OCC::PinState::Inherited:
        return CF_PIN_STATE_INHERIT;
    case OCC::PinState::AlwaysLocal:
        return CF_PIN_STATE_PINNED;
    case OCC::PinState::OnlineOnly:
        return CF_PIN_STATE_UNPINNED;
    case OCC::PinState::Unspecified:
        return CF_PIN_STATE_UNSPECIFIED;
    }
    Q_UNREACHABLE();
}

CF_SET_PIN_FLAGS pinRecurseModeToCfSetPinFlags(OCC::CfApiWrapper::SetPinRecurseMode mode)
{
    switch (mode) {
    case OCC::CfApiWrapper::NoRecurse:
        return CF_SET_PIN_FLAG_NONE;
    case OCC::CfApiWrapper::Recurse:
        return CF_SET_PIN_FLAG_RECURSE;
    case OCC::CfApiWrapper::ChildrenOnly:
        return CF_SET_PIN_FLAG_RECURSE_ONLY;
    }
    Q_UNREACHABLE();
}

QString convertSidToStringSid(void *sid)
{
    wchar_t *stringSid = nullptr;
    if (!ConvertSidToStringSid(sid, &stringSid)) {
        return {};
    }

    const auto result = QString::fromWCharArray(stringSid);
    LocalFree(stringSid);
    return result;
}

std::unique_ptr<TOKEN_USER> getCurrentTokenInformation()
{
    const auto tokenHandle = GetCurrentThreadEffectiveToken();

    auto tokenInfoSize = DWORD{0};

    const auto tokenSizeCallSucceeded = ::GetTokenInformation(tokenHandle, TokenUser, nullptr, 0, &tokenInfoSize);
    const auto lastError = GetLastError();
    Q_ASSERT(!tokenSizeCallSucceeded && lastError == ERROR_INSUFFICIENT_BUFFER);
    if (tokenSizeCallSucceeded || lastError != ERROR_INSUFFICIENT_BUFFER) {
        qCCritical(lcCfApiWrapper) << "GetTokenInformation for token size has failed with error" << lastError;
        return {};
    }

    std::unique_ptr<TOKEN_USER> tokenInfo;

    tokenInfo.reset(reinterpret_cast<TOKEN_USER *>(new char[tokenInfoSize]));
    if (!::GetTokenInformation(tokenHandle, TokenUser, tokenInfo.get(), tokenInfoSize, &tokenInfoSize)) {
        qCCritical(lcCfApiWrapper) << "GetTokenInformation failed with error" << lastError;
        return {};
    }

    return tokenInfo;
}

QString retrieveWindowsSid()
{
    if (const auto tokenInfo = getCurrentTokenInformation()) {
        return convertSidToStringSid(tokenInfo->User.Sid);
    }

    return {};
}

QString createSyncRootID(const QString &providerName, const QUuid &accountUUID, const QString &syncRootPath)
{
    // We must set specific Registry keys to make the progress bar refresh correctly and also add status icons into Windows Explorer
    // More about this here: https://docs.microsoft.com/en-us/windows/win32/shell/integrate-cloud-storage
    const auto windowsSid = retrieveWindowsSid();
    Q_ASSERT(!windowsSid.isEmpty());
    if (windowsSid.isEmpty()) {
        qCWarning(lcCfApiWrapper) << "Failed to set Registry keys for shell integration, as windowsSid is empty. Progress bar will not work.";
        return {};
    }

    // syncRootId should be: [storage provider ID]![Windows SID]![Account ID]![PathHash]
    // multiple sync folders for the same account) folder registry keys go like:
    // OpenCloud!S-1-5-21-2096452760-2617351404-2281157308-1001!AccountUUID!hash
    const auto stableFolderHash =
        QString::fromUtf8(QCryptographicHash::hash(QFileInfo(syncRootPath).canonicalFilePath().toUtf8(), QCryptographicHash::Sha256).toHex());
    return u"%1!%2!%3!%4"_s.arg(providerName, windowsSid, accountUUID.toString(QUuid::WithoutBraces), stableFolderHash);
}

void OCC::CfApiWrapper::registerSyncRoot(const VfsSetupParams &params, const std::function<void(QString)> &callback)
{
    static std::mutex register_mutex;
    const auto nativePath = QDir::toNativeSeparators(params.filesystemPath);
    winrt::StorageFolder::GetFolderFromPathAsync(reinterpret_cast<const wchar_t *>(nativePath.utf16()))
        .Completed([params, callback, mutex = &register_mutex](const winrt::IAsyncOperation<winrt::StorageFolder> &result, winrt::AsyncStatus status) {
            if (status != winrt::AsyncStatus::Completed) {
                callback(u"Failed to retrieve folder info: %1"_s.arg(Utility::formatWinError(result.ErrorCode())));
                return;
            }
            try {
                const auto iconPath = QCoreApplication::applicationFilePath();
                const auto id = createSyncRootID(params.providerName, params.account->uuid(), params.filesystemPath);
                const auto displayName = u"%1 - %2"_s.arg(params.providerDisplayName, params.account->davDisplayName());
                const auto version = params.providerVersion.toString();

                winrt::StorageProviderSyncRootInfo info;
                info.Id(reinterpret_cast<const wchar_t *>(id.utf16()));
                info.Path(result.GetResults());

                info.DisplayNameResource(reinterpret_cast<const wchar_t *>(displayName.utf16()));

                info.IconResource(reinterpret_cast<const wchar_t *>(iconPath.utf16()));
                info.HydrationPolicy(winrt::StorageProviderHydrationPolicy::Full);
                info.HydrationPolicyModifier(winrt::StorageProviderHydrationPolicyModifier::AutoDehydrationAllowed);
                info.PopulationPolicy(winrt::StorageProviderPopulationPolicy::AlwaysFull);
                info.InSyncPolicy(winrt::StorageProviderInSyncPolicy::PreserveInsyncForSyncEngine);
                info.HardlinkPolicy(winrt::StorageProviderHardlinkPolicy::None);
                info.Version(reinterpret_cast<const wchar_t *>(version.utf16()));
                info.AllowPinning(true);
                info.ShowSiblingsAsGroup(true);

#if 0
                winrt::Uri uri(L"https://xxx/files/trash/project?fileId=0e443965-2ebb-4673-9464-b2c1d388e666%2420221f24-95b3-471a-9ac0-8067aa845bbb");
                info.RecycleBinUri(uri);
#endif
                winrt::Streams::DataWriter streamWriter;
                streamWriter.WriteString(params.account->uuid().toString().toStdWString());
                info.Context(streamWriter.DetachBuffer());

                {
                    // don't confuse Windows with parallel registrations
                    std::lock_guard lock(*mutex);
                    winrt::StorageProviderSyncRootManager::Register(info);
                }
                // the example suggests to sleep, and we've seen connectSyncRoot fail with "Failed to register sync root WindowsError: -7ff8fb70: Element not
                // found." as we are not in the main thread this won't block
                std::this_thread::sleep_for(1s);
                callback({});
            } catch (const winrt::hresult_error &ex) {
                callback(u"Failed to register sync root %1"_s.arg(Utility::formatWinError(ex.code())));
            }
        });
}

#if 0
void unregisterSyncRootShellExtensions(const QString &providerName, const QString &folderAlias, const QString &accountDisplayName)
{
    const auto windowsSid = retrieveWindowsSid();
    Q_ASSERT(!windowsSid.isEmpty());
    if (windowsSid.isEmpty()) {
        qCWarning(lcCfApiWrapper) << "Failed to unregister SyncRoot Shell Extensions!";
        return;
    }

    const auto syncRootId = QStringLiteral("%1!%2!%3!%4").arg(providerName).arg(windowsSid).arg(accountDisplayName).arg(folderAlias);

    const QString providerSyncRootIdRegistryKey = syncRootManagerRegKey + QStringLiteral("\\") + syncRootId;

    OCC::Utility::registryDeleteKeyValue(HKEY_LOCAL_MACHINE, providerSyncRootIdRegistryKey, QStringLiteral("ThumbnailProvider"));
    OCC::Utility::registryDeleteKeyValue(HKEY_LOCAL_MACHINE, providerSyncRootIdRegistryKey, QStringLiteral("CustomStateHandler"));

    qCInfo(lcCfApiWrapper) << "Successfully unregistered SyncRoot Shell Extensions!";
}
#endif

OCC::Result<void, QString> OCC::CfApiWrapper::unregisterSyncRoot(const VfsSetupParams &params)
{
    try {
        winrt::StorageProviderSyncRootManager::Unregister(
            reinterpret_cast<const wchar_t *>(createSyncRootID(params.providerName, params.account->uuid(), params.filesystemPath).utf16()));
    } catch (winrt::hresult_error const &ex) {
        return u"unregisterSyncRoot failed: %1"_s.arg(Utility::formatWinError(ex.code()));
    }
    return {};
}

OCC::Result<CF_CONNECTION_KEY, QString> OCC::CfApiWrapper::connectSyncRoot(const QString &path, OCC::VfsCfApi *context)
{
    CF_CONNECTION_KEY key;
    const auto p = QDir::toNativeSeparators(path).toStdWString();
    const qint64 result =
        CfConnectSyncRoot(p.data(), cfApiCallbacks, context, CF_CONNECT_FLAG_REQUIRE_PROCESS_INFO | CF_CONNECT_FLAG_REQUIRE_FULL_FILE_PATH | CF_CONNECT_FLAG_BLOCK_SELF_IMPLICIT_HYDRATION, &key);
    Q_ASSERT(result == S_OK);
    if (result != S_OK) {
        return OCC::Utility::formatWinError(result);
    } else {
        return {std::move(key)};
    }
}

OCC::Result<void, QString> OCC::CfApiWrapper::disconnectSyncRoot(CF_CONNECTION_KEY &&key)
{
    const qint64 result = CfDisconnectSyncRoot(key);
    if (result != S_OK) {
        qCWarning(lcCfApiWrapper) << "Disconnecting sync root failed" << OCC::Utility::formatWinError(result);
        Q_ASSERT(result == S_OK);
        return OCC::Utility::formatWinError(result);
    } else {
        return {};
    }
}


bool OCC::CfApiWrapper::isSparseFile(const QString &path)
{
    const auto p = path.toStdWString();
    const auto attributes = GetFileAttributes(p.data());
    return (attributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
}

OCC::Utility::Handle OCC::CfApiWrapper::handleForPath(const QString &path)
{
    if (path.isEmpty()) {
        qCWarning(lcCfApiWrapper) << "empty path";
        return {};
    }

    if (!FileSystem::fileExists(path)) {
        qCWarning(lcCfApiWrapper) << "does not exist" << path;
        Q_ASSERT(false);
        return {};
    }

    const auto longpath = OCC::FileSystem::toFilesystemPath(path);
    OCC::Utility::Handle occHandle;
    if (std::filesystem::is_directory(longpath)) {
        HANDLE handle = nullptr;
        const uint32_t openResult = CfOpenFileWithOplock(longpath.native().data(), CF_OPEN_FILE_FLAG_NONE, &handle);
        occHandle = OCC::Utility::Handle{handle, &CfCloseHandle, openResult};
    } else {
        occHandle = OCC::Utility::Handle{CreateFile(longpath.native().data(), 0, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &CloseHandle};
    }
    if (!occHandle) {
        qCWarning(lcCfApiWrapper) << "no handle was created" << occHandle.errorMessage();
    }
    return occHandle;
}

template <>
OCC::CfApiWrapper::PlaceHolderInfo<CF_PLACEHOLDER_BASIC_INFO> OCC::CfApiWrapper::findPlaceholderInfo(const QString &path, bool withFileIdentity)
{
    if (auto handle = handleForPath(path)) {
        auto info = getPlaceholderInfo(handle, CF_PLACEHOLDER_INFO_BASIC, withFileIdentity);
        if (!info || info->empty()) {
            return {std::move(handle), {}};
        }
        return PlaceHolderInfo<CF_PLACEHOLDER_BASIC_INFO>(std::move(handle), std::move(*info));
    }
    return {};
}

template <>
OCC::CfApiWrapper::PlaceHolderInfo<CF_PLACEHOLDER_STANDARD_INFO> OCC::CfApiWrapper::findPlaceholderInfo(const QString &path, bool withFileIdentity)
{
    if (auto handle = handleForPath(path)) {
        auto info = getPlaceholderInfo(handle, CF_PLACEHOLDER_INFO_STANDARD, withFileIdentity);
        if (!info || info->empty()) {
            return {std::move(handle), {}};
        }
        return PlaceHolderInfo<CF_PLACEHOLDER_STANDARD_INFO>(std::move(handle), std::move(*info));
    }
    return {};
}


OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> OCC::CfApiWrapper::setPinState(const QString &path, OCC::PinState state, SetPinRecurseMode mode)
{
    const auto cfState = pinStateToCfPinState(state);
    const auto flags = pinRecurseModeToCfSetPinFlags(mode);

    const qint64 result = CfSetPinState(handleForPath(path).handle(), cfState, flags, nullptr);
    if (result == S_OK) {
        return OCC::Vfs::ConvertToPlaceholderResult::Ok;
    } else {
        qCWarning(lcCfApiWrapper) << "Couldn't set pin state" << state << "for" << path << "with recurse mode" << mode << ":"
                                  << OCC::Utility::formatWinError(result);
        return {u"Couldn't set pin state"_s};
    }
}

OCC::Result<void, QString> OCC::CfApiWrapper::createPlaceholderInfo(const QString &path, time_t modtime, qint64 size, const QByteArray &fileId)
{
    if (modtime <= 0) {
        return {u"Could not update metadata due to invalid modification time for %1: %2"_s.arg(path, modtime)};
    }

    const auto fileInfo = QFileInfo(path);
    const auto localBasePath = QDir::toNativeSeparators(fileInfo.path()).toStdWString();
    const auto relativePath = fileInfo.fileName().toStdWString();

    CF_PLACEHOLDER_CREATE_INFO cloudEntry = {};
    cloudEntry.FileIdentity = fileId.data();
    cloudEntry.FileIdentityLength = static_cast<DWORD>(fileId.length());

    cloudEntry.RelativeFileName = relativePath.data();
    cloudEntry.Flags = CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;
    cloudEntry.FsMetadata.FileSize.QuadPart = size;
    cloudEntry.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    OCC::Utility::UnixTimeToLargeIntegerFiletime(modtime, &cloudEntry.FsMetadata.BasicInfo.CreationTime);
    OCC::Utility::UnixTimeToLargeIntegerFiletime(modtime, &cloudEntry.FsMetadata.BasicInfo.LastWriteTime);
    OCC::Utility::UnixTimeToLargeIntegerFiletime(modtime, &cloudEntry.FsMetadata.BasicInfo.LastAccessTime);
    OCC::Utility::UnixTimeToLargeIntegerFiletime(modtime, &cloudEntry.FsMetadata.BasicInfo.ChangeTime);

    if (fileInfo.isDir()) {
        cloudEntry.Flags |= CF_PLACEHOLDER_CREATE_FLAG_DISABLE_ON_DEMAND_POPULATION;
        cloudEntry.FsMetadata.BasicInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        cloudEntry.FsMetadata.FileSize.QuadPart = 0;
    }

    qCDebug(lcCfApiWrapper) << "CfCreatePlaceholders" << path << modtime;
    const qint64 result = CfCreatePlaceholders(localBasePath.data(), &cloudEntry, 1, CF_CREATE_FLAG_NONE, nullptr);
    if (result != S_OK) {
        qCWarning(lcCfApiWrapper) << "Couldn't create placeholder info for" << path << ":" << Utility::formatWinError(result);
        return {u"Couldn't create placeholder info"_s};
    }

    const auto parentInfo = findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    const auto state = parentInfo && parentInfo.pinState() == PinState::OnlineOnly ? PinState::OnlineOnly : PinState::Inherited;

    if (!setPinState(path, state, NoRecurse)) {
        return {u"Couldn't set the default inherit pin state"_s};
    }

    return {};
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> OCC::CfApiWrapper::updatePlaceholderInfo(
    const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath)
{
    return updatePlaceholderState(path, modtime, size, fileId, replacesPath, CfApiUpdateMetadataType::AllMetadata);
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> OCC::CfApiWrapper::dehydratePlaceholder(
    const QString &path, time_t modtime, qint64 size, const QByteArray &fileId)
{
    if (modtime <= 0) {
        return {u"Could not update metadata due to invalid modification time for %1: %2"_s.arg(path, modtime)};
    }

    const auto info = findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(path);
    if (info) {
        setPinState(path, OCC::PinState::OnlineOnly, OCC::CfApiWrapper::NoRecurse);

        CF_FILE_RANGE dehydrationRange = {};
        dehydrationRange.Length.QuadPart = size;

        const qint64 result = CfUpdatePlaceholder(handleForPath(path).handle(), nullptr, fileId.data(), static_cast<DWORD>(fileId.size()), &dehydrationRange, 1,
            CF_UPDATE_FLAG_MARK_IN_SYNC | CF_UPDATE_FLAG_DEHYDRATE, nullptr, nullptr);
        if (result != S_OK) {
            const auto errorMessage = createErrorMessageForPlaceholderUpdateAndCreate(path, u"Couldn't update placeholder info"_s);
            qCWarning(lcCfApiWrapper) << errorMessage << path << ":" << OCC::Utility::formatWinError(result);
            return errorMessage;
        }
    } else {
        const qint64 result = CfConvertToPlaceholder(handleForPath(path).handle(), fileId.data(), static_cast<DWORD>(fileId.size()),
            CF_CONVERT_FLAG_MARK_IN_SYNC | CF_CONVERT_FLAG_DEHYDRATE, nullptr, nullptr);

        if (result != S_OK) {
            const auto errorMessage = createErrorMessageForPlaceholderUpdateAndCreate(path, u"Couldn't convert to placeholder"_s);
            qCWarning(lcCfApiWrapper) << errorMessage << path << ":" << OCC::Utility::formatWinError(result);
            return errorMessage;
        }
    }

    return OCC::Vfs::ConvertToPlaceholderResult::Ok;
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> OCC::CfApiWrapper::convertToPlaceholder(
    const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath)
{
    const qint64 result =
        CfConvertToPlaceholder(handleForPath(path).handle(), fileId.data(), static_cast<DWORD>(fileId.size()), CF_CONVERT_FLAG_MARK_IN_SYNC, nullptr, nullptr);
    Q_ASSERT(result == S_OK);
    if (result != S_OK) {
        const auto errorMessage = createErrorMessageForPlaceholderUpdateAndCreate(path, u"Couldn't convert to placeholder"_s);
        qCWarning(lcCfApiWrapper) << errorMessage << path << ":" << OCC::Utility::formatWinError(result);
        return errorMessage;
    }
    return updatePlaceholderState(path, modtime, size, fileId, replacesPath, CfApiUpdateMetadataType::AllMetadata);
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> OCC::CfApiWrapper::updatePlaceholderMarkInSync(
    const QString &path, const QByteArray &fileId, const QString &replacesPath)
{
    return updatePlaceholderState(path, {}, {}, fileId, replacesPath, CfApiUpdateMetadataType::OnlyBasicMetadata);
}

bool OCC::CfApiWrapper::isPlaceHolderInSync(const QString &filePath)
{
    if (const auto originalInfo = findPlaceholderInfo<CF_PLACEHOLDER_BASIC_INFO>(filePath)) {
        return originalInfo->InSyncState == CF_IN_SYNC_STATE_IN_SYNC;
    }

    return true;
}
