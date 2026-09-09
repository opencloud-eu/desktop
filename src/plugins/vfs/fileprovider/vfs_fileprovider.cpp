/*
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * VFS plugin for macOS: Files on Demand via placeholder files and xattr
 * (no OpenVFS). Placeholders are empty files with xattr: fileId, size, modtime, pinstate.
 */

#include "vfs_fileprovider.h"

#include "common/syncjournaldb.h"
#include "common/syncjournalfilerecord.h"
#include "filesystem.h"
#include "libsync/syncfileitem.h"
#include "libsync/theme.h"
#include "libsync/xattr.h"

#include <QFile>
#include <QLoggingCategory>
#include <QUuid>

using namespace OCC;
using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcVfsFileProvider, "sync.vfs.fileprovider", QtInfoMsg)

namespace {

const QString XATTR_FILE_ID = QStringLiteral("eu.opencloud.desktop.vfs.fileid");
const QString XATTR_SIZE = QStringLiteral("eu.opencloud.desktop.vfs.size");
const QString XATTR_MODTIME = QStringLiteral("eu.opencloud.desktop.vfs.modtime");
const QString XATTR_PLACEHOLDER = QStringLiteral("eu.opencloud.desktop.vfs.placeholder");
const QString XATTR_PIN_STATE = QStringLiteral("eu.opencloud.desktop.vfs.pinstate");

bool hasPlaceholderXattr(const std::filesystem::path &path)
{
    const auto data = FileSystem::Xattr::getxattr(path, XATTR_PLACEHOLDER);
    return data && *data == QByteArrayLiteral("1");
}

std::optional<QByteArray> getPlaceholderFileId(const std::filesystem::path &path)
{
    return FileSystem::Xattr::getxattr(path, XATTR_FILE_ID);
}

bool setPinStateXattr(const std::filesystem::path &path, PinState state)
{
    const auto result = FileSystem::Xattr::setxattr(path, XATTR_PIN_STATE, QByteArray::number(static_cast<int>(state)));
    return static_cast<bool>(result);
}

Optional<PinState> getPinStateXattr(const std::filesystem::path &path)
{
    const auto data = FileSystem::Xattr::getxattr(path, XATTR_PIN_STATE);
    if (!data || data->isEmpty())
        return {};
    bool ok = false;
    const int v = data->toInt(&ok);
    if (!ok || v < 0 || v > 4)
        return {};
    return static_cast<PinState>(v);
}

} // namespace

VfsMacFileProvider::VfsMacFileProvider(QObject *parent)
    : Vfs(parent)
{
}

VfsMacFileProvider::~VfsMacFileProvider() = default;

Vfs::Mode VfsMacFileProvider::mode() const
{
    return Vfs::Mode::MacFileProvider;
}

void VfsMacFileProvider::stop()
{
    for (auto *job : _hydrationJobs)
        job->abort();
    _hydrationJobs.clear();
}

void VfsMacFileProvider::unregisterFolder() { }

bool VfsMacFileProvider::socketApiPinStateActionsShown() const
{
    return true;
}

Result<void, QString> VfsMacFileProvider::createPlaceholder(const SyncFileItem &item)
{
    const auto path = params().root() / item.localName();
    if (path.exists() && !item.isDirectory()) {
        if (item._type == ItemTypeVirtualFileDehydration && FileSystem::fileChanged(path.get(), FileSystem::FileChangedInfo::fromSyncFileItem(&item))) {
            return tr("The file has changed since discovery");
        }
    }
    QFile file(path.toString());
    if (!file.open(QFile::ReadWrite | QFile::Truncate)) {
        return file.errorString();
    }
    file.write("");
    file.close();

    const auto fsPath = path.get();
    if (const auto r = FileSystem::Xattr::setxattr(fsPath, XATTR_PLACEHOLDER, QByteArrayLiteral("1")); !r)
        return r.error();
    if (const auto r = FileSystem::Xattr::setxattr(fsPath, XATTR_FILE_ID, item._fileId); !r)
        return r.error();
    if (const auto r = FileSystem::Xattr::setxattr(fsPath, XATTR_SIZE, QByteArray::number(item._size)); !r)
        return r.error();
    if (const auto r = FileSystem::Xattr::setxattr(fsPath, XATTR_MODTIME, QByteArray::number(static_cast<qint64>(item._modtime))); !r)
        return r.error();
    FileSystem::setModTime(fsPath, item._modtime);
    return {};
}

