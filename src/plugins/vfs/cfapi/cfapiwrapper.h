/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

// include order is important, this must be included before cfapi
#include <windows.h>
#include <winternl.h>

#include <cfapi.h>

#include "common/pinstate.h"
#include "common/result.h"
#include "common/utility_win.h"
#include "common/vfs.h"

struct CF_PLACEHOLDER_BASIC_INFO;

namespace OCC {

class VfsCfApi;

namespace CfApiWrapper {


    class PlaceHolderInfo
    {
    public:
        PlaceHolderInfo(std::vector<char> &&buffer = {});

        inline auto *get() const noexcept { return reinterpret_cast<CF_PLACEHOLDER_BASIC_INFO *>(const_cast<char *>(_data.data())); }
        inline auto *operator->() const noexcept { return get(); }
        inline explicit operator bool() const noexcept { return isValid(); }
        inline bool isValid() const noexcept { return !_data.empty(); };

        inline auto size() const { return _data.size(); }

        Optional<PinState> pinState() const;

    private:
        std::vector<char> _data;
    };

    void registerSyncRoot(const VfsSetupParams &params, const std::function<void(QString)> &callback);
    // void unregisterSyncRootShellExtensions(const QString &providerName, const QString &folderAlias, const QString &accountDisplayName);
    Result<void, QString> unregisterSyncRoot(const VfsSetupParams &params);

    Result<CF_CONNECTION_KEY, QString> connectSyncRoot(const QString &path, VfsCfApi *context);
    Result<void, QString> disconnectSyncRoot(CF_CONNECTION_KEY &&key);

    bool isSparseFile(const QString &path);

    OCC::Utility::Handle handleForPath(const QString &path);

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
