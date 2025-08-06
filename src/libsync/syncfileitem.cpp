/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "syncfileitem.h"
#include "common/syncjournalfilerecord.h"
#include "common/utility.h"
#include "filesystem.h"

#include <QCoreApplication>

#include <bitset>

namespace OCC {

Q_LOGGING_CATEGORY(lcFileItem, "sync.fileitem", QtInfoMsg)

SyncFileItemPtr SyncFileItem::fromSyncJournalFileRecord(const SyncJournalFileRecord &rec)
{
    auto item = SyncFileItemPtr::create();
    item->_localName = rec.path();
    item->_inode = rec.inode();
    item->_modtime = rec.modtime();
    item->_type = rec.type();
    item->_etag = rec.etag();
    item->_fileId = rec.fileId();
    item->_size = rec.size();
    item->_remotePerm = rec.remotePerm();
    item->_serverHasIgnoredFiles = rec.serverHasIgnoredFiles();
    item->_checksumHeader = rec.checksumHeader();
    return item;
}

SyncInstruction SyncFileItem::instruction() const
{
    return _instruction;
}

void SyncFileItem::setInstruction(SyncInstruction instruction)
{
    // only one instruction is allowed
    // with c++23 this can be made a static_assert
    Q_ASSERT(std::bitset<sizeof(SyncFileItem)>(instruction).count() == 1);
    _instruction = instruction;
}

template <>
QString Utility::enumToDisplayName(SyncFileItem::Status s)
{
    switch (s) {
    case SyncFileItem::NoStatus:
        return QCoreApplication::translate("SyncFileItem::Status", "Undefined");
    case OCC::SyncFileItem::FatalError:
        return QCoreApplication::translate("SyncFileItem::Status", "Fatal Error");
    case OCC::SyncFileItem::NormalError:
        return QCoreApplication::translate("SyncFileItem::Status", "Error");
    case OCC::SyncFileItem::SoftError:
        return QCoreApplication::translate("SyncFileItem::Status", "Info");
    case OCC::SyncFileItem::Success:
        return QCoreApplication::translate("SyncFileItem::Status", "Success");
    case OCC::SyncFileItem::Conflict:
        return QCoreApplication::translate("SyncFileItem::Status", "Conflict");
    case OCC::SyncFileItem::FileIgnored:
        return QCoreApplication::translate("SyncFileItem::Status", "File Ignored");
    case OCC::SyncFileItem::Restoration:
        return QCoreApplication::translate("SyncFileItem::Status", "Restored");
    case OCC::SyncFileItem::DetailError:
        return QCoreApplication::translate("SyncFileItem::Status", "Error");
    case OCC::SyncFileItem::BlacklistedError:
        return QCoreApplication::translate("SyncFileItem::Status", "Blacklisted");
    case OCC::SyncFileItem::Excluded:
        return QCoreApplication::translate("SyncFileItem::Status", "Excluded");
    case OCC::SyncFileItem::Message:
        return QCoreApplication::translate("SyncFileItem::Status", "Message");
    case OCC::SyncFileItem::FilenameReserved:
        return QCoreApplication::translate("SyncFileItem::Status", "Filename Reserved");
    case OCC::SyncFileItem::StatusCount:
        Q_UNREACHABLE();
    }
    Q_UNREACHABLE();
}
}

QDebug operator<<(QDebug debug, const OCC::SyncFileItem *item)
{
    if (!item) {
        debug << "OCC::SyncFileItem(0x0)";
    } else {
        QDebugStateSaver saver(debug);
        debug.setAutoInsertSpaces(false);
        debug << "OCC::SyncFileItem(file=" << item->localName();
        if (!item->_renameTarget.isEmpty()) {
            debug << ", destination=" << item->destination();
        }
        debug << ", type=" << item->_type << ", instruction=" << item->instruction() << ", status=" << item->_status << ")";
    }
    return debug;
}
