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

#ifndef SYNCJOURNALFILERECORD_H
#define SYNCJOURNALFILERECORD_H

#include "libsync/common/utility.h"
#include "libsync/csync.h"
#include "libsync/opencloudsynclib.h"
#include "remotepermissions.h"

namespace OCC {
class SqlQuery;

class SyncFileItem;
class SyncJournalFileRecordData;

/**
 * @brief The SyncJournalFileRecord class
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT SyncJournalFileRecord
{
public:
    SyncJournalFileRecord();
    SyncJournalFileRecord(const QString &error);
    ~SyncJournalFileRecord();
    SyncJournalFileRecord(const SyncJournalFileRecord &other);
    SyncJournalFileRecord &operator=(const SyncJournalFileRecord &other);
    void swap(SyncJournalFileRecord &other) noexcept { d.swap(other.d); }
    QT_MOVE_ASSIGNMENT_OPERATOR_IMPL_VIA_MOVE_AND_SWAP(SyncJournalFileRecord);

    static QByteArray query();
    static SyncJournalFileRecord fromSqlQuery(SqlQuery &query);
    static SyncJournalFileRecord fromSyncFileItem(const SyncFileItem &syncFile);
    bool isValid() const;

    QDateTime modDateTime() const;

    bool isDirectory() const;
    bool isFile() const;
    bool isVirtualFile() const;

    QString path() const;
    QString name() const;
    QString etag() const;
    QByteArray fileId() const;
    RemotePermissions remotePerm() const;
    QByteArray checksumHeader() const;
    time_t modtime() const;
    int64_t size() const;
    uint64_t inode() const;
    ItemType type() const;

    bool hasDirtyPlaceholder() const;
    void setDirtyPlaceholder(bool b);
    bool serverHasIgnoredFiles() const;

    QString error() const;
    bool hasError() const;

private:
    // constructor for the non null object
    SyncJournalFileRecord(const QString &path, ItemType type);
    bool validateRecord();

    QExplicitlySharedDataPointer<SyncJournalFileRecordData> d;
};

QDebug OPENCLOUD_SYNC_EXPORT operator<<(QDebug debug, const SyncJournalFileRecord &record);

class OPENCLOUD_SYNC_EXPORT SyncJournalErrorBlacklistRecord
{
    Q_GADGET
public:
    enum class Category {
        /// Normal errors have no special behavior
        Normal = 0,
        /// These get a special summary message
        InsufficientRemoteStorage,
        LocalSoftError
    };
    Q_ENUM(Category)

    SyncJournalErrorBlacklistRecord()
        : _retryCount(0)
        , _errorCategory(Category::Normal)
        , _lastTryModtime(0)
        , _lastTryTime(0)
        , _ignoreDuration(0)
    {
    }

    /// The number of times the operation was unsuccessful so far.
    int _retryCount;

    /// The last error string.
    QString _errorString;
    /// The error category. Sometimes used for special actions.
    Category _errorCategory;

    qint64 _lastTryModtime;
    QByteArray _lastTryEtag;

    /// The last time the operation was attempted (in s since epoch).
    qint64 _lastTryTime;

    /// The number of seconds the file shall be ignored.
    qint64 _ignoreDuration;

    QString _file;
    QString _renameTarget;

    /// The last X-Request-ID of the request that failled
    QByteArray _requestId;

    bool isValid() const;
};

/** Represents a conflict in the conflicts table.
 *
 * In the following the "conflict file" is the file that has the conflict
 * tag in the filename, and the base file is the file that it's a conflict for.
 * So if "a/foo.txt" is the base file, its conflict file could be
 * "a/foo (conflicted copy 1234).txt".
 */
class OPENCLOUD_SYNC_EXPORT ConflictRecord
{
public:
    /** Path to the file with the conflict tag in the name
     *
     * The path is sync-folder relative.
     */
    QByteArray path;

    /// File id of the base file
    QByteArray baseFileId;

    /** Modtime of the base file
     *
     * may not be available and be -1
     */
    qint64 baseModtime = -1;

    /** Etag of the base file
     *
     * may not be available and empty
     */
    QByteArray baseEtag;

    /**
     * The path of the original file at the time the conflict was created
     *
     * Note that in nearly all cases one should query the db by baseFileId and
     * thus retrieve the *current* base path instead!
     *
     * maybe be empty if not available
     */
    QByteArray initialBasePath;


    bool isValid() const { return !path.isEmpty(); }
};
}


OPENCLOUD_SYNC_EXPORT bool operator==(const OCC::SyncJournalFileRecord &, const OCC::SyncJournalFileRecord &);
Q_DECLARE_SHARED(OCC::SyncJournalFileRecord);
#endif // SYNCJOURNALFILERECORD_H
