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

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include "common/asserts.h"
#include "common/filesystembase.h"
#include "common/utility.h"

#include "ownsql.h"

#include <sqlite3.h>

#include <thread>

using namespace std::chrono_literals;

namespace {
constexpr int SQLITE_REPEAT_COUNT = 20;

}

#define SQLITE_DO(A)                                                                                                                                           \
    if (1) {                                                                                                                                                   \
        _errId = (A);                                                                                                                                          \
        if (_errId != SQLITE_OK && _errId != SQLITE_DONE && _errId != SQLITE_ROW) {                                                                            \
            _error = QString::fromUtf8(sqlite3_errmsg(_db));                                                                                                   \
        }                                                                                                                                                      \
    }

namespace OCC {

Q_LOGGING_CATEGORY(lcSql, "sync.database.sql", QtInfoMsg)

SqlDatabase::SqlDatabase()
    : _db(nullptr)
    , _errId(0)
{
}

SqlDatabase::~SqlDatabase()
{
    close();
}


bool SqlDatabase::isOpen() const
{
    return _db != nullptr;
}

bool SqlDatabase::openHelper(const QString &filename, int sqliteFlags)
{
    if (isOpen()) {
        return true;
    }

    sqliteFlags |= SQLITE_OPEN_NOMUTEX;

#ifdef Q_OS_WIN
    // allow file paths > 260
    // https://www.sqlite.org/vfs.html#standard_windows_vfses
    const char *vfs = "win32-longpath";
#else
    char *vfs = nullptr;
#endif
    SQLITE_DO(sqlite3_open_v2(FileSystem::longWinPath(filename).toUtf8().constData(), &_db, sqliteFlags, vfs));

    if (_errId != SQLITE_OK) {
        qCWarning(lcSql) << "Error:" << _error << "for" << filename;
        if (_errId == SQLITE_CANTOPEN) {
            qCWarning(lcSql) << "CANTOPEN extended errcode: " << sqlite3_extended_errcode(_db);
#if SQLITE_VERSION_NUMBER >= 3012000
            qCWarning(lcSql) << "CANTOPEN system errno: " << sqlite3_system_errno(_db);
#endif
        }
        close();
        return false;
    }

    if (!_db) {
        qCWarning(lcSql) << "Error: no database for" << filename;
        return false;
    }

    sqlite3_busy_timeout(_db, 5000);

    return true;
}

SqlDatabase::CheckDbResult SqlDatabase::checkDb()
{
    // quick_check can fail with a disk IO error when diskspace is low
    SqlQuery quick_check(*this);

    if (quick_check.prepare("PRAGMA quick_check;", /*allow_failure=*/true) != SQLITE_OK) {
        qCWarning(lcSql) << "Error preparing quick_check on database";
        _errId = quick_check.errorId();
        _error = quick_check.error();
        return CheckDbResult::CantPrepare;
    }
    if (!quick_check.exec()) {
        qCWarning(lcSql) << "Error running quick_check on database";
        _errId = quick_check.errorId();
        _error = quick_check.error();
        return CheckDbResult::CantExec;
    }

    quick_check.next();
    QString result = quick_check.stringValue(0);
    if (result != QLatin1String("ok")) {
        qCWarning(lcSql) << "quick_check returned failure:" << result;
        return CheckDbResult::NotOk;
    }

    return CheckDbResult::Ok;
}

bool SqlDatabase::openOrCreateReadWrite(const QString &filename)
{
    if (isOpen()) {
        return true;
    }

    if (!openHelper(filename, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) {
        return false;
    }

    auto checkResult = checkDb();
    if (checkResult != CheckDbResult::Ok) {
        close();
        if (checkResult == CheckDbResult::CantPrepare) {
            // When disk space is low, preparing may fail even though the db is fine.
            // Typically CANTOPEN or IOERR.
            qint64 freeSpace = Utility::freeDiskSpace(QFileInfo(filename).dir().absolutePath());
            if (freeSpace != -1 && freeSpace < 1000000) {
                qCWarning(lcSql) << "Can't prepare consistency check and disk space is low:" << freeSpace;
            } else if (_errId == SQLITE_CANTOPEN) {
                // Even when there's enough disk space, it might very well be that the
                // file is on a read-only filesystem and can't be opened because of that.
                qCWarning(lcSql) << "Can't open db to prepare consistency check, aborting";
            } else if (_errId == SQLITE_LOCKED || _errId == SQLITE_BUSY) {
                qCWarning(lcSql) << "Can't open db to prepare consistency check, the db is locked aborting" << _errId << _error;
            }
            return false;
        }

        qCCritical(lcSql) << "Consistency check failed, removing broken db" << filename;
        QFile fileToRemove(filename);
        if (!fileToRemove.remove()) {
            qCCritical(lcSql) << "Failed to remove broken db" << filename << ":" << fileToRemove.errorString();
            return false;
        }

        return openHelper(filename, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    }

    return true;
}

bool SqlDatabase::openReadOnly(const QString &filename)
{
    if (isOpen()) {
        return true;
    }

    if (!openHelper(filename, SQLITE_OPEN_READONLY)) {
        return false;
    }

    if (checkDb() != CheckDbResult::Ok) {
        qCWarning(lcSql) << "Consistency check failed in readonly mode, giving up" << filename;
        close();
        return false;
    }

    return true;
}

QString SqlDatabase::error() const
{
    const QString err(_error);
    // _error.clear();
    return err;
}

void SqlDatabase::close()
{
    if (_db) {
        // we need a copy because query is removed from a set in finish()
        const auto queries = _queries;
        for (auto *q : queries) {
            q->finish();
        }
        SQLITE_DO(sqlite3_close(_db));
        if (_errId != SQLITE_OK)
            qCWarning(lcSql) << "Closing database failed" << _error;
        _db = nullptr;
    }
}

bool SqlDatabase::transaction()
{
    if (!_db) {
        return false;
    }
    SQLITE_DO(sqlite3_exec(_db, "BEGIN", nullptr, nullptr, nullptr));
    return _errId == SQLITE_OK;
}

bool SqlDatabase::commit()
{
    if (!_db) {
        return false;
    }
    SQLITE_DO(sqlite3_exec(_db, "COMMIT", nullptr, nullptr, nullptr));
    return _errId == SQLITE_OK;
}

sqlite3 *SqlDatabase::sqliteDb()
{
    return _db;
}

/* =========================================================================================== */

SqlQuery::SqlQuery(SqlDatabase &db)
    : _sqldb(&db)
    , _db(db.sqliteDb())
{
}

SqlQuery::~SqlQuery()
{
    if (_stmt) {
        finish();
    }
}

SqlQuery::SqlQuery(const QByteArray &sql, SqlDatabase &db)
    : _sqldb(&db)
    , _db(db.sqliteDb())
{
    prepare(sql);
}

int SqlQuery::prepare(const QByteArray &sql, bool allow_failure)
{
    _sql = sql.trimmed();
    if (_stmt) {
        finish();
    }
    if (!_sql.isEmpty()) {
        for (int n = 0; n < SQLITE_REPEAT_COUNT; ++n) {
            qCDebug(lcSql) << "SQL prepare" << _sql << "Try:" << n;
            _errId = sqlite3_prepare_v2(_db, _sql.constData(), -1, &_stmt, nullptr);
            if (_errId != SQLITE_OK) {
                qCWarning(lcSql) << "SQL prepare failed" << _sql << QString::fromUtf8(sqlite3_errmsg(_db));
                if ((_errId == SQLITE_BUSY) || (_errId == SQLITE_LOCKED)) {
                    continue;
                }
            }
            break;
        }

        if (_errId != SQLITE_OK) {
            _error = QString::fromUtf8(sqlite3_errmsg(_db));
            qCWarning(lcSql) << "Sqlite prepare statement error:" << _errId << _error << "in" << _sql;
            OC_ENFORCE_X(allow_failure, "SQLITE Prepare error");
        } else {
            OC_ASSERT(_stmt);
            _sqldb->_queries.insert(this);

            if (lcSql().isDebugEnabled()) {
                _boundValues.resize(sqlite3_bind_parameter_count(_stmt));
                for (int i = 1; i <= _boundValues.size(); ++i) {
                    _boundValues[i - 1].name = QString::fromUtf8(sqlite3_bind_parameter_name(_stmt, i));
                }
            }
        }
    }
    return _errId;
}

/**
 * There is no overloads to QByteArray::startWith that takes Qt::CaseInsensitive.
 * Returns true if 'a' starts with 'b' in a case insensitive way
 */
static bool startsWithInsensitive(const QByteArray &a, const QByteArray &b)
{
    return a.size() >= b.size() && qstrnicmp(a.constData(), b.constData(), b.size()) == 0;
}

bool SqlQuery::isSelect()
{
    return startsWithInsensitive(_sql, QByteArrayLiteral("SELECT"));
}

bool SqlQuery::isPragma()
{
    return startsWithInsensitive(_sql, QByteArrayLiteral("PRAGMA"));
}

bool SqlQuery::exec()
{
    if (!_stmt) {
        qCWarning(lcSql) << "Can't exec query, statement unprepared.";
        return false;
    }
    // Don't do anything for selects, that is how we use the lib :-|
    if (!isSelect() && !isPragma()) {
        for (int n = 0; n < SQLITE_REPEAT_COUNT; ++n) {
            if (lcSql().isDebugEnabled()) {
                if (!_boundValues.isEmpty()) {
                    // Try to create a pretty printed version of the sql query
                    // the result can be inaccurate
                    QString query = QString::fromUtf8(_sql);
                    for (const auto &v : _boundValues) {
                        query.replace(query.indexOf(v.name), v.name.size(), v.value);
                    }
                    char *actualQuery = sqlite3_expanded_sql(_stmt);
                    qCDebug(lcSql) << "SQL exec: Estimated query:" << query << "Actual query:" << QString::fromUtf8(actualQuery) << "Try:" << n;
                    sqlite3_free(actualQuery);
                } else {
                    qCDebug(lcSql) << "SQL exec:" << _sql << "Try:" << n;
                }
            }
            _errId = sqlite3_step(_stmt);
            if (_errId != SQLITE_DONE && _errId != SQLITE_ROW) {
                qCWarning(lcSql) << "SQL exec failed" << _sql << QString::fromUtf8(sqlite3_errmsg(_db));
                if (_errId == SQLITE_LOCKED || _errId == SQLITE_BUSY) {
                    continue;
                }
            }
            break;
        }

        if (_errId != SQLITE_DONE && _errId != SQLITE_ROW) {
            _error = QString::fromUtf8(sqlite3_errmsg(_db));
            qCWarning(lcSql) << "Sqlite exec statement error:" << _errId << _error << "in" << _sql;
            if (_errId == SQLITE_IOERR) {
                qCWarning(lcSql) << "IOERR extended errcode: " << sqlite3_extended_errcode(_db);
#if SQLITE_VERSION_NUMBER >= 3012000
                qCWarning(lcSql) << "IOERR system errno: " << sqlite3_system_errno(_db);
#endif
            }
        } else {
            qCDebug(lcSql) << "Last exec affected" << numRowsAffected() << "rows.";
        }
        return (_errId == SQLITE_DONE); // either SQLITE_ROW or SQLITE_DONE
    }

    return true;
}

auto SqlQuery::next() -> NextResult
{
    const bool firstStep = !sqlite3_stmt_busy(_stmt);

    for (int n = 0; n < SQLITE_REPEAT_COUNT; ++n) {
        _errId = sqlite3_step(_stmt);
        if (!(firstStep && (_errId == SQLITE_LOCKED || _errId == SQLITE_BUSY))) {
            // For the first step: retry if the database is locked or busy.
            // For any other iteration: the first one succeeded, so subsequent steps should also
            // succeed. If not, bail out and report an error (done below).
            break;
        }
    }

    NextResult result;
    result.ok = _errId == SQLITE_ROW || _errId == SQLITE_DONE;
    result.hasData = _errId == SQLITE_ROW;
    if (!result.ok) {
        _error = QString::fromUtf8(sqlite3_errmsg(_db));
        qCWarning(lcSql) << "Sqlite step statement error:" << _errId << _error << "in" << _sql;
    }

    return result;
}

void SqlQuery::bindValueInternal(int pos, const QVariant &value)
{
    int res = -1;
    if (!_stmt) {
        OC_ASSERT(false);
        return;
    }

    const auto bindUtf16 = [this](int pos, const QString &str) {
        return sqlite3_bind_text16(_stmt, pos, str.utf16(), static_cast<int>(str.size() * sizeof(ushort)), SQLITE_TRANSIENT);
    };

    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::Bool:
        res = sqlite3_bind_int(_stmt, pos, value.toInt());
        break;
    case QMetaType::Double:
        res = sqlite3_bind_double(_stmt, pos, value.toDouble());
        break;
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        res = sqlite3_bind_int64(_stmt, pos, value.toLongLong());
        break;
    case QMetaType::QDateTime: {
        const QDateTime dateTime = value.toDateTime();
        const QString str = dateTime.toString(QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz"));
        res = bindUtf16(pos, str);
        break;
    }
    case QMetaType::QTime: {
        const QTime time = value.toTime();
        const QString str = time.toString(QStringLiteral("hh:mm:ss.zzz"));
        res = bindUtf16(pos, str);
        break;
    }
    case QMetaType::QString: {
        if (!value.toString().isNull()) {
            // lifetime of string == lifetime of its qvariant
            const QString *str = static_cast<const QString *>(value.constData());
            res = bindUtf16(pos, *str);
        } else {
            res = sqlite3_bind_null(_stmt, pos);
        }
        break;
    }
    case QMetaType::QByteArray: {
        auto ba = value.toByteArray();
        res = sqlite3_bind_text(_stmt, pos, ba.constData(), ba.size(), SQLITE_TRANSIENT);
        break;
    }
    default: {
        QString str = value.toString();
        // SQLITE_TRANSIENT makes sure that sqlite buffers the data
        res = sqlite3_bind_text16(_stmt, pos, str.utf16(), static_cast<int>(str.size() * sizeof(ushort)), SQLITE_TRANSIENT);
        break;
    }
    }
    if (res != SQLITE_OK) {
        qCWarning(lcSql) << "ERROR binding SQL value:" << value << "error:" << res;
    }
    OC_ASSERT(res == SQLITE_OK);
}

bool SqlQuery::nullValue(int index)
{
    return sqlite3_column_type(_stmt, index) == SQLITE_NULL;
}

QString SqlQuery::stringValue(int index)
{
    return QString::fromUtf16(static_cast<const char16_t *>(sqlite3_column_text16(_stmt, index)));
}

int SqlQuery::intValue(int index)
{
    return sqlite3_column_int(_stmt, index);
}

quint64 SqlQuery::int64Value(int index)
{
    return sqlite3_column_int64(_stmt, index);
}

QByteArray SqlQuery::baValue(int index)
{
    return QByteArray(static_cast<const char *>(sqlite3_column_blob(_stmt, index)), sqlite3_column_bytes(_stmt, index));
}

QString SqlQuery::error() const
{
    return _error;
}

int SqlQuery::errorId() const
{
    return _errId;
}

const QByteArray &SqlQuery::lastQuery() const
{
    return _sql;
}

int SqlQuery::numRowsAffected()
{
    return sqlite3_changes(_db);
}

void SqlQuery::finish()
{
    if (!_stmt)
        return;
    SQLITE_DO(sqlite3_finalize(_stmt));
    _stmt = nullptr;
    if (_sqldb) {
        _sqldb->_queries.remove(this);
    }
}

void SqlQuery::reset_and_clear_bindings()
{
    if (_stmt) {
        SQLITE_DO(sqlite3_reset(_stmt));
        SQLITE_DO(sqlite3_clear_bindings(_stmt));
    }
}

} // namespace OCC
