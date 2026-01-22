/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vfs_xattr.h"

#include "account.h"
#include "common/syncjournaldb.h"
#include "filesystem.h"
#include "libsync/xattr.h"
#include "syncfileitem.h"
#include "vfs/hydrationjob.h"
#include "theme.h"

#include <string>

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUuid>
#include <QString>

using namespace Qt::StringLiterals;
using namespace xattr;

Q_LOGGING_CATEGORY(lcVfsXAttr, "sync.vfs.xattr", QtInfoMsg)


namespace {
const QString ownerXAttrName = u"user.openvfs.owner"_s;
const QString etagXAttrName = u"user.openvfs.etag"_s;
const QString fileidXAttrName = u"user.openvfs.fileid"_s;
const QString modtimeXAttrName = u"user.openvfs.modtime"_s;
const QString fileSizeXAttrName = u"user.openvfs.fsize"_s;
const QString actionXAttrName = u"user.openvfs.action"_s;
const QString stateXAttrName = u"user.openvfs.state"_s;
const QString pinstateXAttrName = u"user.openvfs.pinstate"_s;

const QString fileStateVirtual = u"virtual"_s;
const QString fileStateHydrate = u"hydrate"_s;
const QString fileStateDehydrate = u"dehydrate"_s;
const QString fileStateHydrated = u"hydrated"_s;

}

