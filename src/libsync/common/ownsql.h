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

#ifndef OWNSQL_H
#define OWNSQL_H

#include <QLoggingCategory>
#include <QObject>
#include <QSet>
#include <QVariant>

#include "libsync/opencloudsynclib.h"

struct sqlite3;
struct sqlite3_stmt;

namespace OCC {
OPENCLOUD_SYNC_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcSql)

    class SqlQuery;

/**
 * @brief The SqlDatabase class
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT SqlDatabase
{
    Q_DISABLE_COPY(SqlDatabase)
public:
    explicit SqlDatabase();
    ~SqlDatabase();

    bool isOpen() const;
    bool openOrCreateReadWrite(const QString &filename);
    bool openReadOnly(const QString &filename);
    bool transaction();
    bool commit();
    void close();
    QString error() const;
    sqlite3 *sqliteDb();

private:
    enum class CheckDbResult {
        Ok,
        CantPrepare,
        CantExec,
        NotOk,
    };

    bool openHelper(const QString &filename, int sqliteFlags);
    CheckDbResult checkDb();

    sqlite3 *_db;
    QString _error; // last error string
    int _errId;

    friend class SqlQuery;
    QSet<SqlQuery *> _queries;
};

/**
 * @brief The SqlQuery class
 * @ingroup libsync
 *
 * There is basically 3 ways to initialize and use a query:
 *
    SqlQuery q1;
    [...]
    q1.initOrReset(...);
    q1.bindValue(...);
    q1.exec(...)
 *
    SqlQuery q2(db);
    q2.prepare(...);
    [...]
    q2.reset_and_clear_bindings();
    q2.bindValue(...);
    q2.exec(...)
 *
    SqlQuery q3("...", db);
    q3.bindValue(...);
    q3.exec(...)
 *
 */
class OPENCLOUD_SYNC_EXPORT SqlQuery
{
    Q_DISABLE_COPY(SqlQuery)
public:
    explicit SqlQuery() = default;
    explicit SqlQuery(SqlDatabase &db);
    explicit SqlQuery(const QByteArray &sql, SqlDatabase &db);
    /**
     * Prepare the SqlQuery.
     * If the query was already prepared, this will first call finish(), and re-prepare it.
     * This function must only be used if the constructor was setting a SqlDatabase
     */
    int prepare(const QByteArray &sql, bool allow_failure = false);

    ~SqlQuery();
    QString error() const;
    int errorId() const;

    /// Checks whether the value at the given column index is NULL
    bool nullValue(int index);

    QString stringValue(int index);
    int intValue(int index);
    quint64 int64Value(int index);
    QByteArray baValue(int index);
    bool isSelect();
    bool isPragma();
    bool exec();

    struct NextResult
    {
        bool ok = false;
        bool hasData = false;
    };
    NextResult next();

    template <class T>
    void bindValue(int pos, const T &value)
    {
        const auto converted = convertValue(value);
        if (lcSql().isDebugEnabled() && !_boundValues.isEmpty()) {
            QString sqlValue;
            auto stream = QDebug(&sqlValue).noquote().nospace() << '\'' << value;
            if (typeid(converted) != typeid(value)) {
                stream << " [" << converted << ']';
            }
            stream << '\'';
            _boundValues[pos - 1].value = sqlValue;
        }
        bindValueInternal(pos, converted);
    }

    const QByteArray &lastQuery() const;
    int numRowsAffected();
    void reset_and_clear_bindings();

private:
    template <class T, typename std::enable_if<std::is_enum<T>::value, int>::type = 0>
    auto convertValue(const T &value)
    {
        return static_cast<int>(value);
    }

    template <class T, typename std::enable_if<!std::is_enum<T>::value, int>::type = 0>
    auto convertValue(const T &value)
    {
        return value;
    }

    void bindValueInternal(int pos, const QVariant &value);
    void finish();

    SqlDatabase *_sqldb = nullptr;
    sqlite3 *_db = nullptr;
    sqlite3_stmt *_stmt = nullptr;
    QString _error;
    int _errId;
    QByteArray _sql;
    struct Binding
    {
        QString name;
        QString value;
    };
    QVector<Binding> _boundValues;

    friend class SqlDatabase;
    friend class PreparedSqlQueryManager;
};

} // namespace OCC

#endif // OWNSQL_H
