// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "simplenetworkjob.h"

#include <QBuffer>
#include <QJsonDocument>

using namespace OCC;

SimpleNetworkJob::SimpleNetworkJob(
    AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, const QNetworkRequest &req, QObject *parent)
    : AbstractNetworkJob(account, rootUrl, path, parent)
    , _request(req)
    , _verb(verb)
{
}

SimpleNetworkJob::SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, QObject *parent)
    : SimpleNetworkJob(account, rootUrl, path, verb, {}, parent)
{
}

SimpleNetworkJob::SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, const UrlQuery &arguments,
    const QNetworkRequest &req, QObject *parent)
    : SimpleNetworkJob(account, rootUrl, path, verb, req, parent)
{
    Q_ASSERT((QList<QByteArray>{"GET", "PUT", "POST", "DELETE", "HEAD", "PATCH"}.contains(verb)));
    if (!arguments.isEmpty()) {
        QUrlQuery args;
        // ensure everything is percent encoded
        // this is especially important for parameters that contain spaces or +
        for (const auto &item : arguments) {
            args.addQueryItem(QString::fromUtf8(QUrl::toPercentEncoding(item.first)), QString::fromUtf8(QUrl::toPercentEncoding(item.second)));
        }
        if (verb == QByteArrayLiteral("POST") || verb == QByteArrayLiteral("PUT") || verb == QByteArrayLiteral("PATCH")) {
            _request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded; charset=UTF-8"));
            _body = args.query(QUrl::FullyEncoded).toUtf8();
            _device = std::make_unique<QBuffer>(&_body);
        } else {
            setQuery(args);
        }
    }
}

SimpleNetworkJob::SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, const QJsonObject &arguments,
    const QNetworkRequest &req, QObject *parent)
    : SimpleNetworkJob(account, rootUrl, path, verb, QJsonDocument(arguments).toJson(), req, parent)
{
    _request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
}

SimpleNetworkJob::SimpleNetworkJob(AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb,
    std::unique_ptr<QIODevice> &&requestBody, const QNetworkRequest &req, QObject *parent)
    : SimpleNetworkJob(account, rootUrl, path, verb, req, parent)
{
    _device = std::move(requestBody);
}

// delay setting _device until we adopted the body
SimpleNetworkJob::SimpleNetworkJob(
    AccountPtr account, const QUrl &rootUrl, const QString &path, const QByteArray &verb, QByteArray &&requestBody, const QNetworkRequest &req, QObject *parent)
    : SimpleNetworkJob(account, rootUrl, path, verb, std::unique_ptr<QIODevice>{}, req, parent)
{
    _body = std::move(requestBody);
    _device = std::make_unique<QBuffer>(&_body);
}

SimpleNetworkJob::~SimpleNetworkJob() { }
void SimpleNetworkJob::start()
{
    Q_ASSERT(!_verb.isEmpty());
    // AbstractNetworkJob will take ownership of the buffer
    sendRequest(_verb, _request, std::move(_device));
    AbstractNetworkJob::start();
}

void SimpleNetworkJob::addNewReplyHook(std::function<void(QNetworkReply *)> &&hook)
{
    _replyHooks.push_back(hook);
}

void SimpleNetworkJob::finished()
{
    if (_device) {
        _device->close();
    }
    if (body()) {
        body()->close();
    }
}

void SimpleNetworkJob::newReplyHook(QNetworkReply *reply)
{
    for (const auto &hook : _replyHooks) {
        hook(reply);
    }
}
