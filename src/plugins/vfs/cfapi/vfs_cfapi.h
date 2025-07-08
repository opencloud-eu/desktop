/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QObject>
#include <QScopedPointer>

#include "common/plugin.h"
#include "common/vfs.h"

namespace OCC {
class HydrationJob;
class VfsCfApiPrivate;
class SyncJournalFileRecord;

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
    bool statTypeVirtualFile(csync_file_stat_t *stat, void *statData) override;

    bool setPinState(const QString &folderPath, PinState state) override;
    Optional<PinState> pinState(const QString &folderPath) override;
    AvailabilityResult availability(const QString &folderPath) override;

    void cancelHydration(const QString &requestId, const QString &path);

    int finalizeHydrationJob(const QString &requestId);

public Q_SLOTS:
    void requestHydration(const QString &requestId, const QString &path);
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

Q_SIGNALS:
    void hydrationRequestReady(const QString &requestId);
    void hydrationRequestFailed(const QString &requestId);
    void hydrationRequestFinished(const QString &requestId);

protected:
    Result<ConvertToPlaceholderResult, QString> updateMetadata(const SyncFileItem &, const QString &, const QString &) override;
    void startImpl(const VfsSetupParams &params) override;

private:
    void scheduleHydrationJob(const QString &requestId, const QString &folderPath);
    void onHydrationJobFinished(HydrationJob *job);
    HydrationJob *findHydrationJob(const QString &requestId) const;

    struct HasHydratedDehydrated
    {
        bool hasHydrated = false;
        bool hasDehydrated = false;
    };
    struct HydratationAndPinStates
    {
        Optional<PinState> pinState;
        HasHydratedDehydrated hydrationStatus;
    };
    HydratationAndPinStates computeRecursiveHydrationAndPinStates(const QString &path, const Optional<PinState> &basePinState);

    QScopedPointer<VfsCfApiPrivate> d;
};

class CfApiVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsCfApi>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "csync/common/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)
};

} // namespace OCC
