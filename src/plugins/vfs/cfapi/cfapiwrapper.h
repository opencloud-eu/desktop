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


    template <typename T>
    class PlaceHolderInfo
    {
    public:
        PlaceHolderInfo(Utility::Handle &&handle = {}, const std::vector<char> &&buffer = {})
            : _handle(std::move(handle))
            , _data(std::move(buffer))
        {
        }

        inline auto *get() const noexcept { return reinterpret_cast<T *>(const_cast<char *>(_data.data())); }
        inline auto *operator->() const noexcept { return get(); }
        inline explicit operator bool() const noexcept { return isValid(); }
        inline bool isValid() const noexcept { return !_data.empty(); };

        inline auto size() const { return _data.size(); }

        PinState pinState() const
        {
            Q_ASSERT(this);
            switch (get()->PinState) {
            case CF_PIN_STATE_UNSPECIFIED:
                return OCC::PinState::Unspecified;
            case CF_PIN_STATE_PINNED:
                return OCC::PinState::AlwaysLocal;
            case CF_PIN_STATE_UNPINNED:
                return OCC::PinState::OnlineOnly;
            case CF_PIN_STATE_INHERIT:
                return OCC::PinState::Inherited;
            case CF_PIN_STATE_EXCLUDED:
                return OCC::PinState::Inherited;
            }
            Q_UNREACHABLE();
        }

        const Utility::Handle &handle() const { return _handle; }

    private:
        Utility::Handle _handle;
        std::vector<char> _data;
    };

    void registerSyncRoot(const VfsSetupParams &params, const std::function<void(QString)> &callback);
    // void unregisterSyncRootShellExtensions(const QString &providerName, const QString &folderAlias, const QString &accountDisplayName);
    Result<void, QString> unregisterSyncRoot(const VfsSetupParams &params);

    Result<CF_CONNECTION_KEY, QString> connectSyncRoot(const QString &path, VfsCfApi *context);
    Result<void, QString> disconnectSyncRoot(CF_CONNECTION_KEY &&key);

    bool isSparseFile(const QString &path);

    OCC::Utility::Handle handleForPath(const QString &path);

    /**
     * The placeholder info can have a dynamic size, by default we don't query FileIdentity
     * If FileIdentity is required withFileIdentity must be set to true.
     */
    template <typename T>
    PlaceHolderInfo<T> findPlaceholderInfo(const QString &path, bool withFileIdentity = false)
    {
    }

    template <>
    PlaceHolderInfo<CF_PLACEHOLDER_BASIC_INFO> findPlaceholderInfo(const QString &path, bool withFileIdentity);

    template <>
    PlaceHolderInfo<CF_PLACEHOLDER_STANDARD_INFO> findPlaceholderInfo(const QString &path, bool withFileIdentity);

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
