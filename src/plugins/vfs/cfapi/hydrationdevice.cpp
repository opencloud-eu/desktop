// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "hydrationdevice.h"
#include "vfs_cfapi.h"

#include "libsync/common/filesystembase.h"
#include "libsync/filesystem.h"

#include <ntstatus.h>

using namespace OCC;
using namespace OCC::FileSystem::SizeLiterals;

Q_LOGGING_CATEGORY(lcCfApiHydrationDevice, "sync.vfs.cfapi.hydrationdevice", QtDebugMsg)
namespace {
constexpr auto ChunkSize = 4_KiB;
constexpr auto BufferSize = ChunkSize * 10;
}


CfApiHydrationJob *CfApiWrapper::HydrationDevice::requestHydration(const CfApiWrapper::CallBackContext &context, qint64 totalSize, QObject *parent)
{
    qCWarning(lcCfApiHydrationDevice) << u"Requesting hydration" << context;
    if (context.vfs->_hydrationJobs.contains(context.transferKey)) {
        qCWarning(lcCfApiHydrationDevice) << u"Ignoring hydration request for running hydration" << context;
        return {};
    }
    // we assume that transferKey is unique and that we don't receive multiple requests for the same file with the same key
    Q_ASSERT(std::find_if(context.vfs->_hydrationJobs.values().cbegin(), context.vfs->_hydrationJobs.values().cend(), [&context](const auto &it) {
        return it->context().path == context.path;
    }) == context.vfs->_hydrationJobs.values().cend());

    auto *hydration =
        new CfApiHydrationJob(context.vfs, context.fileId, std::make_unique<OCC::CfApiWrapper::HydrationDevice>(context, totalSize, parent), parent);
    hydration->setContext(context);
    context.vfs->_hydrationJobs.insert(context.transferKey, hydration);
    connect(hydration, &HydrationJob::finished, context.vfs, [hydration] {
        hydration->deleteLater();
        hydration->context().vfs->_hydrationJobs.remove(hydration->context().transferKey);
        if (QFileInfo::exists(hydration->context().path)) {
            auto item = OCC::SyncFileItem::fromSyncJournalFileRecord(hydration->record());
            // the file is now downloaded
            item->_type = ItemTypeFile;
            FileSystem::getInode(FileSystem::toFilesystemPath(hydration->context().path), &item->_inode);
            const auto result = hydration->context().vfs->params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
            if (!result) {
                qCWarning(lcCfApiHydrationDevice) << u"Error when setting the file record to the database" << hydration->context() << result.error();
            } else {
                qCInfo(lcCfApiHydrationDevice) << u"Hydration succeeded" << hydration->context();
            }
        } else {
            qCWarning(lcCfApiHydrationDevice) << u"Hydration succeeded but the file appears to be moved" << hydration->context();
        }
    });
    connect(hydration, &HydrationJob::error, context.vfs, [hydration](const QString &error) {
        hydration->deleteLater();
        hydration->context().vfs->_hydrationJobs.remove(hydration->context().transferKey);
        qCWarning(lcCfApiHydrationDevice) << u"Hydration failed" << hydration->context() << error;
    });
    return hydration;
}

CfApiWrapper::HydrationDevice::HydrationDevice(const CfApiWrapper::CallBackContext &context, qint64 totalSize, QObject *parent)
    : QIODevice(parent)
    , _context(context)
    , _totalSize(totalSize)
{
    _buffer.reserve(BufferSize);
}

qint64 CfApiWrapper::HydrationDevice::readData(char *, qint64)
{
    Q_UNREACHABLE();
}

qint64 CfApiWrapper::HydrationDevice::writeData(const char *data, qint64 len)
{
    _buffer.append(data, len);
    // the buffer should not grow above BufferSize
    Q_ASSERT(_buffer.size() <= BufferSize);
    const bool isLastChunk = _offset + _buffer.size() == _totalSize;

    if (_buffer.size() >= ChunkSize || isLastChunk) {
        // ensure we chunk the writes to the block size, if we are at then end of the file take all the rest
        const auto currentBlockLength = isLastChunk ? _buffer.size() : // we are in the last chunk, use everything
            _buffer.size() - _buffer.size() % ChunkSize; // align the current block with ChunkSize
        CF_OPERATION_INFO opInfo = {};
        opInfo.StructSize = sizeof(opInfo);
        opInfo.Type = CF_OPERATION_TYPE_TRANSFER_DATA;
        opInfo.ConnectionKey = _context.connectionKey;
        opInfo.TransferKey.QuadPart = _context.transferKey;

        CF_OPERATION_PARAMETERS opParams = {};
        opParams.ParamSize = CF_SIZE_OF_OP_PARAM(TransferData);
        opParams.TransferData.CompletionStatus = STATUS_SUCCESS;
        opParams.TransferData.Buffer = _buffer.data();
        opParams.TransferData.Offset.QuadPart = _offset;
        opParams.TransferData.Length.QuadPart = currentBlockLength;

        const qint64 cfExecuteResult = CfExecute(&opInfo, &opParams);
        if (cfExecuteResult != S_OK) {
            qCCritical(lcCfApiHydrationDevice) << u"Couldn't send transfer info" << _context << u":" << cfExecuteResult
                                               << Utility::formatWinError(cfExecuteResult);
            setErrorString(Utility::formatWinError(cfExecuteResult));
            return -1;
        }

        const auto trailing = _buffer.size() - currentBlockLength;
        Q_ASSERT(trailing >= 0);
        if (trailing) {
            // move the memory to the front
            std::memcpy(_buffer.data(), _buffer.data() + currentBlockLength, trailing);
        }
        _buffer.resize(trailing);

        _offset += currentBlockLength;
        // refresh Windows Copy Dialog progress
        const LARGE_INTEGER progressTotal = {.QuadPart = _totalSize};

        const LARGE_INTEGER progressCompleted = {.QuadPart = _offset};

        const qint64 cfReportProgressResult =
            CfReportProviderProgress(_context.connectionKey, {.QuadPart = _context.transferKey}, progressTotal, progressCompleted);

        if (cfReportProgressResult != S_OK) {
            qCCritical(lcCfApiHydrationDevice) << u"Couldn't report provider progress" << _context << u":" << cfReportProgressResult
                                               << Utility::formatWinError(cfReportProgressResult);
        }
    }
    return len;
}
