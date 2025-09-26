/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vfs_xattr.h"

#include "syncfileitem.h"
#include "filesystem.h"
#include "common/syncjournaldb.h"
#include "account.h"
#include "vfs/hydrationjob.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QUuid>

#include <sys/xattr.h>

Q_LOGGING_CATEGORY(lcVfsXAttr, "sync.vfs.xattr", QtInfoMsg)

namespace {

#if 0
std::atomic<int> id{1};

int requestId() {
    return id++;
}
#endif
}

namespace xattr {
constexpr auto ownerXAttrName = "user.openvfs.owner";
constexpr auto etagXAttrName = "user.openvfs.etag";
constexpr auto fileidXAttrName = "user.openvfs.fileid";
constexpr auto modtimeXAttrName = "user.openvfs.modtime";
constexpr auto fileSizeXAttrName = "user.openvfs.fsize";
constexpr auto actionXAttrName = "user.openvfs.action";
constexpr auto stateXAttrName = "user.openvfs.state";
constexpr auto pinstateXAttrName = "user.openvfs.pinstate";

OCC::Optional<QByteArray> get(const QByteArray &path, const QByteArray &name)
{
    QByteArray result(512, Qt::Initialization::Uninitialized);
    auto count = getxattr(path.constData(), name.constData(), result.data(), result.size());
    if (count > 0) {
        // xattr is special. It does not store C-Strings, but blobs.
        // So it needs to be checked, if a trailing \0 was added when writing
        // (as this software does) or not as the standard setfattr-tool
        // the following will handle both cases correctly.
        if (result[count-1] == '\0') {
            count--;
        }
        result.truncate(count);
        return result;
    } else {
        return {};
    }
}

bool set(const QByteArray &path, const QByteArray &name, const QByteArray &value)
{
    const auto returnCode = setxattr(path.constData(), name.constData(), value.constData(), value.size()+1, 0);
    return returnCode == 0;
}
}

