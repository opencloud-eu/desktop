/*
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "common/plugin.h"
#include "libsync/vfs/hydrationjob.h"
#include "libsync/vfs/vfs.h"

#include <QMap>

namespace OCC {

class VfsMacFileProvider : public Vfs
{
    Q_OBJECT

public:
    explicit VfsMacFileProvider(QObject *parent = nullptr);
    ~VfsMacFileProvider() override;

    Mode mode() const override;

    void stop() override;
    void unregisterFolder() override;

    bool socketApiPinStateActionsShown() const override;

    Result<void, QString> createPlaceholder(const SyncFileItem &item) override;

    bool needsMetadataUpdate(const SyncFileItem &item) override;
    bool isDehydratedPlaceholder(const QString &filePath) override;
    LocalInfo statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type) override;

    bool setPinState(const QString &relFilePath, PinState state) override;
    Optional<PinState> pinState(const QString &relFilePath) override;
    AvailabilityResult availability(const QString &folderPath) override;

    HydrationJob *hydrateFile(const QByteArray &fileId, const QString &targetPath) override;

public Q_SLOTS:
    void fileStatusChanged(const QString &systemFileName, SyncFileStatus fileStatus) override;

private Q_SLOTS:
    void slotHydrateJobFinished();

protected:
    Result<ConvertToPlaceholderResult, QString> updateMetadata(const SyncFileItem &item, const QString &filePath, const QString &replacesFile) override;
    void startImpl(const VfsSetupParams &params) override;

private:
    QMap<QByteArray, HydrationJob *> _hydrationJobs;
};

class FileProviderVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsMacFileProvider>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/vfs/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)
public:
    Result<void, QString> prepare(const QString &path, const QUuid &accountUuid) const override;
};

} // namespace OCC
