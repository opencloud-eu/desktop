/*
 * SPDX-FileCopyrightText: 2020 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "common/plugin.h"
#include "libsync/vfs/hydrationjob.h"
#include "libsync/vfs/vfs.h"
#include "plugins/vfs/cfapi/cfapiwrapper.h"

#include <QScopedPointer>

namespace OCC {
class HydrationJob;
class VfsCfApiPrivate;
class SyncJournalFileRecord;
namespace CfApiWrapper {
    struct CallBackContext;
    class HydrationDevice;
}
class CfApiHydrationJob : public HydrationJob
{
    Q_OBJECT
public:
    using HydrationJob::HydrationJob;
    void setContext(const CfApiWrapper::CallBackContext &context) { _context = context; }

    CfApiWrapper::CallBackContext context() const { return _context; }

private:
    CfApiWrapper::CallBackContext _context;
};

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

    LocalInfo statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type) override;

public Q_SLOTS:
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

protected:
    Result<ConvertToPlaceholderResult, QString> updateMetadata(const SyncFileItem &, const QString &, const QString &) override;
    void startImpl(const VfsSetupParams &params) override;

private:
    QMap<uint64_t, CfApiHydrationJob *> _hydrationJobs;
    CF_CONNECTION_KEY _connectionKey = {};
    friend class CfApiWrapper::HydrationDevice;
};

class CfApiVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsCfApi>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/vfs/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)
};

} // namespace OCC
