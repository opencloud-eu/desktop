/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vfs_xattr.h"

#include "account.h"
#include "common/chronoelapsedtimer.h"
#include "common/syncjournaldb.h"
#include "filesystem.h"
#include "libsync/theme.h"
#include "libsync/xattr.h"
#include "syncfileitem.h"
#include "vfs/hydrationjob.h"

#include <openvfs/openvfsattributes.h>

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QString>
#include <QUuid>


using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcVfsXAttr, "sync.vfs.xattr", QtInfoMsg)


namespace {
QString openVFSExePath()
{
    return QStringLiteral(OPENVFS_EXE);
}


QByteArray xattrOwnerString(const QUuid &accountUuid)
{
    return OCC::Theme::instance()->appName().toUtf8() + ":" + accountUuid.toByteArray(QUuid::WithoutBraces);
}

QString openVFSConfigFilePath()
{
    return QStandardPaths::locate(QStandardPaths::ConfigLocation, u"openvfs/config.json"_s);
}

OpenVfsAttributes::PlaceHolderAttributes placeHolderAttributes(const std::filesystem::path &path)
{
    const auto data = OCC::FileSystem::Xattr::getxattr(path, QString::fromUtf8(OpenVfsConstants::XAttributeNames::Data));
    if (!data) {
        qCWarning(lcVfsXAttr) << "No OpenVFS xattr found for" << path;
    }
    return OpenVfsAttributes::PlaceHolderAttributes::fromData(path, data ? std::vector<uint8_t>{data->cbegin(), data->cend()} : std::vector<uint8_t>{});
}

OpenVfsAttributes::PlaceHolderAttributes placeHolderAttributes(const QString &path)
{
    return placeHolderAttributes(OCC::FileSystem::toFilesystemPath(path));
}

OCC::Result<void, QString> setPlaceholderAttributes(const OpenVfsAttributes::PlaceHolderAttributes &attributes)
{
    const auto data = attributes.toData();
    return OCC::FileSystem::Xattr::setxattr(attributes.absolutePath, QString::fromUtf8(OpenVfsConstants::XAttributeNames::Data),
        {reinterpret_cast<const char *>(data.data()), static_cast<qsizetype>(data.size())});
}

OCC::Result<void, QString> setPlaceholderAttributes(const OpenVfsAttributes::PlaceHolderAttributes &attributes, time_t modtime)
{
    if (const auto result = setPlaceholderAttributes(attributes); !result) {
        return result;
    }
    OCC::FileSystem::setModTime(attributes.absolutePath, modtime);
    return {};
}

OpenVfsConstants::PinStates convertPinState(OCC::PinState pState)
{
    switch (pState) {
    case OCC::PinState::AlwaysLocal:
        return OpenVfsConstants::PinStates::AlwaysLocal;
    case OCC::PinState::Inherited:
        return OpenVfsConstants::PinStates::Inherited;
    case OCC::PinState::OnlineOnly:
        return OpenVfsConstants::PinStates::OnlineOnly;
    case OCC::PinState::Excluded:
        return OpenVfsConstants::PinStates::OnlineOnly;
    case OCC::PinState::Unspecified:
        return OpenVfsConstants::PinStates::Unspecified;
    };
    Q_UNREACHABLE();
}

OCC::PinState convertPinState(OpenVfsConstants::PinStates pState)
{
    switch (pState) {
    case OpenVfsConstants::PinStates::AlwaysLocal:
        return OCC::PinState::AlwaysLocal;
    case OpenVfsConstants::PinStates::Inherited:
        return OCC::PinState::Inherited;
    case OpenVfsConstants::PinStates::OnlineOnly:
        return OCC::PinState::OnlineOnly;
    case OpenVfsConstants::PinStates::Unspecified:
        return OCC::PinState::Unspecified;
    case OpenVfsConstants::PinStates::Excluded:
        return OCC::PinState::Excluded;
    }
    Q_UNREACHABLE();
}

#ifdef Q_OS_LINUX

// Helper function to parse paths that the kernel inserts escape sequences
// for.
// https://github.com/qt/qtbase/blob/f47d9bcb45c77183c23e406df415ec2d9f4acbc4/src/corelib/io/qstorageinfo_linux.cpp#L72
QByteArray parseMangledPath(QByteArrayView path)
{
    // The kernel escapes with octal the following characters:
    //  space ' ', tab '\t', backslash '\\', and newline '\n'
    // See:
    // https://codebrowser.dev/linux/linux/fs/proc_namespace.c.html#show_mountinfo
    // https://codebrowser.dev/linux/linux/fs/seq_file.c.html#mangle_path

    QByteArray ret(path.size(), '\0');
    char *dst = ret.data();
    const char *src = path.data();
    const char *srcEnd = path.data() + path.size();
    while (src != srcEnd) {
        switch (*src) {
        case ' ': // Shouldn't happen
            return {};

        case '\\': {
            // It always uses exactly three octal characters.
            ++src;
            char c = (*src++ - '0') << 6;
            c |= (*src++ - '0') << 3;
            c |= (*src++ - '0');
            *dst++ = c;
            break;
        }

        default:
            *dst++ = *src++;
            break;
        }
    }
    // If "path" contains any of the characters this method is demangling,
    // "ret" would be oversized with extra '\0' characters at the end.
    ret.resize(dst - ret.data());
    return ret;
}
#endif
}

