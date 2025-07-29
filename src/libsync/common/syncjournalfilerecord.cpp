/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libsync/common/syncjournalfilerecord.h"

#include "libsync/common/utility.h"
#include "libsync/filesystem.h"
#include "libsync/syncfileitem.h"

Q_LOGGING_CATEGORY(lcSyncJournalFileRecord, "sync.syncjournalfilerecord", QtInfoMsg)
using namespace Qt::Literals::StringLiterals;

using namespace OCC;


class OCC::SyncJournalFileRecordData : public QSharedData
{
public:
    SyncJournalFileRecordData() = default;
    ~SyncJournalFileRecordData() = default;

    QString _path;
    quint64 _inode = 0;
    qint64 _modtime = 0;
    ItemType _type = ItemTypeUnsupported;
    QString _etag;
    QByteArray _fileId;
    qint64 _fileSize = 0;
    RemotePermissions _remotePerm;
    bool _serverHasIgnoredFiles = false;
    bool _hasDirtyPlaceholder = false;
    QByteArray _checksumHeader;

    QString _error;
};

bool SyncJournalErrorBlacklistRecord::isValid() const
{
    return !_file.isEmpty() && (!_lastTryEtag.isEmpty() || _lastTryModtime != 0) && _lastTryTime > 0;
}

SyncJournalFileRecord::SyncJournalFileRecord()
    : d(new SyncJournalFileRecordData)
{
}

SyncJournalFileRecord::SyncJournalFileRecord(const QString &error)
    : d(new SyncJournalFileRecordData)
{
    d->_error = error;
}

SyncJournalFileRecord::~SyncJournalFileRecord() = default;
SyncJournalFileRecord::SyncJournalFileRecord(const SyncJournalFileRecord &other) = default;
SyncJournalFileRecord &SyncJournalFileRecord::operator=(const SyncJournalFileRecord &other) = default;

SyncJournalFileRecord SyncJournalFileRecord::fromSqlQuery(OCC::SqlQuery &query)
{
    SyncJournalFileRecord rec;
    rec.d->_path = QString::fromUtf8(query.baValue(0)); // historically utf8
    rec.d->_inode = query.int64Value(1);
    rec.d->_modtime = query.int64Value(2);
    rec.d->_type = static_cast<ItemType>(query.intValue(3));
    rec.d->_etag = QString::fromUtf8(query.baValue(4));
    rec.d->_fileId = query.baValue(5);
    rec.d->_remotePerm = OCC::RemotePermissions::fromDbValue(query.baValue(6));
    rec.d->_fileSize = query.int64Value(7);
    rec.d->_serverHasIgnoredFiles = (query.intValue(8) > 0);
    rec.d->_checksumHeader = query.baValue(9);
    return rec;
}
SyncJournalFileRecord SyncJournalFileRecord::fromSyncFileItem(const SyncFileItem &syncFile)
{
    SyncJournalFileRecord rec;
    rec.d->_path = syncFile.destination();
    rec.d->_modtime = syncFile._modtime;

    // Some types should never be written to the database when propagation completes
    rec.d->_type = syncFile._type;
    if (rec.d->_type == ItemTypeVirtualFileDownload)
        rec.d->_type = ItemTypeFile;
    if (rec.d->_type == ItemTypeVirtualFileDehydration)
        rec.d->_type = ItemTypeVirtualFile;

    rec.d->_etag = syncFile._etag;
    rec.d->_fileId = syncFile._fileId;
    rec.d->_fileSize = syncFile._size;
    rec.d->_inode = syncFile._inode;
    rec.d->_remotePerm = syncFile._remotePerm;
    rec.d->_serverHasIgnoredFiles = syncFile._serverHasIgnoredFiles;
    rec.d->_checksumHeader = syncFile._checksumHeader;
    Q_ASSERT(!rec.remotePerm().isNull());
    Q_ASSERT(rec.inode() != 0);
    Q_ASSERT(rec.modtime() != 0);
    Q_ASSERT(!rec.fileId().isEmpty());
    Q_ASSERT(rec.type() != ItemTypeUnsupported);

    return rec;
}

bool SyncJournalFileRecord::isValid() const
{
    return !hasError() && !d->_path.isEmpty();
}

QDateTime SyncJournalFileRecord::modDateTime() const
{
    return Utility::qDateTimeFromTime_t(modtime());
}

bool SyncJournalFileRecord::isDirectory() const
{
    return d->_type == ItemTypeDirectory;
}

bool SyncJournalFileRecord::isFile() const
{
    return d->_type == ItemTypeFile || d->_type == ItemTypeVirtualFileDehydration;
}

bool SyncJournalFileRecord::isVirtualFile() const
{
    return d->_type == ItemTypeVirtualFile || d->_type == ItemTypeVirtualFileDownload;
}
QString SyncJournalFileRecord::path() const
{
    return d->_path;
}

QString SyncJournalFileRecord::name() const
{
    return path().mid(path().lastIndexOf('/'_L1) + 1);
}
QString SyncJournalFileRecord::etag() const
{
    return d->_etag;
}
QByteArray SyncJournalFileRecord::fileId() const
{
    return d->_fileId;
}
RemotePermissions SyncJournalFileRecord::remotePerm() const
{
    return d->_remotePerm;
}

QByteArray SyncJournalFileRecord::checksumHeader() const
{
    return d->_checksumHeader;
}
time_t SyncJournalFileRecord::modtime() const
{
    return d->_modtime;
}
int64_t SyncJournalFileRecord::size() const
{
    return d->_fileSize;
}

uint64_t SyncJournalFileRecord::inode() const
{
    return d->_inode;
}

ItemType SyncJournalFileRecord::type() const
{
    return d->_type;
}

bool SyncJournalFileRecord::hasDirtyPlaceholder() const
{
    return d->_hasDirtyPlaceholder;
}

void SyncJournalFileRecord::setDirtyPlaceholder(bool b)
{
    d.detach();
    d->_hasDirtyPlaceholder = b;
}

bool SyncJournalFileRecord::serverHasIgnoredFiles() const
{
    return d->_serverHasIgnoredFiles;
}

QString SyncJournalFileRecord::error() const
{
    return d->_error;
}

bool SyncJournalFileRecord::hasError() const
{
    return !d->_error.isEmpty();
}
QDebug OCC::operator<<(QDebug debug, const SyncJournalFileRecord &record)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "SyncJournalFileRecord(";
    if (record.hasError()) {
        debug << "Error: " << record.error();
    } else {
        debug << "path: " << record.path() << ", inode: " << record.inode() << ", modtime: " << record.modtime() << ", type: " << record.type()
              << ", etag: " << record.etag() << ", fileId: " << record.fileId() << ", remotePerm: " << record.remotePerm().toString()
              << ", size: " << record.size() << ", checksum: " << record.checksumHeader() << ", serverHasIgnoredFiles: " << record.serverHasIgnoredFiles()
              << ", hasDirtyPlaceholder: " << record.hasDirtyPlaceholder();
    }
    return debug << ")";
}

bool operator==(const SyncJournalFileRecord &lhs, const SyncJournalFileRecord &rhs)
{
    return lhs.path() == rhs.path() && lhs.inode() == rhs.inode() && lhs.modtime() == rhs.modtime() && lhs.type() == rhs.type() && lhs.etag() == rhs.etag()
        && lhs.fileId() == rhs.fileId() && lhs.size() == rhs.size() && lhs.remotePerm() == rhs.remotePerm()
        && lhs.serverHasIgnoredFiles() == rhs.serverHasIgnoredFiles() && lhs.checksumHeader() == rhs.checksumHeader();
}
