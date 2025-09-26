/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QObject>
#include <QScopedPointer>

#include "vfs/vfs.h"
#include "common/plugin.h"
#include "common/result.h"

#include "config.h"

namespace xattr {

struct PlaceHolderAttribs {
public:
    qint64 size() const { return _size; }
    QString fileId() const { return _fileId; }
    time_t modTime() const {return _modtime; }
    QString eTag() const { return _etag; }
    QString pinState() const { return _pinState; }
    QString action() const { return _action; }
    QString state() const { return _state; }

    qint64 _size;
    QString _fileId;
    time_t _modtime;
    QString _etag;
    QString _pinState;
    QString _action;
    QString _state;

};
}

namespace OCC {
class HydrationJob;

using namespace xattr;

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

    QString pinStateToString(PinState) const;
    PinState stringToPinState(const QString&) const;

Q_SIGNALS:
    void finished(Result<void, QString>);

public Q_SLOTS:
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

    void slotHydrateJobFinished();

protected:
    void startImpl(const VfsSetupParams &params) override;

private:
    QString xattrOwnerString() const;
    PlaceHolderAttribs placeHolderAttributes(const QString& path);
    OCC::Result<void, QString> addPlaceholderAttribute(const QString &path, const QString &name = {}, const QString &val = {});
    OCC::Result<void, QString> removePlaceHolderAttributes(const QString& path);

    QMap<QByteArray, HydrationJob*> _hydrationJobs;
};

class XattrVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsXAttr>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/common/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)
};

} // namespace OCC