namespace OCC {

VfsXAttr::VfsXAttr(QObject *parent)
    : Vfs(parent)
{
}

VfsXAttr::~VfsXAttr() = default;

Vfs::Mode VfsXAttr::mode() const
{
    return Mode::XAttr;
}

void VfsXAttr::startImpl(const VfsSetupParams &params)
{
    qCDebug(lcVfsXAttr, "Start XAttr VFS");

    // Lets claim the sync root directory for us
    // set the owner to opencloud to claim it
    if (!FileSystem::Xattr::setxattr(params.root(), QString::fromUtf8(OpenVfsConstants::XAttributeNames::Owner), xattrOwnerString(params.account->uuid()))) {
        Q_EMIT error(tr("Unable to claim the sync root for files on demand"));
        return;
    }

    qCDebug(lcVfsXAttr) << "Mounting" << openVFSExePath() << params.root().toString();
    _openVfsProcess = new QProcess(this);
    // merging the channels and piping the output to our log lead to deadlocks
    _openVfsProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    const auto logPrefix = [path = params.filesystemPath(), this] { return u"[%1 %2] "_s.arg(QString::number(_openVfsProcess->processId()), path); };
    connect(_openVfsProcess, &QProcess::finished, _openVfsProcess, [logPrefix, this] {
        qCInfo(lcVfsXAttr) << logPrefix() << "finished" << _openVfsProcess->exitCode();
        _openVfsProcess->deleteLater();
    });
    connect(_openVfsProcess, &QProcess::started, this, [logPrefix, this] {
        qCInfo(lcVfsXAttr) << logPrefix() << u"started";
        // TODO:
        // give it time to mount
        QTimer::singleShot(1s, this, &Vfs::started);
    });
    connect(_openVfsProcess, &QProcess::errorOccurred, this, [logPrefix, this] { qCWarning(lcVfsXAttr) << logPrefix() << _openVfsProcess->errorString(); });
    _openVfsProcess->start(openVFSExePath(), {u"-d"_s, u"-i"_s, openVFSConfigFilePath(), params.filesystemPath()}, QIODevice::ReadOnly);
}

void VfsXAttr::stop()
{
    if (_openVfsProcess) {
        _openVfsProcess->terminate();
        _openVfsProcess->waitForFinished();
        _openVfsProcess->deleteLater();
    }
}

void VfsXAttr::unregisterFolder()
{
}

bool VfsXAttr::socketApiPinStateActionsShown() const
{
    return true;
}


Result<void, QString> XattrVfsPluginFactory::prepare(const QString &path, const QUuid &accountUuid) const
{
#ifdef Q_OS_LINUX
    // we can't use QStorageInfo as it does not list fuse mounts
    if (!_cacheTimer.isStarted() || _cacheTimer.duration() > 30s) {
        _fuseMountCache.clear();
        QFile file(u"/proc/self/mountinfo"_s);
        if (file.open(QIODevice::ReadOnly)) {
            const auto lines = file.readAll().split('\n');
            file.close();
            for (auto &line : lines) {
                auto fields = line.split(' ');
                if (fields.size() >= 9 && fields[8] == "fuse.openvfsfuse") {
                    _fuseMountCache << QString::fromUtf8(parseMangledPath(fields[4]));
                }
            }
        } else {
            qCWarning(lcVfsXAttr) << "Failed to read /proc/self/mountinfo" << file.errorString();
            return tr("Failed to read /proc/self/mountinfo");
        }
    }
    if (std::ranges::find_if(_fuseMountCache, [&](const QString &p) { return FileSystem::isChildPathOf2(path, p).testFlag(FileSystem::ChildResult::IsEqual); })
        != _fuseMountCache.cend()) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(u"fusermount"_s, {u"-zu"_s, path});
        // TODO: don't block?
        process.waitForFinished();
        if (process.exitCode() != 0) {
            const auto output = process.readAll();
            qCWarning(lcVfsXAttr) << "Failed to unmount the OpenVFS mount" << path << output;
            return tr("Failed to unmount the OpenVFS mount %1 Error:%2").arg(path, output);
        } else {
            qCDebug(lcVfsXAttr) << "Unmounted OpenVFS mount" << path;
        }
    }
#endif
    const auto fsPath = FileSystem::toFilesystemPath(path);
    if (!FileSystem::Xattr::supportsxattr(fsPath)) {
        qCDebug(lcVfsXAttr) << path << "does not support xattributes";
        return tr("The filesystem for %1 does not support xattributes.").arg(path);
    }
    if (const auto owner = FileSystem::Xattr::getxattr(fsPath, QString::fromUtf8(OpenVfsConstants::XAttributeNames::Owner))) {
        if (accountUuid.isNull()) {
            qCDebug(lcVfsXAttr) << path << "has an owner set" << owner << "Not our vfs!";
            return tr("The sync path is already claimed by a different account, please check your setup");
        } else if (owner != xattrOwnerString(accountUuid)) {
            // owner is set. See if it is us
            qCDebug(lcVfsXAttr) << path << "is claimed by a different account" << owner << "Not our vfs!";
            return tr("The sync path is claimed by a different cloud, please check your setup");
        }
    }
    if (!QFileInfo::exists(openVFSExePath())) {
        qCDebug(lcVfsXAttr) << "OpenVFS executable not found at" << openVFSExePath();
        return tr("OpenVFS executable not found, please install it");
    }
    const auto vfsConfig = openVFSConfigFilePath();
    if (!vfsConfig.isEmpty()) {
        qCDebug(lcVfsXAttr) << "Using config file" << vfsConfig;
    } else {
        return tr("Failed to find the OpenVFS config file, please check your installation.");
    }
    return {};
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile)
{
    if (syncItem._type == ItemTypeVirtualFileDehydration) {
        // replace the file with a placeholder
        if (const auto result = createPlaceholder(syncItem); !result) {
            qCCritical(lcVfsXAttr) << "Failed to create placeholder for" << filePath << result.error();
            return result.error();
        }
        return ConvertToPlaceholderResult::Ok;
    } else {
        OpenVfsAttributes::PlaceHolderAttributes attributes = [&] {
            // load the previous attributes
            if (!replacesFile.isEmpty()) {
                if (const auto attr = placeHolderAttributes(replacesFile)) {
                    return attr;
                }
            }
            if (const auto attr = placeHolderAttributes(filePath)) {
                return attr;
            }
            Q_ASSERT(QFileInfo::exists(filePath));
            // generate new meta data for an existing file
            auto attr = OpenVfsAttributes::PlaceHolderAttributes::create(
                FileSystem::toFilesystemPath(filePath), syncItem._etag.toStdString(), syncItem._fileId.toStdString(), syncItem._size);
            attr.state = OpenVfsConstants::States::Hydrated;
            return attr;
        }();
        Q_ASSERT(attributes);

        attributes.size = syncItem._size;
        attributes.fileId = syncItem._fileId.toStdString();
        attributes.etag = syncItem._etag.toStdString();

        qCDebug(lcVfsXAttr) << attributes.absolutePath << syncItem._type;

        switch (syncItem._type) {
        case ItemTypeVirtualFileDownload:
            attributes.state = OpenVfsConstants::States::Hydrating;
            break;
        case ItemTypeVirtualFile:
            [[fallthrough]];
        case ItemTypeVirtualFileDehydration:
            qCDebug(lcVfsXAttr) << "updateMetadata for virtual file " << syncItem._type;
            attributes.state = OpenVfsConstants::States::DeHydrated;
            break;
        case ItemTypeFile:
            [[fallthrough]];
        case ItemTypeDirectory:
            qCDebug(lcVfsXAttr) << "updateMetadata for" << syncItem._type;
            attributes.state = OpenVfsConstants::States::Hydrated;
            break;
        case ItemTypeSymLink:
            [[fallthrough]];
        case ItemTypeUnsupported:
            Q_UNREACHABLE();
        }

        if (const auto result = setPlaceholderAttributes(attributes, syncItem._modtime); !result) {
            qCCritical(lcVfsXAttr) << "Failed to update placeholder for" << filePath << result.error();
            return result.error();
        }

        return ConvertToPlaceholderResult::Ok;
    }
}

