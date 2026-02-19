/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "common/chronoelapsedtimer.h"


#include <QObject>
#include <QScopedPointer>

#include "vfs/vfs.h"
#include "common/plugin.h"
#include "common/result.h"


namespace OCC {
class HydrationJob;

class VfsXAttr : public Vfs
{
    Q_OBJECT

public:
    explicit VfsXAttr(QObject *parent = nullptr);
    ~VfsXAttr() override;

    [[nodiscard]] Mode mode() const override;

    void stop() override;
    void unregisterFolder() override;

    [[nodiscard]] bool socketApiPinStateActionsShown() const override;

    Result<ConvertToPlaceholderResult, QString> updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile) override;
    // [[nodiscard]] bool isPlaceHolderInSync(const QString &filePath) const override { Q_UNUSED(filePath) return true; }

    Result<void, QString> createPlaceholder(const SyncFileItem &item) override;

    bool needsMetadataUpdate(const SyncFileItem &item) override;
    bool isDehydratedPlaceholder(const QString &filePath) override;
    LocalInfo statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type) override;

    bool setPinState(const QString &folderPath, PinState state) override;
    Optional<PinState> pinState(const QString &folderPath) override;
    AvailabilityResult availability(const QString &folderPath) override;

    HydrationJob* hydrateFile(const QByteArray &fileId, const QString& targetPath) override;

Q_SIGNALS:
    void finished(Result<void, QString>);

public Q_SLOTS:
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

    void slotHydrateJobFinished();

protected:
    void startImpl(const VfsSetupParams &params) override;

private:
    QMap<QByteArray, HydrationJob*> _hydrationJobs;
};

class XattrVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsXAttr>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/vfs/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)

public:
    Result<void, QString> prepare(const QString &path, const QUuid &accountUuid) const override;

private:
    mutable Utility::ChronoElapsedTimer _cacheTimer = false;
    mutable QStringList _fuseMountCache;
};

} // namespace OCC