namespace OCC {

VfsXAttr::VfsXAttr(QObject *parent)
    : Vfs(parent)
{
}

VfsXAttr::~VfsXAttr() = default;

Vfs::Mode VfsXAttr::mode() const
{
    return XAttr;
}

QString VfsXAttr::xattrOwnerString() const
{
    return u"%1:%2"_s.arg(Theme::instance()->appName(), params().account->uuid().toString(QUuid::WithoutBraces));
}

void VfsXAttr::startImpl(const VfsSetupParams &params)
{
    qCDebug(lcVfsXAttr, "Start XAttr VFS");

    // Lets claim the sync root directory for us
    const auto path = FileSystem::toFilesystemPath(params.filesystemPath);

    auto owner = FileSystem::Xattr::getxattr(path, ownerXAttrName);
    QString err;

    if (!owner) {
        // set the owner to opencloud to claim it
        if (!FileSystem::Xattr::setxattr(path, ownerXAttrName, xattrOwnerString() )) {
            err = tr("Unable to claim the sync root for files on demand");
        }
    } else if (owner.value() != xattrOwnerString()) {
        // owner is set. See if it is us
        qCDebug(lcVfsXAttr) << "Root-FS has a different owner" << owner.value() << "Not our vfs!";
        err = tr("The sync path is claimed by a different cloud, please check your setup");
    }
    if (err.isEmpty()) {
        Q_EMIT started();
    } else {
        Q_EMIT error(err);
    }
}

void VfsXAttr::stop()
{
}

void VfsXAttr::unregisterFolder()
{
}

bool VfsXAttr::socketApiPinStateActionsShown() const
{
    return true;
}

xattr::PlaceHolderAttribs VfsXAttr::placeHolderAttributes(const QString& path)
{
    PlaceHolderAttribs attribs;
    const auto fPath = FileSystem::toFilesystemPath(path);

    attribs._etag = FileSystem::Xattr::getxattr(fPath, etagXAttrName).value_or(QString());
    attribs._fileId = FileSystem::Xattr::getxattr(fPath, fileidXAttrName).value_or(QString());

    const QString tt = FileSystem::Xattr::getxattr(fPath, modtimeXAttrName).value_or(QString());
    attribs._modtime = tt.toLongLong();

    attribs._action = FileSystem::Xattr::getxattr(fPath, actionXAttrName).value_or(QString());
    attribs._size = FileSystem::Xattr::getxattr(fPath, fileSizeXAttrName).value_or(QString()).toLongLong();
    attribs._state = FileSystem::Xattr::getxattr(fPath, stateXAttrName).value_or(QString());
    attribs._pinState = FileSystem::Xattr::getxattr(fPath, pinstateXAttrName).value_or(QString());

    return attribs;
}

OCC::Result<void, QString> VfsXAttr::addPlaceholderAttribute(const QString &path, const QString& name, const QString& value)
{
    if (!name.isEmpty()) {
        auto success = FileSystem::Xattr::setxattr(FileSystem::toFilesystemPath(path), name, value);
        // Q_ASSERT(success);
        if (!success) {
            return tr("Failed to set the extended file attribute");
        }
    }

    return {};
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile)
{
    Q_UNUSED(replacesFile);
    const auto localPath = FileSystem::toFilesystemPath(filePath);

    qCDebug(lcVfsXAttr) << localPath << syncItem._type;

    // PlaceHolderAttribs attribs = placeHolderAttributes(localPath);
    OCC::Vfs::ConvertToPlaceholderResult res{OCC::Vfs::ConvertToPlaceholderResult::Ok};

    if (syncItem._type == ItemTypeVirtualFileDehydration) { //
        // FIXME: Error handling
        auto r = createPlaceholder(syncItem);
        if (!r) {
            res = OCC::Vfs::ConvertToPlaceholderResult::Locked;
        }
        addPlaceholderAttribute(filePath, actionXAttrName, fileStateDehydrate);
        addPlaceholderAttribute(filePath, stateXAttrName, fileStateVirtual);
        addPlaceholderAttribute(filePath, fileSizeXAttrName, QString::number(syncItem._size));
    } else if (syncItem._type == ItemTypeVirtualFileDownload) {
        addPlaceholderAttribute(filePath, actionXAttrName, fileStateHydrate);
        // file gets downloaded and becomes a normal file, the xattr gets removed
        FileSystem::Xattr::removexattr(localPath, stateXAttrName);
        FileSystem::Xattr::removexattr(localPath, fileSizeXAttrName);
    } else if (syncItem._type == ItemTypeVirtualFile) {
        qCDebug(lcVfsXAttr) << "updateMetadata for virtual file " << syncItem._type;
        addPlaceholderAttribute(filePath, stateXAttrName, fileStateVirtual);
        addPlaceholderAttribute(filePath, fileSizeXAttrName, QString::number(syncItem._size));
    } else if (syncItem._type == ItemTypeFile) {
        qCDebug(lcVfsXAttr) << "updateMetadata for normal file " << syncItem._type;
        FileSystem::Xattr::removexattr(localPath, fileSizeXAttrName);
    } else if (syncItem._type == ItemTypeDirectory) {
        qCDebug(lcVfsXAttr) << "updateMetadata for directory" << syncItem._type;
    } else {
        qCDebug(lcVfsXAttr) << "Unexpected syncItem Type" << syncItem._type;
        Q_UNREACHABLE();
    }

    FileSystem::setModTime(localPath, syncItem._modtime);

    addPlaceholderAttribute(filePath, fileidXAttrName, QString::fromUtf8(syncItem._fileId));
    addPlaceholderAttribute(filePath, etagXAttrName, syncItem._etag);

    // remove the action marker again
    FileSystem::Xattr::removexattr(localPath, actionXAttrName);

    return res;
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

        // set the xattrs
        // the file is not virtual any more, remove the xattrs. No state xattr means local available data
        if (!(FileSystem::Xattr::removexattr(targetPath, stateXAttrName) &&
              FileSystem::Xattr::removexattr(targetPath, actionXAttrName) &&
              FileSystem::Xattr::removexattr(targetPath, fileSizeXAttrName) )) {
            qCInfo(lcVfsXAttr) << u"Removing extended file attribute action failed for" << targetPath;
        }

        time_t modtime = item->_modtime;
        qCInfo(lcVfsXAttr) << u"Setting hydrated file's modtime to" << modtime;

        if (!FileSystem::setModTime(targetPath, modtime)) {
            qCInfo(lcVfsXAttr) << u"Failed to set the mod time of the hydrated file" << targetPath;
            // What can be done in this error condition
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
    const auto path = QDir::toNativeSeparators(params().filesystemPath + item.localName());

    qCDebug(lcVfsXAttr) << path;

    QFile file(path);
    if (file.exists()
        && FileSystem::fileChanged(FileSystem::toFilesystemPath(path),
                                   FileSystem::FileChangedInfo::fromSyncFileItem(&item))) {
        return tr("Cannot create a placeholder because a file with the placeholder name already exist");
    }

    if (!file.open(QFile::ReadWrite | QFile::Truncate)) {
        return file.errorString();
    }
    file.write("");
    file.close();

    FileSystem::Xattr::removexattr(FileSystem::toFilesystemPath(path), actionXAttrName); // remove the action xattr

    // FIXME only write attribs if they're different, and/or all together
    addPlaceholderAttribute(path, fileSizeXAttrName, QString::number(item._size));
    addPlaceholderAttribute(path, stateXAttrName, fileStateVirtual);
    addPlaceholderAttribute(path, fileidXAttrName, QString::fromUtf8(item._fileId));
    addPlaceholderAttribute(path, etagXAttrName, item._etag);
    FileSystem::setModTime(path, item._modtime);

    // Ensure the pin state isn't contradictory
    const auto pin = pinState(path);
    if (pin && *pin == PinState::AlwaysLocal) {
        setPinState(item._renameTarget, PinState::Unspecified);
    }

    return {};
}

HydrationJob* VfsXAttr::hydrateFile(const QByteArray &fileId, const QString &targetPath)
{
    qCInfo(lcVfsXAttr) << u"Requesting hydration for" << fileId;
    if (_hydrationJobs.contains(fileId)) {
        qCWarning(lcVfsXAttr) << u"Ignoring hydration request for running hydration for fileId" << fileId;
        return {};
    }

    HydrationJob *hydration = new HydrationJob(this, fileId, std::make_unique<QFile>(targetPath), nullptr);
    hydration->setTargetFile(targetPath);
    _hydrationJobs.insert(fileId, hydration);

    // set an action attrib
    addPlaceholderAttribute(targetPath, actionXAttrName, fileStateHydrate);

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
    const QString path = params().filesystemPath + item.localName();

    return QFileInfo::exists(path);
}

bool VfsXAttr::isDehydratedPlaceholder(const QString &filePath)
{

    if (QFileInfo::exists(filePath)) {
        const auto attribs = placeHolderAttributes(filePath);
        return (attribs.state() == fileStateVirtual);
    }
    return false;
}

LocalInfo VfsXAttr::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    const QString p = FileSystem::fromFilesystemPath(path.path());
    if (type == ItemTypeFile) {

        auto attribs = placeHolderAttributes(p);
        if (attribs.state() == fileStateVirtual) {
            type = ItemTypeVirtualFile;
            if (attribs.pinState() == pinStateToString(PinState::AlwaysLocal)) {
                type = ItemTypeVirtualFileDownload;
            }
        } else {
            if (attribs.pinState() == pinStateToString(PinState::OnlineOnly)) {
                type = ItemTypeVirtualFileDehydration;
            }
        }
    }
    qCDebug(lcVfsXAttr) << p << Utility::enumToString(type);

    return LocalInfo(path, type);
}

// expects a relative path
bool VfsXAttr::setPinState(const QString &folderPath, PinState state)
{
    const auto localPath = QDir::toNativeSeparators(params().filesystemPath + folderPath);
    qCDebug(lcVfsXAttr) << localPath << state;

    if (state == PinState::AlwaysLocal || state == PinState::OnlineOnly || state == PinState::Excluded) {
        auto stateStr = pinStateToString(state);
        addPlaceholderAttribute(localPath, pinstateXAttrName, stateStr);
    } else {
        qCDebug(lcVfsXAttr) << "Do not set Pinstate" << pinStateToString(state) << ", remove pinstate xattr";
        FileSystem::Xattr::removexattr(FileSystem::toFilesystemPath(localPath), pinstateXAttrName);
    }
    return true;
}

Optional<PinState> VfsXAttr::pinState(const QString &folderPath)
{

    PlaceHolderAttribs attribs = placeHolderAttributes(folderPath);

    PinState pState{PinState::Unspecified}; // the default if no owner or state is set
    const QString pin = attribs.pinState();

    if (pin == pinStateToString(PinState::AlwaysLocal)) {
        pState = PinState::AlwaysLocal;
    } else if (pin == pinStateToString(PinState::Excluded)) {
        pState = PinState::Excluded;
    } else if (pin.isEmpty() || pin == pinStateToString(PinState::Inherited)) {
        pState = PinState::Inherited;
    } else if (pin == pinStateToString(PinState::OnlineOnly)) {
        pState = PinState::OnlineOnly;
    }
    qCDebug(lcVfsXAttr) << folderPath << pState;

    return pState;
}

Vfs::AvailabilityResult VfsXAttr::availability(const QString &folderPath)
{

    const auto basePinState = pinState(folderPath);
    Vfs::AvailabilityResult res {VfsItemAvailability::Mixed};

    if (basePinState) {
        switch (*basePinState) {
        case OCC::PinState::AlwaysLocal:
            res = VfsItemAvailability::AlwaysLocal;
            break;
        case OCC::PinState::Inherited:
            break;
        case OCC::PinState::OnlineOnly:
            res = VfsItemAvailability::OnlineOnly;
            break;
        case OCC::PinState::Unspecified:
            break;
        case OCC::PinState::Excluded:
            break;
        };
        res = VfsItemAvailability::Mixed;
    } else {
        res = AvailabilityError::NoSuchItem;
    }
    qCDebug(lcVfsXAttr) << folderPath << res.get();

    return res;
}

void VfsXAttr::fileStatusChanged(const QString& systemFileName, SyncFileStatus fileStatus)
{
    if (fileStatus.tag() == SyncFileStatus::StatusExcluded) {
        setPinState(systemFileName, PinState::Excluded);
        return;
    }

    qCDebug(lcVfsXAttr) << systemFileName << fileStatus;
}

QString VfsXAttr::pinStateToString(PinState pState) const
{
    switch (pState) {
    case OCC::PinState::AlwaysLocal:
        return u"alwayslocal"_s;
    case OCC::PinState::Inherited:
        return u"interited"_s;
    case OCC::PinState::OnlineOnly:
        return u"onlineonly"_s;
    case OCC::PinState::Unspecified:
        return u"unspecified"_s;
    case OCC::PinState::Excluded:
        return u"excluded"_s;
    };
    return u"unspecified"_s;
}

PinState VfsXAttr::stringToPinState(const QString& str) const
{
    if (str.isEmpty() || str == u"unspecified"_s) {
        return PinState::Unspecified;
    } else if( str == u"alwayslocal"_s) {
        return PinState::AlwaysLocal;
    } else if( str == u"inherited"_s) {
        return PinState::Inherited;
    } else if( str == u"unspecified"_s) {
        return PinState::Unspecified;
    } else if( str == u"excluded"_s) {
        return PinState::Excluded;
    }
    return PinState::Unspecified;
}

} // namespace OCC