void VfsXAttr::slotHydrateJobFinished()
{
    HydrationJob *hydration = qobject_cast<HydrationJob*>(sender());

    const auto targetPath = FileSystem::toFilesystemPath(hydration->targetFileName());
    Q_ASSERT(!targetPath.empty());

    qCInfo(lcVfsXAttr) << u"Hydration Job finished for" << targetPath;

    if (std::filesystem::exists(targetPath)) {
        auto item = OCC::SyncFileItem::fromSyncJournalFileRecord(hydration->record());
        // the file is now downloaded
        item->_type = ItemTypeFile;

        if (auto inode = FileSystem::getInode(targetPath)) {
            item->_inode = inode.value();
        } else {
            qCWarning(lcVfsXAttr) << u"Failed to get inode for" << targetPath;
        }
        // Update the client sync journal database if the file modifications have been successful
        const auto result = this->params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
        if (!result) {
            qCWarning(lcVfsXAttr) << u"Error when setting the file record to the database" << result.error();
        } else {
            qCInfo(lcVfsXAttr) << u"Hydration succeeded" << targetPath;
        }
    } else {
        qCWarning(lcVfsXAttr) << u"Hydration succeeded but the file appears to be moved" << targetPath;
    }

    hydration->deleteLater();
    this->_hydrationJobs.remove(hydration->fileId());
}