namespace OCC {

using namespace xattr;

VfsXAttr::VfsXAttr(QObject *parent)
    : Vfs(parent)
{
}

VfsXAttr::~VfsXAttr() = default;

Vfs::Mode VfsXAttr::mode() const
{
    return XAttr;
}

void VfsXAttr::startImpl(const VfsSetupParams &)
{
    qCDebug(lcVfsXAttr(), "Start XAttr VFS");

    Q_EMIT started();
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

QByteArray VfsXAttr::xattrOwnerString() const
{
    auto s = QByteArray(APPLICATION_EXECUTABLE);
    s.append(":");
    s.append(_setupParams->account->uuid().toByteArray(QUuid::WithoutBraces));
    return s;
}

PlaceHolderAttribs VfsXAttr::placeHolderAttributes(const QString& path)
{
    PlaceHolderAttribs attribs;

    // lambda to handle the Optional return val of xattrGet
    auto xattr = [](const QByteArray& p, const QByteArray& name) {
        const auto value = xattr::get(p, name);
        if (value) {
            return *value;
        } else {
            return QByteArray();
        }
    };

    const auto p = path.toUtf8();

    attribs._owner = xattr(p, ownerXAttrName);
    if (attribs._owner.isEmpty()) {
        // lets claim it
        attribs._owner = xattrOwnerString();
    } else {
        if (attribs._owner != xattrOwnerString()) {
            qCDebug(lcVfsXAttr) << "XAttributes not from our instance";
            attribs._owner.clear();
            return attribs;
        }
    }

    attribs._etag = QString::fromUtf8(xattr(p, etagXAttrName));
    attribs._fileId = xattr(p, fileidXAttrName);

    const QByteArray& tt = xattr(p, modtimeXAttrName);
    attribs._modtime = tt.toLongLong();

    attribs._action = xattr(p, actionXAttrName);
    attribs._size = xattr(p, fileSizeXAttrName).toLongLong();
    attribs._state = xattr(p, stateXAttrName);
    attribs._pinState = xattr(p, pinstateXAttrName);

    return attribs;
}

OCC::Result<void, QString> VfsXAttr::addPlaceholderAttribute(const QString &path, const QByteArray& name, const QByteArray& value)
{
    const PlaceHolderAttribs attribs = placeHolderAttributes(path);

    if (! attribs.validOwner()) {
        return QStringLiteral("Can not overwrite attributes - not our placeholder");
    }

    // FIXME: this always sets the name, can be optimized
    auto success = xattr::set(path.toUtf8(), ownerXAttrName, xattrOwnerString());
    if (!success) {
        return QStringLiteral("Failed to set the extended attribute for owner");
    }

    if (!name.isEmpty()) {
        auto success = xattr::set(path.toUtf8(), name, value);
        if (!success) {
            return QStringLiteral("Failed to set the extended attribute");
        }
    }

    return {};
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile)
{
    const auto localPath = QDir::toNativeSeparators(filePath);
    const auto replacesPath = QDir::toNativeSeparators(replacesFile);

    qCDebug(lcVfsXAttr()) << localPath;

    PlaceHolderAttribs attribs = placeHolderAttributes(localPath);
    OCC::Vfs::ConvertToPlaceholderResult res{OCC::Vfs::ConvertToPlaceholderResult::Ok};

    if (attribs.validOwner() && attribs.state().isEmpty()) { // No status
        // There is no state, so it is a normal, hydrated file
    }

    if (syncItem._type == ItemTypeVirtualFileDehydration) { //
        addPlaceholderAttribute(localPath, actionXAttrName, "dehydrate");
        // FIXME: Error handling
        auto r = createPlaceholder(syncItem);
        if (!r) {
            res = OCC::Vfs::ConvertToPlaceholderResult::Locked;
        }

    } else if (syncItem._type == ItemTypeVirtualFileDownload) {
        addPlaceholderAttribute(localPath, actionXAttrName, "hydrate");
        // start to download? FIXME
    } else if (syncItem._type == ItemTypeVirtualFile) {
            FileSystem::setModTime(localPath, syncItem._modtime);

            // FIXME only write attribs if they're different, and/or all together
            addPlaceholderAttribute(localPath, fileSizeXAttrName, QByteArray::number(syncItem._size));
            addPlaceholderAttribute(localPath, stateXAttrName, Utility::enumToDisplayName(PinState::OnlineOnly).toUtf8());
            addPlaceholderAttribute(localPath, fileidXAttrName, syncItem._fileId);
            addPlaceholderAttribute(localPath, etagXAttrName, syncItem._etag.toUtf8());
    } else {
            // FIXME anything to check for other types?
        qCDebug(lcVfsXAttr) << "Unexpected syncItem Type";
    }

    // FIXME Errorhandling
    return res;
}

Result<void, QString> VfsXAttr::createPlaceholder(const SyncFileItem &item)
{
    if (item._modtime <= 0) {
        return {tr("Error updating metadata due to invalid modification time")};
    }

    const auto path = QDir::toNativeSeparators(params().filesystemPath + item.localName());

    qCDebug(lcVfsXAttr()) << path;

    QFile file(path);
    // FIXME: Check to not overwrite an existing file
    // if (file.exists() && file.size() > 1
    //    && !FileSystem::verifyFileUnchanged(path, item._size, item._modtime)) {
    //    return QStringLiteral("Cannot create a placeholder because a file with the placeholder name already exist");
    // }

    if (!file.open(QFile::ReadWrite | QFile::Truncate)) {
        return file.errorString();
    }

    file.write("");
    file.close();

    /*
     * Only write the state and the executor, the rest is added in the updateMetadata() method
    */
    addPlaceholderAttribute(path, stateXAttrName, Utility::enumToDisplayName(PinState::OnlineOnly).toUtf8());


    // Ensure the pin state isn't contradictory
    const auto pin = pinState(item.localName());
    if (pin && *pin == PinState::AlwaysLocal) {
        setPinState(item._renameTarget, PinState::Unspecified);
    }
    return {};
}

HydrationJob* VfsXAttr::hydrateFile(const QByteArray &fileId)
{
    qCInfo(lcVfsXAttr) << u"Requesting hydration for" << fileId;
    if (_hydrationJobs.contains(fileId)) {
        qCWarning(lcVfsXAttr) << u"Ignoring hydration request for running hydration for fileId" << fileId;
        return {};
    }

    QString fileName; // FIXME: Needs to come from outside
    // Create a device to write in

    HydrationJob *hydration = new HydrationJob(this, fileId, std::make_unique<QFile>(fileName), nullptr);
    _hydrationJobs.insert(fileId, hydration);

    connect(hydration, &HydrationJob::finished, this, [this, fileName, hydration] {

        if (QFileInfo::exists(fileName)) {
            auto item = OCC::SyncFileItem::fromSyncJournalFileRecord(hydration->record());
            // the file is now downloaded
            item->_type = ItemTypeFile;
            FileSystem::getInode(fileName, &item->_inode);
            const auto result = this->params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
            if (!result) {
                qCWarning(lcVfsXAttr) << u"Error when setting the file record to the database" << result.error();
            } else {
                qCInfo(lcVfsXAttr) << u"Hydration succeeded" << fileName;
            }
        } else {
            qCWarning(lcVfsXAttr) << u"Hydration succeeded but the file appears to be moved" << fileName;
        }

        hydration->deleteLater();
        this->_hydrationJobs.remove(hydration->fileId());
    });
    connect(hydration, &HydrationJob::error, this, [this, hydration](const QString &error) {
        qCWarning(lcVfsXAttr) << u"Hydration failed" << error;
        hydration->deleteLater();
        this->_hydrationJobs.remove(hydration->fileId());
    });

    return hydration;
}

OCC::Result<Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::convertToPlaceholder(
        const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath)
{
    Q_UNUSED(modtime)
    Q_UNUSED(size)
    Q_UNUSED(fileId)
    Q_UNUSED(replacesPath)

    // Nothing necessary - no idea why, taken from previews...
    qCDebug(lcVfsXAttr) << "empty function returning ok, DOUBLECHECK" << path ;
    return {ConvertToPlaceholderResult::Ok};
}

bool VfsXAttr::needsMetadataUpdate(const SyncFileItem &)
{
    qCDebug(lcVfsXAttr()) << "returns false by default DOUBLECHECK";
    return false;
}

bool VfsXAttr::isDehydratedPlaceholder(const QString &filePath)
{
    const auto fi = QFileInfo(filePath);
    if (fi.exists()) {
        const auto attribs = placeHolderAttributes(filePath);
        return (attribs.validOwner() &&
                attribs.state() == Utility::enumToDisplayName(PinState::OnlineOnly).toUtf8());
    }
    return false;
}

LocalInfo VfsXAttr::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    if (type == ItemTypeFile) {
        const QString p = QString::fromUtf8(path.path().c_str()); //FIXME?
        qCDebug(lcVfsXAttr()) << p;

        auto attribs = placeHolderAttributes(p);
        if (attribs.validOwner()) {
            bool shouldDownload{false};
            if (attribs.pinState() == Utility::enumToDisplayName(PinState::AlwaysLocal).toUtf8()) {
                shouldDownload = true;
            }

            // const auto shouldDownload = pin && (*pin == PinState::AlwaysLocal);
            if (shouldDownload) {
                type = ItemTypeVirtualFileDownload;
            } else {
                type = ItemTypeVirtualFile;
            }
        }
    }

    return LocalInfo(path, type);
}

bool VfsXAttr::setPinState(const QString &folderPath, PinState state)
{
    qCDebug(lcVfsXAttr()) << folderPath << state;
    auto stateStr = Utility::enumToDisplayName(state);
    auto res = addPlaceholderAttribute(folderPath, pinstateXAttrName, stateStr.toUtf8());
    if (!res) {
        qCDebug(lcVfsXAttr()) << "Failed to set pin state";
        return false;
    }
    return true;
}

Optional<PinState> VfsXAttr::pinState(const QString &folderPath)
{
    qCDebug(lcVfsXAttr()) << folderPath;

    PlaceHolderAttribs attribs = placeHolderAttributes(folderPath);

    PinState pState{PinState::Unspecified};
    if (attribs.validOwner()) {
        const QString pin = QString::fromUtf8(attribs.pinState());

        if (pin == Utility::enumToDisplayName(PinState::AlwaysLocal)) {
            pState = PinState::AlwaysLocal;
        } else if (pin == Utility::enumToDisplayName(PinState::Excluded)) {
            pState = PinState::Excluded;
        } else if (pin.isEmpty() || pin == Utility::enumToDisplayName(PinState::Inherited)) {
            pState = PinState::Inherited;
        } else if (pin == Utility::enumToDisplayName(PinState::OnlineOnly)) {
            pState = PinState::OnlineOnly;
        }
    }

    return pState;
}

Vfs::AvailabilityResult VfsXAttr::availability(const QString &folderPath)
{
    qCDebug(lcVfsXAttr()) << folderPath;

    const auto basePinState = pinState(folderPath);

    if (basePinState) {
        switch (*basePinState) {
        case OCC::PinState::AlwaysLocal:
            return VfsItemAvailability::AlwaysLocal;
            break;
        case OCC::PinState::Inherited:
            break;
        case OCC::PinState::OnlineOnly:
            return VfsItemAvailability::OnlineOnly;
            break;
        case OCC::PinState::Unspecified:
            break;
        case OCC::PinState::Excluded:
            break;
        };
        return VfsItemAvailability::Mixed;
    } else {
        return AvailabilityError::NoSuchItem;
    }
}

void VfsXAttr::fileStatusChanged(const QString& systemFileName, SyncFileStatus fileStatus)
{
    if (fileStatus.tag() == SyncFileStatus::StatusExcluded) {
        setPinState(systemFileName, PinState::Excluded);
        return;
    }

    qCDebug(lcVfsXAttr()) << systemFileName << fileStatus;
}

} // namespace OCC
