/*
 * SPDX-FileCopyrightText: 2021 Nextcloud GmbH and Nextcloud contributors
 * SPDX-FileCopyrightText: 2025 OpenCloud GmbH and OpenCloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vfs_xattr.h"

#include "syncfileitem.h"
#include "filesystem.h"
#include "common/syncjournaldb.h"
#include "xattrwrapper.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcVfsXAttr, "sync.vfs.xattr", QtInfoMsg)

namespace xattr {
// using namespace XAttrWrapper;
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



OCC::Result<OCC::Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::updateMetadata(const SyncFileItem &syncItem, const QString &filePath, const QString &replacesFile)
{
    const auto localPath = QDir::toNativeSeparators(filePath);
    const auto replacesPath = QDir::toNativeSeparators(replacesFile);

    qCDebug(lcVfsXAttr()) << localPath;

    if (syncItem._type == ItemTypeVirtualFileDehydration) {
        // FIXME: Error handling
        dehydratePlaceholder(syncItem);
    } else {
        XAttrWrapper::PlaceHolderAttribs attribs = XAttrWrapper::placeHolderAttributes(localPath);

        if (attribs.itsMe()) { // checks if there are placeholder Attribs at all
            FileSystem::setModTime(localPath, syncItem._modtime);

            XAttrWrapper::addPlaceholderAttribute(localPath, "user.openvfs.fsize", QByteArray::number(syncItem._size));
            XAttrWrapper::addPlaceholderAttribute(localPath, "user.openvfs.state", "dehydrated");
            XAttrWrapper::addPlaceholderAttribute(localPath, "user.openvfs.fileid", syncItem._fileId);
            XAttrWrapper::addPlaceholderAttribute(localPath, "user.openvfs.etag", syncItem._etag.toUtf8());

        } else {
            // FIXME use fileItem as parameter
            return convertToPlaceholder(localPath, syncItem._modtime, syncItem._size, syncItem._fileId, replacesPath);
        }
    }

    return {OCC::Vfs::ConvertToPlaceholderResult::Ok};
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

    file.write(" ");
    file.close();

    /*
     * Only write the state and the executor, the rest is added in the updateMetadata() method
    */
    XAttrWrapper::addPlaceholderAttribute(path, "user.openvfs.state", "dehydrated");
    return {};
}

OCC::Result<void, QString> VfsXAttr::dehydratePlaceholder(const SyncFileItem &item)
{
    /*
     * const auto path = QDir::toNativeSeparators(params().filesystemPath + item.localName());
     *
     * QFile file(path);
     *
     * if (!file.remove()) {
     *   return QStringLiteral("Couldn't remove the original file to dehydrate");
     * }
     */
    auto r = createPlaceholder(item);
    if (!r) {
        return r;
    }

    // Ensure the pin state isn't contradictory
    const auto pin = pinState(item.localName());
    if (pin && *pin == PinState::AlwaysLocal) {
        setPinState(item._renameTarget, PinState::Unspecified);
    }
    return {};
}

OCC::Result<Vfs::ConvertToPlaceholderResult, QString> VfsXAttr::convertToPlaceholder(
        const QString &path, time_t modtime, qint64 size, const QByteArray &fileId, const QString &replacesPath)
{
    Q_UNUSED(modtime)
    Q_UNUSED(size)
    Q_UNUSED(fileId)
    Q_UNUSED(replacesPath)

    // Nothing necessary - no idea why, taken from previews...
    qCDebug(lcVfsXAttr()) << "empty function returning ok, DOUBLECHECK" << path ;
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
    return fi.exists() &&
            XAttrWrapper::hasPlaceholderAttributes(filePath);
}

LocalInfo VfsXAttr::statTypeVirtualFile(const std::filesystem::directory_entry &path, ItemType type)
{
    if (type == ItemTypeFile) {
        const QString p = QString::fromUtf8(path.path().c_str()); //FIXME?
        qCDebug(lcVfsXAttr()) << p;

        if (XAttrWrapper::hasPlaceholderAttributes(p)) {
            // const auto shouldDownload = pin && (*pin == PinState::AlwaysLocal);
            bool shouldDownload{false};
            if (shouldDownload) {
                type = ItemTypeVirtualFileDownload;
            } else {
                type = ItemTypeVirtualFile;
            }
        } else {
            const auto shouldDehydrate = false; // pin && (*pin == PinState::OnlineOnly);
            if (shouldDehydrate) {
                type = ItemTypeVirtualFileDehydration;
            }
        }
    }

    return LocalInfo(path, type);
}

bool VfsXAttr::setPinState(const QString &folderPath, PinState state)
{
    qCDebug(lcVfsXAttr()) << folderPath << state;
    auto stateStr = Utility::enumToDisplayName(state);
    auto res = XAttrWrapper::addPlaceholderAttribute(folderPath, "user.openvfs.pinstate", stateStr.toUtf8());
    if (res) {
        qCDebug(lcVfsXAttr()) << "Failed to set pin state";
        return false;
    }
    return true;
}

Optional<PinState> VfsXAttr::pinState(const QString &folderPath)
{
    qCDebug(lcVfsXAttr()) << folderPath;

    XAttrWrapper::PlaceHolderAttribs attribs = XAttrWrapper::placeHolderAttributes(folderPath);

    const QString pin = QString::fromUtf8(attribs.pinState());
    PinState pState;
    if (pin == Utility::enumToDisplayName(PinState::AlwaysLocal)) {
        pState = PinState::AlwaysLocal;
    } else if (pin == Utility::enumToDisplayName(PinState::Excluded)) {
        pState = PinState::Excluded;
    } else if (pin == Utility::enumToDisplayName(PinState::Inherited)) {
        pState = PinState::Inherited;
    } else if (pin == Utility::enumToDisplayName(PinState::OnlineOnly)) {
        pState = PinState::OnlineOnly;
    } else if (pin == Utility::enumToDisplayName(PinState::Unspecified)) {
        pState = PinState::Unspecified;
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
    qCDebug(lcVfsXAttr()) << systemFileName << fileStatus;

    if (fileStatus.tag() == SyncFileStatus::StatusExcluded) {
            setPinState(systemFileName, PinState::Excluded);
        }
}

} // namespace OCC