Result<void, QString> VfsXAttr::createPlaceholder(const SyncFileItem &item)
{
    const auto path = params().root() / item.localName();
    if (std::filesystem::exists(path)) {
        if (item._type == ItemTypeVirtualFileDehydration && FileSystem::fileChanged(path, FileSystem::FileChangedInfo::fromSyncFileItem(&item))) {
            return tr("Cannot dehydrate a placeholder because the file changed");
        }
        Q_ASSERT(item._type == ItemTypeVirtualFile);
    }
    QFile file(path.get());
    if (!file.open(QFile::ReadWrite | QFile::Truncate)) {
        return file.errorString();
    }
    file.write("");
    file.close();

    const auto attributes = OpenVfsAttributes::PlaceHolderAttributes::create(path, item._etag.toStdString(), item._fileId.toStdString(), item._size);
    return setPlaceholderAttributes(attributes, item._modtime);
}

HydrationJob* VfsXAttr::hydrateFile(const QByteArray &fileId, const QString &targetPath)
{
    qCInfo(lcVfsXAttr) << u"Requesting hydration for" << fileId;
    if (_hydrationJobs.contains(fileId)) {
        qCWarning(lcVfsXAttr) << u"Ignoring hydration request for running hydration for fileId" << fileId;
        return {};
    }

    if (auto attr = placeHolderAttributes(targetPath)) {
        attr.state = OpenVfsConstants::States::Hydrating;
        if (auto res = setPlaceholderAttributes(attr); !res) {
            qCWarning(lcVfsXAttr) << u"Failed to set attributes for" << targetPath << res.error();
            return nullptr;
        }
    } else {
        qCWarning(lcVfsXAttr) << u"Failed to get attributes for" << targetPath;
        return nullptr;
    }
    HydrationJob *hydration = new HydrationJob(this, fileId, std::make_unique<QFile>(targetPath), nullptr);
    hydration->setTargetFile(targetPath);
    _hydrationJobs.insert(fileId, hydration);

    connect(hydration, &HydrationJob::finished, this, &VfsXAttr::slotHydrateJobFinished);
    connect(hydration, &HydrationJob::error, this, [this, hydration](const QString &error) {
        qCWarning(lcVfsXAttr) << u"Hydration failed" << error;
        this->_hydrationJobs.remove(hydration->fileId());
        hydration->deleteLater();
    });

    return hydration;
}

