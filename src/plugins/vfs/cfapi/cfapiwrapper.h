/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <memory>

#include "common/pinstate.h"
#include "common/result.h"
#include "common/vfs.h"

struct CF_PLACEHOLDER_BASIC_INFO;

namespace OCC {

class VfsCfApi;

namespace CfApiWrapper {

    class ConnectionKey
    {
    public:
        ConnectionKey();
        inline void *get() const { return _data.get(); }

    private:
        std::unique_ptr<void, void (*)(void *)> _data;
    };

    class FileHandle
    {
    public:
        using Deleter = void (*)(void *);

        FileHandle();
        FileHandle(void *data, Deleter deleter);

        inline void *get() const { return _data.get(); }
        inline explicit operator bool() const noexcept { return static_cast<bool>(_data); }

    private:
        std::unique_ptr<void, void (*)(void *)> _data;
    };

    class PlaceHolderInfo
    {
    public:
        using Deleter = void (*)(CF_PLACEHOLDER_BASIC_INFO *);

        PlaceHolderInfo();
        PlaceHolderInfo(CF_PLACEHOLDER_BASIC_INFO *data, Deleter deleter);

        inline CF_PLACEHOLDER_BASIC_INFO *get() const noexcept { return _data.get(); }
        inline CF_PLACEHOLDER_BASIC_INFO *operator->() const noexcept { return _data.get(); }
        inline explicit operator bool() const noexcept { return static_cast<bool>(_data); }

        Optional<PinState> pinState() const;

    private:
        std::unique_ptr<CF_PLACEHOLDER_BASIC_INFO, Deleter> _data;
    };

    Result<void, QString> registerSyncRoot(
        const QString &path, const QString &providerName, const QString &providerVersion, const QUuid &accountId, const QString &folderDisplayName);
    // void unregisterSyncRootShellExtensions(const QString &providerName, const QString &folderAlias, const QString &accountDisplayName);
    Result<void, QString> unregisterSyncRoot(const QString &path, const QString &providerName, const QUuid &accountId);

    Result<ConnectionKey, QString> connectSyncRoot(const QString &path, VfsCfApi *context);
    Result<void, QString> disconnectSyncRoot(ConnectionKey &&key);
    bool isAnySyncRoot(const QString &providerName, const QString &accountDisplayName);

    bool isSparseFile(const QString &path);

    FileHandle handleForPath(const QString &path);

    PlaceHolderInfo findPlaceholderInfo(const QString &path);

    enum SetPinRecurseMode { NoRecurse = 0, Recurse, ChildrenOnly };

    Result<OCC::Vfs::ConvertToPlaceholderResult, QString> setPinState(const QString &path, PinState state, SetPinRecurseMode mode);
    Result<void, QString> createPlaceholderInfo(const QString &path, time_t modtime, qint64 size, const QByteArray &fileId);
    Result<OCC::Vfs::ConvertToPlaceholderResult, QString> updatePlaceholderInfo(
        const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath = QString());
    Result<OCC::Vfs::ConvertToPlaceholderResult, QString> convertToPlaceholder(
        const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath);
    Result<OCC::Vfs::ConvertToPlaceholderResult, QString> dehydratePlaceholder(const QString &path, time_t modtime, qint64 size, const QByteArray &fileId);
    Result<OCC::Vfs::ConvertToPlaceholderResult, QString> updatePlaceholderMarkInSync(
        const QString &path, const QByteArray &fileId, const QString &replacesPath = QString());
    bool isPlaceHolderInSync(const QString &filePath);
}

} // namespace OCC
