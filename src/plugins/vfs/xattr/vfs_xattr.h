/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QObject>
#include <QScopedPointer>

#include "common/vfs.h"
#include "common/plugin.h"
#include "common/result.h"

#include "config.h"

namespace xattr {

struct PlaceHolderAttribs {
public:
    qint64 size() const { return _size; }
    QByteArray fileId() const { return _fileId; }
    time_t modTime() const {return _modtime; }
    QString eTag() const { return _etag; }
    QByteArray pinState() const { return _pinState; }
    QByteArray action() const { return _action; }
    QByteArray state() const { return _state; }
    QByteArray owner() const { return _owner; }

    // the owner must not be empty but set to the ownerString, that consists
    // of the app name and an instance ID
    // If no xattrs are set at all, the method @placeHolderAttributes sets it
    // to our name and claims the space

    // Always check if we're the valid owner before accessing the xattrs.
    bool validOwner() const { return !_owner.isEmpty(); }

    qint64 _size;
    QByteArray _fileId;
    time_t _modtime;
    QString _etag;
    QByteArray _owner;
    QByteArray _pinState;
    QByteArray _action;
    QByteArray _state;

};
}

namespace OCC {

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
    OCC::Result<Vfs::ConvertToPlaceholderResult, QString> convertToPlaceholder(
            const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath);

    bool handleAction(const QString& path, const PlaceHolderAttribs &attribs);
    bool needsMetadataUpdate(const SyncFileItem &item) override;
    bool isDehydratedPlaceholder(const QString &filePath) override;
    LocalInfo statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type) override;

    bool setPinState(const QString &folderPath, PinState state) override;
    Optional<PinState> pinState(const QString &folderPath) override;
    AvailabilityResult availability(const QString &folderPath) override;

public Q_SLOTS:
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

protected:
    void startImpl(const VfsSetupParams &params) override;

private:
    QByteArray xattrOwnerString() const;
    PlaceHolderAttribs placeHolderAttributes(const QString& path);
    OCC::Result<void, QString> addPlaceholderAttribute(const QString &path, const QByteArray &name = {}, const QByteArray &val = {});

};

class XattrVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsXAttr>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/common/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)
};

} // namespace OCC