bool VfsXAttr::needsMetadataUpdate(const SyncFileItem &item)
{
    const auto path = params().root() / item.localName();
    // if the attributes do not exist we need to add them
    return QFileInfo::exists(path.toString()) && !placeHolderAttributes(path);
}

bool VfsXAttr::isDehydratedPlaceholder(const QString &filePath)
{
    if (QFileInfo::exists(filePath)) {
        return placeHolderAttributes(filePath).state == OpenVfsConstants::States::DeHydrated;
    }
    return false;
}

LocalInfo VfsXAttr::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    if (type == ItemTypeFile) {
        const auto attribs = placeHolderAttributes(path.path());
        if (attribs.state == OpenVfsConstants::States::DeHydrated) {
            type = ItemTypeVirtualFile;
            if (attribs.pinState == convertPinState(PinState::AlwaysLocal)) {
                type = ItemTypeVirtualFileDownload;
            }
        } else {
            if (attribs.pinState == convertPinState(PinState::OnlineOnly)) {
                type = ItemTypeVirtualFileDehydration;
            }
        }
    }
    qCDebug(lcVfsXAttr) << path.path().native() << Utility::enumToString(type);
    return LocalInfo(path, type);
}

bool VfsXAttr::setPinState(const QString &folderPath, PinState state)
{
    const auto localPath = params().root() / folderPath;
    qCDebug(lcVfsXAttr) << localPath.toString() << state;
    auto attribs = placeHolderAttributes(localPath);
    if (!attribs) {
        // the file is not yet converted
        return false;
    }
    attribs.pinState = convertPinState(state);
    if (!setPlaceholderAttributes(attribs)) {
        return false;
    }
    return true;
}

Optional<PinState> VfsXAttr::pinState(const QString &folderPath)
{
    for (auto relativePath = FileSystem::Path::relative(folderPath).get();; relativePath = relativePath.parent_path()) {
        const auto attributes = placeHolderAttributes(params().root() / relativePath);
        if (!attributes) {
            qCDebug(lcVfsXAttr) << "Couldn't find pin state for placeholder file" << folderPath;
            return {};
        }
        // if the state is inherited and we still have a parent path, retreive that instead.
        if (attributes.pinState != OpenVfsConstants::PinStates::Inherited || !relativePath.has_relative_path()) {
            return convertPinState(attributes.pinState);
        }
    }
}

Vfs::AvailabilityResult VfsXAttr::availability(const QString &folderPath)
{
    const auto attribs = placeHolderAttributes(params().root() / folderPath);
    if (attribs) {
        switch (convertPinState(attribs.pinState)) {
        case OCC::PinState::AlwaysLocal:
            return VfsItemAvailability::AlwaysLocal;
        case OCC::PinState::OnlineOnly:
            return VfsItemAvailability::OnlineOnly;
        case OCC::PinState::Inherited: {
            switch (attribs.state) {
            case OpenVfsConstants::States::Hydrated:
                return VfsItemAvailability::AllHydrated;
            case OpenVfsConstants::States::DeHydrated:
                return VfsItemAvailability::AllDehydrated;
            case OpenVfsConstants::States::Hydrating:
                return VfsItemAvailability::Mixed;
            }
        }
            Q_UNREACHABLE();
        case OCC::PinState::Unspecified:
            [[fallthrough]];
        case OCC::PinState::Excluded:
            return VfsItemAvailability::Mixed;
        };
    } else {
        return AvailabilityError::NoSuchItem;
    }
    return VfsItemAvailability::Mixed;
}

void VfsXAttr::fileStatusChanged(const QString& systemFileName, SyncFileStatus fileStatus)
{
    if (fileStatus.tag() == SyncFileStatus::StatusExcluded) {
        const FileSystem::Path rel = std::filesystem::relative(FileSystem::Path(systemFileName), params().root());
        setPinState(rel.toString(), PinState::Excluded);
        return;
    }
    qCDebug(lcVfsXAttr) << systemFileName << fileStatus;
}


} // namespace OCC
