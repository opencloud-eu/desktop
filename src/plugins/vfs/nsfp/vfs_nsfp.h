// VfsNSFP header -- macOS NSFileProvider-based virtual file system plugin.
#pragma once

#include "common/plugin.h"
#include "vfs/vfs.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

#ifdef __OBJC__
#import <FileProvider/FileProvider.h>
#import <Foundation/Foundation.h>
#endif

namespace OCC {

class NsfpDomainManager;
class NsfpXpcHandler;

class VfsNSFP : public Vfs
{
    Q_OBJECT

public:
    explicit VfsNSFP(QObject *parent = nullptr);
    ~VfsNSFP() override;

    [[nodiscard]] Mode mode() const override;

    void stop() override;
    void unregisterFolder() override;

    [[nodiscard]] bool socketApiPinStateActionsShown() const override;

    [[nodiscard]] Result<void, QString> createPlaceholder(const SyncFileItem &item) override;

    [[nodiscard]] bool needsMetadataUpdate(const SyncFileItem &item) override;
    [[nodiscard]] bool isDehydratedPlaceholder(const QString &filePath) override;
    [[nodiscard]] LocalInfo statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type) override;

    [[nodiscard]] bool setPinState(const QString &relFilePath, PinState state) override;
    [[nodiscard]] Optional<PinState> pinState(const QString &relFilePath) override;
    [[nodiscard]] AvailabilityResult availability(const QString &folderPath) override;

public Q_SLOTS:
    void fileStatusChanged(const QString &systemFileName, OCC::SyncFileStatus fileStatus) override;

protected:
    [[nodiscard]] Result<ConvertToPlaceholderResult, QString> updateMetadata(
        const SyncFileItem &item, const QString &filePath, const QString &replacesFile) override;
    void startImpl(const VfsSetupParams &params) override;

private:
    /// Derives a stable domain identifier from account UUID and space ID.
    [[nodiscard]] QString domainIdentifier() const;

    std::unique_ptr<NsfpDomainManager> _domainManager;
    std::unique_ptr<NsfpXpcHandler> _xpcHandler;
    QString _domainId;

    /// Periodic timer that triggers sync cycles and metadata refresh so the
    /// Finder view stays current with server-side changes (like iCloud/OneDrive).
    QTimer _pollTimer;

    /// In-memory pin state cache. Key: relative file path. Value: PinState.
    /// For NSFP the journal does not store pin state natively, so we keep
    /// an in-memory map that persists for the lifetime of the VFS instance.
    QMap<QString, PinState> _pinStates;
};

class NsfpVfsPluginFactory : public QObject, public DefaultPluginFactory<VfsNSFP>
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "eu.opencloud.PluginFactory" FILE "libsync/vfs/vfspluginmetadata.json")
    Q_INTERFACES(OCC::PluginFactory)

public:
    Result<void, QString> prepare(const QString &path, const QUuid &accountUuid) const override;
};

} // namespace OCC
