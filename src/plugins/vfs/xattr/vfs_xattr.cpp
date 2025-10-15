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
const QString ownerXAttrName = QStringLiteral("user.openvfs.owner");
const QString etagXAttrName = QStringLiteral("user.openvfs.etag");
const QString fileidXAttrName = QStringLiteral("user.openvfs.fileid");
const QString modtimeXAttrName = QStringLiteral("user.openvfs.modtime");
const QString fileSizeXAttrName = QStringLiteral("user.openvfs.fsize");
const QString actionXAttrName = QStringLiteral("user.openvfs.action");
const QString stateXAttrName = QStringLiteral("user.openvfs.state");
const QString pinstateXAttrName = QStringLiteral("user.openvfs.pinstate");

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

bool set(const QString &path, const QString &name, const QString &value)
{
    const auto returnCode = setxattr(path.toUtf8().constData(), name.toUtf8().constData(),
                                     value.toUtf8().constData(), value.toUtf8().size()+1, 0);
    return returnCode == 0;
}

bool remove(const QString &path, const QString &name)
{
    const auto returnCode = removexattr(path.toUtf8().constData(), name.toUtf8().constData());
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

QString VfsXAttr::xattrOwnerString() const
{
    auto s = QByteArray(APPLICATION_EXECUTABLE);
    s.append(":");
    s.append(_setupParams->account->uuid().toByteArray(QUuid::WithoutBraces));
    return QString::fromUtf8(s);
}

PlaceHolderAttribs VfsXAttr::placeHolderAttributes(const QString& path)
{
    PlaceHolderAttribs attribs;

    // lambda to handle the Optional return val of xattrGet
    auto xattr = [](const QString& p, const QString& name) {
        const auto value = xattr::get(p.toUtf8(), name.toUtf8());
        if (value) {
            return QString::fromUtf8(*value);
        } else {
            return QString();
        }
    };

    attribs._owner = xattr(path, ownerXAttrName);
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

    attribs._etag = xattr(path, etagXAttrName);
    attribs._fileId = xattr(path, fileidXAttrName);

    const QString tt = xattr(path, modtimeXAttrName);
    attribs._modtime = tt.toLongLong();

    attribs._action = xattr(path, actionXAttrName);
    attribs._size = xattr(path, fileSizeXAttrName).toLongLong();
    attribs._state = xattr(path, stateXAttrName);
    attribs._pinState = xattr(path, pinstateXAttrName);

    return attribs;
}

OCC::Result<void, QString> VfsXAttr::addPlaceholderAttribute(const QString &path, const QString& name, const QString& value)
{
    const PlaceHolderAttribs attribs = placeHolderAttributes(path);

    if (! attribs.validOwner()) {
        return QStringLiteral("Can not overwrite attributes - not our placeholder");
    }

    // FIXME: this always sets the name, can be optimized
    auto success = xattr::set(path, ownerXAttrName, xattrOwnerString());
    if (!success) {
        return QStringLiteral("Failed to set the extended attribute for owner");
    }

    if (!name.isEmpty()) {
        auto success = xattr::set(path, name, value);
        if (!success) {
            return QStringLiteral("Failed to set the extended attribute");
        }
    }

    return {};
}

// removes the state and owner which makes the file a hydrated file
OCC::Result<void, QString> VfsXAttr::removePlaceHolderAttributes(const QString& folderPath)
{
    bool ok{true};
    ok = xattr::remove(folderPath, stateXAttrName);
    ok = ok && xattr::remove(folderPath, ownerXAttrName);
    // ok = ok && xattr::remove(folderPath, pinstateXAttrName);

    if (ok)
        return {};
    else
        return QStringLiteral("Failed to remove xattr");
}

OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile)
{
    const auto localPath = QDir::toNativeSeparators(filePath);
    const auto replacesPath = QDir::toNativeSeparators(replacesFile);

    qCDebug(lcVfsXAttr) << localPath;

    PlaceHolderAttribs attribs = placeHolderAttributes(localPath);
    OCC::Vfs::ConvertToPlaceholderResult res{OCC::Vfs::ConvertToPlaceholderResult::Ok};

    if (attribs.validOwner() && attribs.state().isEmpty()) { // No status
        // There is no state, so it is a normal, hydrated file
    }

    if (syncItem._type == ItemTypeVirtualFileDehydration) { //
        addPlaceholderAttribute(localPath, actionXAttrName, QStringLiteral("dehydrate"));
        // FIXME: Error handling
        auto r = createPlaceholder(syncItem);
        if (!r) {
            res = OCC::Vfs::ConvertToPlaceholderResult::Locked;
        }

    } else if (syncItem._type == ItemTypeVirtualFileDownload) {
        addPlaceholderAttribute(localPath, actionXAttrName, QStringLiteral("hydrate"));
        qCDebug(lcVfsXAttr) << "FIXME: Do we need to download here?";
        // start to download? FIXME
    } else if (syncItem._type == ItemTypeVirtualFile) {
            FileSystem::setModTime(localPath, syncItem._modtime);

            // FIXME only write attribs if they're different, and/or all together
            addPlaceholderAttribute(localPath, fileSizeXAttrName, QString::number(syncItem._size));
            addPlaceholderAttribute(localPath, stateXAttrName, QStringLiteral("virtual"));
            addPlaceholderAttribute(localPath, fileidXAttrName, QString::fromUtf8(syncItem._fileId));
            addPlaceholderAttribute(localPath, etagXAttrName, syncItem._etag);
    } else {
            // FIXME anything to check for other types?
        qCDebug(lcVfsXAttr) << "Unexpected syncItem Type";
    }

    // FIXME Errorhandling
    return res;
}

