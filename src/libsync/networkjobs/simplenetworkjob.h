// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#pragma once

#include "libsync/abstractcorejob.h"

namespace OCC {


/**
 * @brief A basic job around a network request without extra funtionality
 * @ingroup libsync
 *
 * Primarily adds timeout and redirection handling.
 */
class OPENCLOUD_SYNC_EXPORT SimpleNetworkJob : public AbstractNetworkJob
{
    Q_OBJECT
public:
    using UrlQuery = QList<QPair<QString, QString>>;

    // fully qualified urls can be passed in the QNetworkRequest
    explicit SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, QObject *parent);
    explicit SimpleNetworkJob(
        AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, const QNetworkRequest &req = {}, QObject *parent = nullptr);
    explicit SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb,
        std::unique_ptr<QIODevice> &&requestBody = {}, const QNetworkRequest &req = {}, QObject *parent = nullptr);
    explicit SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, const UrlQuery &arguments,
        const QNetworkRequest &req = {}, QObject *parent = nullptr);
    explicit SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, const QJsonObject &arguments,
        const QNetworkRequest &req = {}, QObject *parent = nullptr);
    explicit SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, QByteArray &&requestBody,
        const QNetworkRequest &req = {}, QObject *parent = nullptr);

    virtual ~SimpleNetworkJob();

    void start() override;

    void addNewReplyHook(std::function<void(QNetworkReply *)> &&hook);

protected:
    void finished() override;
    void newReplyHook(QNetworkReply *) override;

    QNetworkRequest _request;

private:
    QByteArray _verb;
    QByteArray _body;
    std::unique_ptr<QIODevice> _device;
    std::vector<std::function<void(QNetworkReply *)>> _replyHooks;
};
}