bool VfsMacFileProvider::needsMetadataUpdate(const SyncFileItem &item)
{
    const auto path = params().root() / item.localName();
    if (!path.exists())
        return false;
    const auto fsPath = path.get();
    if (!hasPlaceholderXattr(fsPath))
        return false;
    const auto sizeAttr = FileSystem::Xattr::getxattr(fsPath, XATTR_SIZE);
    const auto modAttr = FileSystem::Xattr::getxattr(fsPath, XATTR_MODTIME);
    if (!sizeAttr || sizeAttr->toLongLong() != item._size)
        return true;
    if (!modAttr || modAttr->toLongLong() != static_cast<qint64>(item._modtime))
        return true;
    return false;
}

bool VfsMacFileProvider::isDehydratedPlaceholder(const QString &filePath)
{
    return hasPlaceholderXattr(FileSystem::toFilesystemPath(filePath));
}

LocalInfo VfsMacFileProvider::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    if (type != ItemTypeFile)
        return LocalInfo(path, type);
    if (!hasPlaceholderXattr(path.path()))
        return LocalInfo(path, type);
    const auto pin = getPinStateXattr(path.path());
    if (pin == PinState::AlwaysLocal)
        return LocalInfo(path, ItemTypeVirtualFileDownload);
    return LocalInfo(path, ItemTypeVirtualFile);
}

bool VfsMacFileProvider::setPinState(const QString &relFilePath, PinState state)
{
    const auto localPath = params().root() / relFilePath;
    if (!localPath.exists()) {
        qCWarning(lcVfsFileProvider) << "setPinState: path does not exist" << localPath.toString();
        return false;
    }
    return setPinStateXattr(localPath.get(), state);
}

Optional<PinState> VfsMacFileProvider::pinState(const QString &relFilePath)
{
    const auto localPath = params().root() / relFilePath;
    if (!localPath.exists())
        return {};
    return getPinStateXattr(localPath.get());
}

Vfs::AvailabilityResult VfsMacFileProvider::availability(const QString &folderPath)
{
    const auto localPath = params().root() / folderPath;
    if (!localPath.exists())
        return AvailabilityError::NoSuchItem;
    const auto pin = getPinStateXattr(localPath.get());
    if (pin == PinState::AlwaysLocal)
        return VfsItemAvailability::AlwaysLocal;
    if (pin == PinState::OnlineOnly)
        return VfsItemAvailability::OnlineOnly;
    if (hasPlaceholderXattr(localPath.get()))
        return VfsItemAvailability::AllDehydrated;
    return VfsItemAvailability::Mixed;
}

HydrationJob *VfsMacFileProvider::hydrateFile(const QByteArray &fileId, const QString &targetPath)
{
    qCInfo(lcVfsFileProvider) << "Requesting hydration for" << fileId;
    if (_hydrationJobs.contains(fileId)) {
        qCWarning(lcVfsFileProvider) << "Ignoring hydration request, already running for fileId" << fileId;
        return nullptr;
    }
    if (!isDehydratedPlaceholder(targetPath)) {
        qCWarning(lcVfsFileProvider) << "Path is not a placeholder:" << targetPath;
        return nullptr;
    }
    auto *hydration = new HydrationJob(this, fileId, std::make_unique<QFile>(targetPath), nullptr);
    hydration->setTargetFile(targetPath);
    _hydrationJobs.insert(fileId, hydration);
    connect(hydration, &HydrationJob::finished, this, &VfsMacFileProvider::slotHydrateJobFinished);
    connect(hydration, &HydrationJob::error, this, [this, hydration](const QString &error) {
        qCWarning(lcVfsFileProvider) << "Hydration failed:" << error;
        _hydrationJobs.remove(hydration->fileId());
        hydration->deleteLater();
    });
    return hydration;
}

