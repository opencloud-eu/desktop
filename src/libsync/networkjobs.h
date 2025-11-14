/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
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

#ifndef NETWORKJOBS_H
#define NETWORKJOBS_H

#include "abstractnetworkjob.h"
#include "common/result.h"
#include <QJsonObject>
#include <QUrlQuery>
#include <functional>

class QUrl;

namespace OCC {

struct HttpError
{
    int code; // HTTP error code
    QString message;
};

template <typename T>
using HttpResult = Result<T, HttpError>;

/**
 * @brief The EntityExistsJob class
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT EntityExistsJob : public AbstractNetworkJob
{
    Q_OBJECT
public:
    explicit EntityExistsJob(AccountPtr account, const QUrl &rootUrl, const QString &path, QObject *parent = nullptr);
    void start() override;

Q_SIGNALS:
    void exists(QNetworkReply *);

private Q_SLOTS:
    void finished() override;
};

/**
 * @brief The PropfindJob class parser
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT LsColXMLParser : public QObject
{
    Q_OBJECT
public:
    explicit LsColXMLParser();

    bool parse(const QByteArray &xml, QHash<QString, qint64> *sizes, const QString &expectedPath);

Q_SIGNALS:
    void directoryListingSubfolders(const QStringList &items);
    void directoryListingIterated(const QString &name, const QMap<QString, QString> &properties);
    void finishedWithoutError();
};

class OPENCLOUD_SYNC_EXPORT PropfindJob : public AbstractNetworkJob
{
    Q_OBJECT
public:
    enum class Depth {
        Zero,
        One
    } Q_ENUMS(Depth);
    explicit PropfindJob(AccountPtr account, const QUrl &url, const QString &path, Depth depth, QObject *parent = nullptr);
    void start() override;

    /**
     * Used to specify which properties shall be retrieved.
     *
     * The properties can
     *  - contain no colon: they refer to a property in the DAV: namespace
     *  - contain a colon: and thus specify an explicit namespace,
     *    e.g. "ns:with:colons:bar", which is "bar" in the "ns:with:colons" namespace
     */
    void setProperties(const QList<QByteArray> &properties);
    QList<QByteArray> properties() const;

    // TODO: document...
    const QHash<QString, qint64> &sizes() const;

Q_SIGNALS:
    void directoryListingSubfolders(const QStringList &items);
    void directoryListingIterated(const QString &name, const QMap<QString, QString> &properties);
    void finishedWithError();
    void finishedWithoutError();

private Q_SLOTS:
    void finished() override;

private:
    QList<QByteArray> _properties;
    QHash<QString, qint64> _sizes;
    Depth _depth;
};

/**
 * @brief The MkColJob class
 * @ingroup libsync
 */
class OPENCLOUD_SYNC_EXPORT MkColJob : public AbstractNetworkJob
{
    Q_OBJECT
    HeaderMap _extraHeaders;

public:
    explicit MkColJob(AccountPtr account, const QUrl &url, const QString &path,
        const HeaderMap &extraHeaders, QObject *parent = nullptr);
    void start() override;

Q_SIGNALS:
    void finishedWithError(QNetworkReply *reply);
    void finishedWithoutError();

private:
    void finished() override;
};

/**
 * @brief The RequestEtagJob class
 */
class OPENCLOUD_SYNC_EXPORT RequestEtagJob : public PropfindJob
{
    Q_OBJECT
public:
    explicit RequestEtagJob(AccountPtr account, const QUrl &rootUrl, const QString &path, QObject *parent = nullptr);

    const QString &etag() const;

private:
    QString _etag;
};

/**
 * @brief Runs a PROPFIND to figure out the private link url
 *
 * The numericFileId is used only to build the deprecatedPrivateLinkUrl
 * locally as a fallback. If it's empty and the PROPFIND fails, targetFun
 * will be called with an empty string.
 *
 * The job and signal connections are parented to the target QObject.
 *
 * Note: targetFun is guaranteed to be called only through the event
 * loop and never directly.
 */
void OPENCLOUD_SYNC_EXPORT fetchPrivateLinkUrl(
    AccountPtr account, const QUrl &baseUrl, const QString &remotePath, QObject *target, const std::function<void(const QUrl &url)> &targetFun);

} // namespace OCC


#endif // NETWORKJOBS_H
