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
    : d([] {
        static auto nullData = QExplicitlySharedDataPointer{new SyncJournalFileRecordData{}};
        return nullData;
    }())
{
}
SyncJournalFileRecord::SyncJournalFileRecord(const QString &path, ItemType type)
    : d(new SyncJournalFileRecordData)
{
    d->_path = path;
    d->_type = type;
}

bool SyncJournalFileRecord::validateRecord()
{
    Q_ASSERT(d != SyncJournalFileRecord().d);
    Q_ASSERT(!remotePerm().isNull());
    Q_ASSERT(inode() != 0);
    Q_ASSERT(modtime() != 0);
    Q_ASSERT(!fileId().isEmpty());
    Q_ASSERT(type() != ItemTypeUnsupported);
    return true;
}

SyncJournalFileRecord::SyncJournalFileRecord(const QString &error)
    : d(new SyncJournalFileRecordData)
{
    d->_error = error;
}

SyncJournalFileRecord::~SyncJournalFileRecord() = default;
SyncJournalFileRecord::SyncJournalFileRecord(const SyncJournalFileRecord &other) = default;
SyncJournalFileRecord &SyncJournalFileRecord::operator=(const SyncJournalFileRecord &other) = default;

QByteArray SyncJournalFileRecord::query()
{
    return QByteArrayLiteral("SELECT path, inode, modtime, type, md5, fileid, remotePerm, filesize,"
                             " ignoredChildrenRemote, contentchecksumtype.name || ':' || contentChecksum,"
                             " hasDirtyPlaceholder"
                             " FROM metadata"
                             " LEFT JOIN checksumtype as contentchecksumtype ON metadata.contentChecksumTypeId == contentchecksumtype.id ");
}

SyncJournalFileRecord SyncJournalFileRecord::fromSqlQuery(OCC::SqlQuery &query)
{
    // keep in sync with  query
    SyncJournalFileRecord rec(QString::fromUtf8(query.baValue(0)), static_cast<ItemType>(query.intValue(3)));
    rec.d->_inode = query.int64Value(1);
    rec.d->_modtime = query.int64Value(2);
    rec.d->_etag = QString::fromUtf8(query.baValue(4));
    rec.d->_fileId = query.baValue(5);
    rec.d->_remotePerm = OCC::RemotePermissions::fromDbValue(query.baValue(6));
    rec.d->_fileSize = query.int64Value(7);
    rec.d->_serverHasIgnoredFiles = (query.intValue(8) > 0);
    rec.d->_checksumHeader = query.baValue(9);
    rec.d->_hasDirtyPlaceholder = query.intValue(10);
    qCDebug(lcSyncJournalFileRecord) << u"Restored from db:" << rec;
    Q_ASSERT(rec.validateRecord());
    return rec;
}

SyncJournalFileRecord SyncJournalFileRecord::fromSyncFileItem(const SyncFileItem &syncFile)
{
    SyncJournalFileRecord rec(syncFile.destination(), syncFile._type);
    rec.d->_modtime = syncFile._modtime;

    // Some types should never be written to the database when propagation completes
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
    Q_ASSERT(rec.validateRecord());

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
    debug.nospace() << u"SyncJournalFileRecord(";
    if (record.hasError()) {
        debug << u"Error: " << record.error();
    } else {
        debug << u"path: " << record.path() << u", inode: " << record.inode() << u", modtime: " << record.modtime() << u", type: " << record.type()
              << u", etag: " << record.etag() << u", fileId: " << record.fileId() << u", remotePerm: " << record.remotePerm().toString() << u", size: "
              << record.size() << u", checksum: " << record.checksumHeader() << u", serverHasIgnoredFiles: " << record.serverHasIgnoredFiles()
              << u", hasDirtyPlaceholder: " << record.hasDirtyPlaceholder();
    }
    return debug << u")";
}

bool operator==(const SyncJournalFileRecord &lhs, const SyncJournalFileRecord &rhs)
{
    return lhs.path() == rhs.path() && lhs.inode() == rhs.inode() && lhs.modtime() == rhs.modtime() && lhs.type() == rhs.type() && lhs.etag() == rhs.etag()
        && lhs.fileId() == rhs.fileId() && lhs.size() == rhs.size() && lhs.remotePerm() == rhs.remotePerm()
        && lhs.serverHasIgnoredFiles() == rhs.serverHasIgnoredFiles() && lhs.checksumHeader() == rhs.checksumHeader();
}