void VfsXAttr::slotHydrateJobFinished()
{
    HydrationJob *hydration = qobject_cast<HydrationJob*>(sender());

    const QString targetPath = hydration ->targetFileName();
    Q_ASSERT(!targetPath.isEmpty());

    qCInfo(lcVfsXAttr) << u"Hydration Job finished for" << targetPath;

    if (QFileInfo::exists(targetPath)) {
        auto item = OCC::SyncFileItem::fromSyncJournalFileRecord(hydration->record());
        // the file is now downloaded
        item->_type = ItemTypeFile;
        FileSystem::getInode(targetPath, &item->_inode);


        // Update the client sync journal database
        const auto result = this->params().journal->setFileRecord(SyncJournalFileRecord::fromSyncFileItem(*item));
        if (!result) {
            qCWarning(lcVfsXAttr) << u"Error when setting the file record to the database" << result.error();
        } else {
            qCInfo(lcVfsXAttr) << u"Hydration succeeded" << targetPath;
        }

        // set the xattrs
        // the file is not virtual any more, remove the xattrs.
        removePlaceHolderAttributes(targetPath);

        time_t modtime = item->_modtime;
        qCInfo(lcVfsXAttr) << u"Setting hydrated file's modtime to" << modtime;
        FileSystem::setModTime(targetPath, modtime);
    } else {
        qCWarning(lcVfsXAttr) << u"Hydration succeeded but the file appears to be moved" << targetPath;
    }

    hydration->deleteLater();
    this->_hydrationJobs.remove(hydration->fileId());
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

    xattr::remove(path, actionXAttrName); // remove the action xattr

    // FIXME only write attribs if they're different, and/or all together
    addPlaceholderAttribute(path, fileSizeXAttrName, QString::number(item._size));
    addPlaceholderAttribute(path, stateXAttrName, QStringLiteral("virtual"));
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
    addPlaceholderAttribute(targetPath, actionXAttrName, QStringLiteral("hydrate"));

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
    qCDebug(lcVfsXAttr()) << "returns false by default DOUBLECHECK";
    // return true if file exists
    const auto path = item.localName();
    QFileInfo fi{path};

    // FIXME: Unsure about this implementation
    if (fi.exists()) {
        return true;
    }
    return false;
}

bool VfsXAttr::isDehydratedPlaceholder(const QString &filePath)
{
    const auto fi = QFileInfo(filePath);
    if (fi.exists()) {
        const auto attribs = placeHolderAttributes(filePath);
        return (attribs.validOwner() &&
                attribs.state() == QStringLiteral("virtual"));
    }
    return false;
}

LocalInfo VfsXAttr::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    const QString p = FileSystem::fromFilesystemPath(path.path());
    if (type == ItemTypeFile) {

        auto attribs = placeHolderAttributes(p);
        if (attribs.validOwner()) {
            if (attribs.state() == QStringLiteral("virtual")) {
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
    }
    qCDebug(lcVfsXAttr()) << p << Utility::enumToString(type);

    return LocalInfo(path, type);
}

bool VfsXAttr::setPinState(const QString &folderPath, PinState state)
{
    qCDebug(lcVfsXAttr()) << folderPath << state;

    if (state == PinState::AlwaysLocal || state == PinState::OnlineOnly || state == PinState::Excluded) {
        auto stateStr = pinStateToString(state);
        addPlaceholderAttribute(folderPath, pinstateXAttrName, stateStr);
    } else {
        qCDebug(lcVfsXAttr) << "Do not set Pinstate" << pinStateToString(state) << ", remove pinstate xattr";
        xattr::remove(folderPath, pinstateXAttrName);
    }
    return true;
}

Optional<PinState> VfsXAttr::pinState(const QString &folderPath)
{
    qCDebug(lcVfsXAttr()) << folderPath;

    PlaceHolderAttribs attribs = placeHolderAttributes(folderPath);

    PinState pState{PinState::Unspecified}; // the default if no owner or state is set
    if (attribs.validOwner()) {
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

QString VfsXAttr::pinStateToString(PinState pState) const
{
    QString re;
    switch (pState) {
    case OCC::PinState::AlwaysLocal:
        re = QStringLiteral("alwayslocal");
        break;
    case OCC::PinState::Inherited:
        re = QStringLiteral("interited");
        break;
    case OCC::PinState::OnlineOnly:
        re = QStringLiteral("onlineonly");
        break;
    case OCC::PinState::Unspecified:
        re = QStringLiteral("unspecified");
        break;
    case OCC::PinState::Excluded:
        re = QStringLiteral("excluded");
        break;
    };
    return re;
}

PinState VfsXAttr::stringToPinState(const QString& str) const
{
    PinState p{PinState::Unspecified};
    if (str.isEmpty() || str == QStringLiteral("unspecified")) {
        p = PinState::Unspecified;
    } else if( str == QStringLiteral("alwayslocal")) {
        p = PinState::AlwaysLocal;
    } else if( str == QStringLiteral("inherited")) {
        p = PinState::Inherited;
    } else if( str == QStringLiteral("unspecified")) {
        p = PinState::Unspecified;
    } else if( str == QStringLiteral("excluded")) {
        p = PinState::Excluded;
    }
    return p;
}

} // namespace OCC
