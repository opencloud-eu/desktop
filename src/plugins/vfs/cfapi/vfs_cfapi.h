/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "common/plugin.h"
#include "hydrationjob.h"
#include "libsync/vfs/vfs.h"

#include <QScopedPointer>

namespace OCC {
class HydrationJob;
class VfsCfApiPrivate;
class SyncJournalFileRecord;
namespace CfApiWrapper {
    struct CallBackContext;
}
class VfsCfApi : public Vfs
{
    Q_OBJECT

public:
    explicit VfsCfApi(QObject *parent = nullptr);
    ~VfsCfApi();

    Mode mode() const override;

    void stop() override;
    void unregisterFolder() override;

    bool socketApiPinStateActionsShown() const override;

    Result<void, QString> createPlaceholder(const SyncFileItem &item) override;

    bool needsMetadataUpdate(const SyncFileItem &) override;
    bool isDehydratedPlaceholder(const QString &filePath) override;

    bool setPinState(const QString &folderPath, PinState state) override;
    Optional<PinState> pinState(const QString &folderPath) override;
    AvailabilityResult availability(const QString &folderPath) override;

    void cancelHydration(const OCC::CfApiWrapper::CallBackContext &context);

    HydrationJob::Status finalizeHydrationJob(int64_t requestId);

    LocalInfo statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type) override;

public Q_SLOTS:
    void requestHydration(const CfApiWrapper::CallBackContext &context, qint64 requestedFileSize);
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

Q_SIGNALS:
    void hydrationRequestReady(int64_t requestId);
    void hydrationRequestFailed(int64_t requestId);
    void hydrationRequestFinished(int64_t requestId);

protected:
    Result<ConvertToPlaceholderResult, QString> updateMetadata(const SyncFileItem &, const QString &, const QString &) override;
    void startImpl(const VfsSetupParams &params) override;

private:
    void scheduleHydrationJob(const OCC::CfApiWrapper::CallBackContext &context, SyncJournalFileRecord &&record);
    void onHydrationJobFinished(HydrationJob *job);
    HydrationJob *findHydrationJob(int64_t requestId) const;

    QScopedPointer<VfsCfApiPrivate> d;
};

class CfApiVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsCfApi>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/vfs/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)
};

} // namespace OCC