void VfsMacFileProvider::slotHydrateJobFinished()
{
    auto *hydration = qobject_cast<HydrationJob *>(sender());
    if (!hydration)
        return;
    qCInfo(lcVfsFileProvider) << "Hydration finished for" << hydration->targetFileName();
    const auto targetPath = FileSystem::toFilesystemPath(hydration->targetFileName());
    if (std::filesystem::exists(targetPath)) {
        auto item = SyncFileItem::fromSyncJournalFileRecord(hydration->record());
        item->_type = ItemTypeFile;
        if (auto inode = FileSystem::getInode(targetPath))
            item->_inode = inode.value();
        const auto result = params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
        if (!result)
            qCWarning(lcVfsFileProvider) << "Error updating file record after hydration:" << result.error();
        if (FileSystem::Xattr::removexattr(targetPath, XATTR_PLACEHOLDER)) { }
        if (FileSystem::Xattr::removexattr(targetPath, XATTR_FILE_ID)) { }
        if (FileSystem::Xattr::removexattr(targetPath, XATTR_SIZE)) { }
        if (FileSystem::Xattr::removexattr(targetPath, XATTR_MODTIME)) { }
    }
    _hydrationJobs.remove(hydration->fileId());
    hydration->deleteLater();
}

void VfsMacFileProvider::fileStatusChanged(const QString &systemFileName, SyncFileStatus fileStatus)
{
    if (fileStatus.tag() != SyncFileStatus::StatusExcluded)
        return;
    const auto absPath = FileSystem::toFilesystemPath(systemFileName);
    const auto rootPath = params().root().get();
    std::error_code ec;
    const auto relPath = std::filesystem::relative(absPath, rootPath, ec);
    if (ec || relPath.empty())
        return;
    setPinState(QString::fromStdString(relPath.generic_string()), PinState::Excluded);
}

Result<Vfs::ConvertToPlaceholderResult, QString> VfsMacFileProvider::updateMetadata(
    const SyncFileItem &item, const QString &filePath, const QString &replacesFile)
{
    if (item._type == ItemTypeVirtualFileDehydration) {
        if (const auto r = createPlaceholder(item); !r)
            return r.error();
        return ConvertToPlaceholderResult::Ok;
    }
    const auto fsPath = FileSystem::toFilesystemPath(filePath);
    if (!hasPlaceholderXattr(fsPath))
        return ConvertToPlaceholderResult::Ok;
    if (const auto r = FileSystem::Xattr::setxattr(fsPath, XATTR_SIZE, QByteArray::number(item._size)); !r)
        return r.error();
    if (const auto r = FileSystem::Xattr::setxattr(fsPath, XATTR_MODTIME, QByteArray::number(static_cast<qint64>(item._modtime))); !r)
        return r.error();
    FileSystem::setModTime(fsPath, item._modtime);
    return ConvertToPlaceholderResult::Ok;
}

void VfsMacFileProvider::startImpl(const VfsSetupParams &params)
{
    Q_UNUSED(params);
    Q_EMIT started();
}

Result<void, QString> FileProviderVfsPluginFactory::prepare(const QString &path, const QUuid &accountUuid) const
{
    Q_UNUSED(accountUuid);
    const auto canonicalPath = FileSystem::canonicalPath(path);
    const auto fsPath = FileSystem::toFilesystemPath(canonicalPath);
    if (fsPath.empty()) {
        return tr("The path is not valid.");
    }
    if (!FileSystem::Xattr::supportsxattr(fsPath)) {
        return tr("The filesystem for %1 does not support extended attributes. Files on Demand requires a filesystem with xattr support (e.g. APFS).")
            .arg(path);
    }
    return {};
}
