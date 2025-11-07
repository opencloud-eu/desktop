/*
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include "creds/oauth.h"

#include "account.h"
#include "common/version.h"
#include "credentialmanager.h"
#include "creds/httpcredentials.h"
#include "networkjobs/checkserverjobfactory.h"
#include "networkjobs/fetchuserinfojobfactory.h"
#include "networkjobs/jsonjob.h"
#include "resources/template.h"
#include "theme.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPixmap>
#include <QRandomGenerator>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace OCC;

Q_LOGGING_CATEGORY(lcOauth, "sync.credentials.oauth", QtInfoMsg)

namespace {

const QString wellKnownPathC = QStringLiteral("/.well-known/openid-configuration");
QString redirectUrlC()
{
    return QStringLiteral("http://127.0.0.1");
}

auto defaultOauthPromtValue()
{
    static const auto promptValue = [] {
        OAuth::PromptValuesSupportedFlags out = OAuth::PromptValuesSupported::none;
        // convert the legacy openIdConnectPrompt() to QFlags
        for (const auto &x : Theme::instance()->openIdConnectPrompt().split(QLatin1Char(' '))) {
            out |= Utility::stringToEnum<OAuth::PromptValuesSupported>(x);
        }
        return out;
    }();
    return promptValue;
}

QString renderHttpTemplate(const QString &title, const QString &content)
{
    auto loadFile = [](const QString &font) {
        QFile f(font);
        OC_ASSERT(f.open(QFile::ReadOnly));
        return f.readAll().toBase64();
    };
    return Resources::Template::renderTemplateFromFile(QStringLiteral(":/client/resources/oauth/oauth.html.in"),
        {
            {"TITLE", title}, //
            {"CONTENT", content}, //
            {"ICON", loadFile(QStringLiteral(":/client/OpenCloud/theme/universal/wizard_logo.svg"))}, //
            {"BACKGROUND_COLOR", Theme::instance()->wizardHeaderBackgroundColor().name()}, //
            {"FONT_COLOR", Theme::instance()->wizardHeaderTitleColor().name()}, //
        });
}

auto defaultTimeout()
{
    // as the OAuth process can be interactive we don't want 5min of inactivity
    return qMin(30s, OCC::AbstractNetworkJob::httpTimeout);
}

auto defaultTimeoutMs()
{
    return static_cast<int>(duration_cast<milliseconds>(defaultTimeout()).count());
}

QString dynamicRegistrationDataC()
{
    return QStringLiteral("oauth/dynamicRegistration");
}

QString idTokenC()
{
    return QStringLiteral("oauth/id_token");
}

QVariant getRequiredField(const QVariantMap &json, const QString &s, QString *error)
{
    const auto out = json.constFind(s);
    if (out == json.constEnd()) {
        error->append(QStringLiteral("\tError: Missing field %1\n").arg(s));
        return {};
    }
    return *out;
}

void httpReplyAndClose(const QPointer<QTcpSocket> &socket, const QString &code, const QString &title, const QString &body = {}, const QStringList &additionalHeader = {})
{
    if (!socket) {
        return; // socket can have been deleted if the browser was closed
    }

    const QByteArray content = renderHttpTemplate(title, body.isEmpty() ? title : body).toUtf8();
    QString header = QStringLiteral("HTTP/1.1 %1\r\n"
                                    "Content-Type: text/html; charset=utf-8\r\n"
                                    "Connection: close\r\n"
                                    "Content-Length: %2\r\n")
                         .arg(code, QString::number(content.length()));

    if (!additionalHeader.isEmpty()) {
        const QString nl = QStringLiteral("\r\n");
        header += additionalHeader.join(nl) + nl;
    }

    const QByteArray msg = header.toUtf8() + QByteArrayLiteral("\r\n") + content;

    qCDebug(lcOauth) << u"replying with HTTP response and closing socket:" << msg;

    socket->write(msg);
    socket->disconnectFromHost();

    // We don't want that deleting the server too early prevent queued data to be sent on this socket.
    // The socket will be deleted after disconnection because disconnected is connected to deleteLater
    socket->setParent(nullptr);
}

class RegisterClientJob : public QObject
{
    Q_OBJECT
public:
    RegisterClientJob(QNetworkAccessManager *networkAccessManager, QVariantMap &dynamicRegistrationData, const QUrl &registrationEndpoint, QObject *parent)
        : QObject(parent)
        , _networkAccessManager(networkAccessManager)
        , _dynamicRegistrationData(dynamicRegistrationData)
        , _registrationEndpoint(registrationEndpoint)
    {
        connect(this, &RegisterClientJob::errorOccured, this, &RegisterClientJob::deleteLater);
        connect(this, &RegisterClientJob::finished, this, &RegisterClientJob::deleteLater);
    }

    void start()
    {
        if (!_dynamicRegistrationData.isEmpty()) {
            registerClientFinished(_dynamicRegistrationData);
        } else {
            registerClientOnline();
        }
    }

Q_SIGNALS:
    void finished(const QString &clientId, const QString &clientSecret, const QVariantMap &dynamicRegistrationData);
    void errorOccured(const QString &error);

private:
    void registerClientOnline()
    {
        const QJsonObject json(
            {{QStringLiteral("client_name"), QStringLiteral("%1 %2").arg(Theme::instance()->appNameGUI(), OCC::Version::versionWithBuildNumber().toString())},
                {QStringLiteral("redirect_uris"), QJsonArray{QStringLiteral("http://127.0.0.1")}},
                {QStringLiteral("application_type"), QStringLiteral("native")}, //
                {QStringLiteral("token_endpoint_auth_method"), QStringLiteral("none")}});
        QNetworkRequest req;
        req.setUrl(_registrationEndpoint);
        req.setAttribute(HttpCredentials::DontAddCredentialsAttribute, true);
        req.setTransferTimeout(defaultTimeoutMs());
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        auto reply = _networkAccessManager->post(req, QJsonDocument(json).toJson());
        connect(reply, &QNetworkReply::finished, this, [reply, this] {
            // https://datatracker.ietf.org/doc/html/rfc7591#section-3.2
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 201) {
                const auto data = reply->readAll();
                QJsonParseError error{};
                const auto json = QJsonDocument::fromJson(data, &error);
                if (error.error == QJsonParseError::NoError) {
                    registerClientFinished(json.object().toVariantMap());
                } else {
                    qCWarning(lcOauth) << u"Failed to register the client" << error.errorString() << data;
                    Q_EMIT errorOccured(error.errorString());
                }
            } else {
                Q_EMIT errorOccured(reply->errorString());
            }
        });
    }

    void registerClientFinished(const QVariantMap &data)
    {
        // extracting these values could be done by the signal receiver, too, but that'd require duplicating the error handling code
        // therefore, we extract the values here and pass them separately in the signal
        // sure, the data will be redundant, but it's worth it
        QString error;
        const auto client_id = getRequiredField(data, QStringLiteral("client_id"), &error).toString();
        if (!error.isEmpty()) {
            Q_EMIT errorOccured(error);
            return;
        }
        Q_EMIT finished(client_id, {}, data);
    }

private:
    QNetworkAccessManager *_networkAccessManager;
    QVariantMap _dynamicRegistrationData;
    QUrl _registrationEndpoint;
};

void logCredentialsJobResult(CredentialJob *credentialsJob)
{
    qCDebug(lcOauth) << u"credentials job has finished";

    if (!credentialsJob->data().isValid()) {
        qCInfo(lcOauth) << u"Failed to read client id" << credentialsJob->errorString();
    }
}
}

OAuth::OAuth(const QUrl &serverUrl, QNetworkAccessManager *networkAccessManager, const QVariantMap &dynamicRegistrationData, QObject *parent)
    : QObject(parent)
    , _serverUrl(serverUrl)
    , _dynamicRegistrationData(dynamicRegistrationData)
    , _networkAccessManager(networkAccessManager)
    , _clientId(Theme::instance()->oauthClientId())
    , _clientSecret(Theme::instance()->oauthClientSecret())
    , _supportedPromtValues(defaultOauthPromtValue())
{
}

OAuth::~OAuth() = default;

void OAuth::setIdToken(IdToken &&idToken)
{
    _idToken = std::move(idToken);
}

const IdToken &OAuth::idToken() const
{
    return _idToken;
}

QVariantMap OAuth::dynamicRegistrationData() const
{
    return _dynamicRegistrationData;
}

void OAuth::startAuthentication()
{
    qCDebug(lcOauth) << u"starting authentication";

    // Listen on the socket to get a port which will be used in the redirect_uri

    for (const auto port : Theme::instance()->oauthPorts()) {
        if (_server.listen(QHostAddress::LocalHost, port)) {
            break;
        }
        qCDebug(lcOauth) << u"Creating local server Port:" << port << u"failed. Error:" << _server.errorString();
    }
    if (!_server.isListening()) {
        qCDebug(lcOauth) << u"server is not listening";
        Q_EMIT result(Error, {});
        return;
    }

    _pkceCodeVerifier = generateRandomString(24);
    OC_ASSERT(_pkceCodeVerifier.size() == 128)
    _state = generateRandomString(8);

    connect(this, &OAuth::fetchWellKnownFinished, this, [this] {
        connect(this, &AccountBasedOAuth::dynamicRegistrationDataReceived, this, &OAuth::authorisationLinkChanged);
        updateDynamicRegistration();
    });

    fetchWellKnown();

    QObject::connect(&_server, &QTcpServer::newConnection, this, [this] {
        while (QPointer<QTcpSocket> socket = _server.nextPendingConnection()) {
            qCDebug(lcOauth) << u"accepted client connection from" << socket->peerAddress();

            QObject::connect(socket.data(), &QTcpSocket::disconnected, socket.data(), &QTcpSocket::deleteLater);

            QObject::connect(socket.data(), &QIODevice::readyRead, this, [this, socket] {
                const QByteArray peek = socket->peek(qMin(socket->bytesAvailable(), 4000LL)); //The code should always be within the first 4K

                // wait until we find a \n
                if (!peek.contains('\n')) {
                    return;
                }

                qCDebug(lcOauth) << u"Server provided:" << peek;

                const auto getPrefix = QByteArrayLiteral("GET /?");
                if (!peek.startsWith(getPrefix)) {
                    httpReplyAndClose(socket, QStringLiteral("404 Not Found"), QStringLiteral("404 Not Found"));
                    return;
                }
                const auto endOfUrl = peek.indexOf(' ', getPrefix.length());
                const QUrlQuery args(QUrl::fromPercentEncoding(peek.mid(getPrefix.length(), endOfUrl - getPrefix.length())));
                if (args.queryItemValue(QStringLiteral("state")).toUtf8() != _state) {
                    httpReplyAndClose(socket, QStringLiteral("400 Bad Request"), QStringLiteral("400 Bad Request"));
                    return;
                }

                // server port cannot be queried any more after server has been closed, which we want to do as early as possible in the processing chain
                // therefore we have to store it beforehand
                const auto serverPort = _server.serverPort();

                // we only allow one response
                qCDebug(lcOauth) << u"Received the first valid response, closing server socket";
                _server.close();

                auto reply = postTokenRequest({
                    {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                    {QStringLiteral("code"), args.queryItemValue(QStringLiteral("code"))},
                    {QStringLiteral("redirect_uri"), QStringLiteral("%1:%2").arg(redirectUrlC(), QString::number(serverPort))},
                    {QStringLiteral("code_verifier"), QString::fromUtf8(_pkceCodeVerifier)},
                });

                connect(reply, &QNetworkReply::finished, this, [reply, socket, this] {
                    const auto jsonData = reply->readAll();
                    QJsonParseError jsonParseError;
                    const auto data = QJsonDocument::fromJson(jsonData, &jsonParseError).object().toVariantMap();
                    QString fieldsError;
                    const QString accessToken = getRequiredField(data, QStringLiteral("access_token"), &fieldsError).toString();
                    const QString refreshToken = getRequiredField(data, QStringLiteral("refresh_token"), &fieldsError).toString();
                    const QString tokenType = getRequiredField(data, QStringLiteral("token_type"), &fieldsError).toString().toLower();
                    const QUrl messageUrl = QUrl::fromEncoded(data[QStringLiteral("message_url")].toByteArray());
                    auto idToken = IdToken(JWT(getRequiredField(data, QStringLiteral("id_token"), &fieldsError).toByteArray()).payload());


                    auto reportError = [socket, this](const QString &errorReason) {
                        qCWarning(lcOauth) << u"Error when getting the accessToken" << errorReason;
                        httpReplyAndClose(
                            socket, QStringLiteral("500 Internal Server Error"), tr("Login Error"), tr("<h1>Login Error</h1><p>%1</p>").arg(errorReason));
                        Q_EMIT result(Error);
                    };
                    if (reply->error() != QNetworkReply::NoError || jsonParseError.error != QJsonParseError::NoError
                        || !fieldsError.isEmpty()
                        || tokenType != QLatin1String("bearer")) {
                        // do we have error message suitable for users?
                        QString errorReason = data[QStringLiteral("error_description")].toString();
                        if (errorReason.isEmpty()) {
                            // fall back to technical error
                            errorReason = data[QStringLiteral("error")].toString();
                        }
                        if (!errorReason.isEmpty()) {
                            reportError(tr("Error returned from the server: <em>%1</em>").arg(errorReason.toHtmlEscaped()));
                        } else if (reply->error() != QNetworkReply::NoError) {
                            reportError(tr("There was an error accessing the 'token' endpoint: <br><em>%1</em>").arg(reply->errorString().toHtmlEscaped()));
                        } else if (jsonParseError.error != QJsonParseError::NoError) {
                            reportError(tr("Could not parse the JSON returned from the server: <br><em>%1</em>").arg(jsonParseError.errorString()));
                        } else if (tokenType != QStringLiteral("bearer")) {
                            reportError(tr("Unsupported token type: %1").arg(tokenType));
                        } else if (!fieldsError.isEmpty()) {
                            reportError(tr("The reply from the server did not contain all expected fields\n:%1").arg(fieldsError));
                        } else {
                            reportError(tr("Unknown Error"));
                        }
                    } else if (!idToken.aud().contains(_clientId)) {
                        reportError(tr("The audience of the id_token did not contain \"%1\"").arg(_clientId));
                    } else if (_idToken.isValid() && _idToken.sub() != idToken.sub()) {
                        // Connected with the wrong user
                        qCWarning(lcOauth) << u"We expected the user" << _idToken.toJson() << u"but the server answered with user" << idToken.toJson();
                        const QString expectedName = !_idToken.preferred_username().isEmpty() ? _idToken.preferred_username() : _idToken.name();
                        const QString actualName = !idToken.preferred_username().isEmpty() ? idToken.preferred_username() : idToken.name();
                        QString message;
                        if (!expectedName.isEmpty() && !actualName.isEmpty() && expectedName != actualName) {
                            message = tr("<h1>Incorrect user</h1>"
                                         "<p>You logged-in as user <em>%1</em>, but must login with user <em>%2</em>.<br>"
                                         "Please return to the %3 and restart the authentication.</p>")
                                          .arg(actualName, expectedName, Theme::instance()->appNameGUI());
                        } else {
                            message = tr("<h1>Incorrect user</h1>"
                                         "<p>You logged-in as a different user than is associated with this account.<br>"
                                         "Please return to the %1 and restart the authentication.</p>")
                                          .arg(Theme::instance()->appNameGUI());
                        }
                        httpReplyAndClose(socket, QStringLiteral("403 Forbidden"), tr("Incorrect user"), message);
                        Q_EMIT result(Error);
                    } else {
                        setIdToken(std::move(idToken));
                        finalize(socket, accessToken, refreshToken, messageUrl);
                    }
                });
            });
        }
    });
}

void OAuth::finalize(const QPointer<QTcpSocket> &socket, const QString &accessToken, const QString &refreshToken, const QUrl &messageUrl)
{
    const QString loginSuccessfulHtml = tr("<h1>Login successful</h1><p>You can close this window.</p>");
    const QString loginSuccessfulTitle = tr("Login successful");
    if (messageUrl.isValid()) {
        httpReplyAndClose(socket, QStringLiteral("303 See Other"), loginSuccessfulTitle, loginSuccessfulHtml,
            {QStringLiteral("Location: %1").arg(QString::fromUtf8(messageUrl.toEncoded()))});
    } else {
        httpReplyAndClose(socket, QStringLiteral("200 OK"), loginSuccessfulTitle, loginSuccessfulHtml);
    }
    Q_EMIT result(LoggedIn, accessToken, refreshToken);
}

QNetworkReply *OAuth::postTokenRequest(QUrlQuery &&queryItems)
{
    QNetworkRequest req;
    req.setTransferTimeout(defaultTimeoutMs());
    switch (_endpointAuthMethod) {
    case TokenEndpointAuthMethods::client_secret_basic:
        req.setRawHeader("Authorization", "Basic " + QStringLiteral("%1:%2").arg(_clientId, _clientSecret).toUtf8().toBase64());
        break;
    case TokenEndpointAuthMethods::client_secret_post:
        queryItems.addQueryItem(QStringLiteral("client_id"), _clientId);
        queryItems.addQueryItem(QStringLiteral("client_secret"), _clientSecret);
        break;
    case TokenEndpointAuthMethods::none:
        queryItems.addQueryItem(QStringLiteral("client_id"), _clientId);
        break;
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded; charset=UTF-8"));
    req.setAttribute(HttpCredentials::DontAddCredentialsAttribute, true);

    queryItems.addQueryItem(QStringLiteral("scope"), QString::fromUtf8(QUrl::toPercentEncoding(Theme::instance()->openIdConnectScopes())));
    req.setUrl(_tokenEndpoint);
    return _networkAccessManager->post(req, queryItems.toString(QUrl::FullyEncoded).toUtf8());
}

QByteArray OAuth::generateRandomString(size_t size) const
{
    // TODO: do we need a varaible size?
    std::vector<quint32> buffer(size, 0);
    QRandomGenerator::global()->fillRange(buffer.data(), static_cast<qsizetype>(size));
    return QByteArray(reinterpret_cast<char *>(buffer.data()), static_cast<int>(size * sizeof(quint32))).toBase64(QByteArray::Base64UrlEncoding);
}

QUrl OAuth::authorisationLink() const
{
    Q_ASSERT(_server.isListening());
    Q_ASSERT(_wellKnownFinished);
    Q_ASSERT(_authEndpoint.isValid());

    const QByteArray code_challenge = QCryptographicHash::hash(_pkceCodeVerifier, QCryptographicHash::Sha256)
                                          .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrlQuery query{
        {QStringLiteral("response_type"), QStringLiteral("code")},
        {QStringLiteral("client_id"), _clientId},
        {QStringLiteral("redirect_uri"), QStringLiteral("%1:%2").arg(redirectUrlC(), QString::number(_server.serverPort()))},
        {QStringLiteral("code_challenge"), QString::fromLatin1(code_challenge)},
        {QStringLiteral("code_challenge_method"), QStringLiteral("S256")},
        {QStringLiteral("scope"), QString::fromUtf8(QUrl::toPercentEncoding(Theme::instance()->openIdConnectScopes()))},
        {QStringLiteral("prompt"), QString::fromUtf8(QUrl::toPercentEncoding(toString(_supportedPromtValues)))},
        {QStringLiteral("state"), QString::fromUtf8(_state)},
    };

    if (!_idToken.preferred_username().isEmpty()) {
        query.addQueryItem(QStringLiteral("login_hint"), QString::fromUtf8(QUrl::toPercentEncoding(_idToken.preferred_username())));
    }
    return Utility::concatUrlPath(_authEndpoint, {}, query);
}

QString OAuth::clientId() const
{
    return _clientId;
}

QString OAuth::clientSecret() const
{
    return _clientSecret;
}

void OAuth::persist(const OCC::AccountPtr &accountPtr, const QVariantMap &dynamicRegistrationData, const IdToken &idToken)
{
    if (!dynamicRegistrationData.isEmpty()) {
        accountPtr->credentialManager()->set(dynamicRegistrationDataC(), dynamicRegistrationData);
    } else {
        accountPtr->credentialManager()->clear(dynamicRegistrationDataC());
    }
    if (idToken.isValid()) {
        accountPtr->credentialManager()->set(idTokenC(), idToken.toJson());
    } else {
        accountPtr->credentialManager()->clear(idTokenC());
    }
}

void OAuth::updateDynamicRegistration()
{
    // this slightly complicated construct allows us to log case-specific messages
    if (!Theme::instance()->oidcEnableDynamicRegistration()) {
        qCDebug(lcOauth) << u"dynamic registration disabled by theme";
    } else if (!_registrationEndpoint.isValid()) {
        qCDebug(lcOauth) << u"registration endpoint not provided or empty:" << _registrationEndpoint.toString()
                         << u"we asume dynamic registration is not supported by the server";
    } else {
        auto registerJob = new RegisterClientJob(_networkAccessManager, _dynamicRegistrationData, _registrationEndpoint, this);
        connect(registerJob, &RegisterClientJob::finished, this,
            [this](const QString &clientId, const QString &clientSecret, const QVariantMap &dynamicRegistrationData) {
                qCDebug(lcOauth) << u"client registration finished successfully";
                _clientId = clientId;
                _clientSecret = clientSecret;
                _dynamicRegistrationData = dynamicRegistrationData;
                Q_EMIT dynamicRegistrationDataReceived();
            });
        connect(registerJob, &RegisterClientJob::errorOccured, this, [this](const QString &error) {
            qCWarning(lcOauth) << u"Failed to dynamically register the client, try the default client id" << error;
            Q_EMIT dynamicRegistrationDataReceived();
        });
        registerJob->start();
        return;
    }
    Q_EMIT dynamicRegistrationDataReceived();
}

void OAuth::fetchWellKnown()
{
    const QPair<QString, QString> urls = Theme::instance()->oauthOverrideAuthUrl();

    if (!urls.first.isNull()) {
        OC_ASSERT(!urls.second.isNull());
        _authEndpoint = QUrl(urls.first);
        _tokenEndpoint = QUrl(urls.second);

        qCDebug(lcOauth) << u"override URL set, using auth endpoint" << _authEndpoint << u"and token endpoint" << _tokenEndpoint;

        _wellKnownFinished = true;
        Q_EMIT fetchWellKnownFinished();
    } else {
        qCDebug(lcOauth) << u"fetching" << wellKnownPathC;

        QNetworkRequest req;
        req.setAttribute(HttpCredentials::DontAddCredentialsAttribute, true);
        req.setUrl(Utility::concatUrlPath(_serverUrl, wellKnownPathC));
        req.setTransferTimeout(defaultTimeoutMs());

        auto reply = _networkAccessManager->get(req);

        connect(reply, &QNetworkReply::finished, this, [reply, this] {
            _wellKnownFinished = true;
            if (reply->error() != QNetworkReply::NoError) {
                qCDebug(lcOauth) << u"failed to fetch .well-known reply, error:" << reply->error();
                if (_isRefreshingToken) {
                    Q_EMIT refreshError(reply->error(), reply->errorString());
                } else {
                    Q_EMIT result(Error);
                }
                return;
            }
            QJsonParseError err = {};
            QJsonObject data = QJsonDocument::fromJson(reply->readAll(), &err).object();
            if (err.error == QJsonParseError::NoError) {
                _authEndpoint = QUrl::fromEncoded(data[QStringLiteral("authorization_endpoint")].toString().toUtf8());
                _tokenEndpoint = QUrl::fromEncoded(data[QStringLiteral("token_endpoint")].toString().toUtf8());
                _registrationEndpoint = QUrl::fromEncoded(data[QStringLiteral("registration_endpoint")].toString().toUtf8());

                if (_clientSecret.isEmpty()) {
                    _endpointAuthMethod = TokenEndpointAuthMethods::none;
                } else {
                    const auto authMethods = data.value(QStringLiteral("token_endpoint_auth_methods_supported")).toArray();
                    if (authMethods.contains(QStringLiteral("none"))) {
                        _endpointAuthMethod = TokenEndpointAuthMethods::none;
                    } else if (authMethods.contains(QStringLiteral("client_secret_post"))) {
                        _endpointAuthMethod = TokenEndpointAuthMethods::client_secret_post;
                    } else if (authMethods.contains(QStringLiteral("client_secret_basic"))) {
                        _endpointAuthMethod = TokenEndpointAuthMethods::client_secret_basic;
                    } else {
                        OC_ASSERT_X(
                            false, qPrintable(QStringLiteral("Unsupported token_endpoint_auth_methods_supported: %1").arg(QDebug::toString(authMethods))));
                    }
                }
                const auto promtValuesSupported = data.value(QStringLiteral("prompt_values_supported")).toArray();
                if (!promtValuesSupported.isEmpty()) {
                    _supportedPromtValues = PromptValuesSupported::none;
                    for (const auto &x : promtValuesSupported) {
                        const auto flag = Utility::stringToEnum<PromptValuesSupported>(x.toString());
                        // only use flags present in Theme::instance()->openIdConnectPrompt()
                        if (flag & defaultOauthPromtValue())
                            _supportedPromtValues |= flag;
                    }
                }

                qCDebug(lcOauth) << u"parsing .well-known reply successful, auth endpoint" << _authEndpoint << u"and token endpoint" << _tokenEndpoint
                                 << u"and registration endpoint" << _registrationEndpoint;
            } else if (err.error == QJsonParseError::IllegalValue) {
                qCDebug(lcOauth) << u"failed to parse .well-known reply as JSON, server might not support OIDC";
            } else {
                qCDebug(lcOauth) << u"failed to parse .well-known reply, error:" << err.error;
                Q_EMIT result(Error);
            }
            Q_EMIT fetchWellKnownFinished();
        });
    }
}

/**
 * Checks whether a URL returned by the server is valid.
 * @param url URL to validate
 * @return true if validation is successful, false otherwise
 */
bool isUrlValid(const QUrl &url)
{
    qCDebug(lcOauth()) << u"Checking URL for validity:" << url;

    // we have hardcoded the oauthOverrideAuth
    const auto overrideUrl = Theme::instance()->oauthOverrideAuthUrl();
    if (!overrideUrl.first.isEmpty()) {
        return QUrl::fromUserInput(overrideUrl.first).matches(url, QUrl::RemoveQuery);
    }

    // the following allowlist contains URL schemes accepted as valid
    // OAuth 2.0 URLs must be HTTPS to be in compliance with the specification
    // for unit tests, we also permit the nonexisting oauthtest scheme
    const QStringList allowedSchemes({ QStringLiteral("https"), QStringLiteral("oauthtest") });
    return allowedSchemes.contains(url.scheme());
}

void OAuth::openBrowser()
{
    Q_ASSERT(!authorisationLink().isEmpty());

    qCDebug(lcOauth) << u"opening browser";

    if (!isUrlValid(authorisationLink())) {
        qCWarning(lcOauth) << u"URL validation failed";
        Q_EMIT result(ErrorInsecureUrl, QString());
        return;
    }

    if (!QDesktopServices::openUrl(authorisationLink())) {
        qCWarning(lcOauth) << u"QDesktopServices::openUrl Failed";
        Q_EMIT result(Error, {});
    }
}

AccountBasedOAuth::AccountBasedOAuth(AccountPtr account, QObject *parent)
    : OAuth(account->url(), account->accessManager(), {}, parent)
    , _account(account)
{
    connect(this, &AccountBasedOAuth::result, this, [account, this](OAuth::Result result, const QString &, const QString &) {
        if (result == OAuth::LoggedIn) {
            persist(account, dynamicRegistrationData(), idToken());
        }
    });
}

void AccountBasedOAuth::startAuthentication()
{
    qCDebug(lcOauth) << u"fetching dynamic registration data";
    connect(this, &AccountBasedOAuth::restored, this, [this] {
        // explicitly call base implementation, this can't be done directly in the connect
        OAuth::startAuthentication();
    });
    restore();
}

void AccountBasedOAuth::fetchWellKnown()
{
    qCDebug(lcOauth) << u"starting CheckServerJob before fetching" << wellKnownPathC;

    auto *checkServerJob = CheckServerJobFactory::createFromAccount(_account, true, this).startJob(_serverUrl, this);

    connect(checkServerJob, &CoreJob::finished, this, [checkServerJob, this]() {
        if (checkServerJob->success()) {
            qCDebug(lcOauth) << u"CheckServerJob succeeded, fetching" << wellKnownPathC;
            OAuth::fetchWellKnown();
        } else {
            qCDebug(lcOauth) << u"CheckServerJob failed, error:" << checkServerJob->errorMessage();
            if (_isRefreshingToken) {
                Q_EMIT refreshError(checkServerJob->reply()->error(), checkServerJob->errorMessage());
            } else {
                Q_EMIT result(Error);
            }
        }
    });
}
void AccountBasedOAuth::restore()
{
    if (_restored) {
        Q_EMIT restored(QPrivateSignal());
        return;
    }
    _restored = true;
    auto idTokenJob = _account->credentialManager()->get(idTokenC());
    connect(idTokenJob, &CredentialJob::finished, this, [idTokenJob, this] {
        if (idTokenJob->error() == QKeychain::EntryNotFound) {
            qCWarning(lcOauth) << u"idToken token token credential not found";
        } else if (idTokenJob->error() != QKeychain::NoError) {
            Q_EMIT result(Error);
            return;
        } else {
            setIdToken(IdToken(idTokenJob->data().value<QJsonObject>()));
        }

        auto credentialsJob = _account->credentialManager()->get(dynamicRegistrationDataC());
        connect(credentialsJob, &CredentialJob::finished, this, [this, credentialsJob] {
            qCDebug(lcOauth) << u"fetched dynamic registration data" << credentialsJob->errorString();
            logCredentialsJobResult(credentialsJob);

            _dynamicRegistrationData = credentialsJob->data().value<QVariantMap>();
            Q_EMIT restored(QPrivateSignal());
        });
    });
}

void AccountBasedOAuth::refreshAuthentication(const QString &refreshToken)
{
    if (!OC_ENSURE(!_isRefreshingToken)) {
        qCDebug(lcOauth) << u"already refreshing token, aborting";
        return;
    }

    _isRefreshingToken = true;

    connect(this, &AccountBasedOAuth::restored, this, [refreshToken, this] {
        connect(this, &OAuth::fetchWellKnownFinished, this, [refreshToken, this] {
            connect(this, &AccountBasedOAuth::dynamicRegistrationDataReceived, this, [refreshToken, this] {
                auto reply =
                    postTokenRequest({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")}, {QStringLiteral("refresh_token"), refreshToken}});
                connect(reply, &QNetworkReply::finished, this, [reply, refreshToken, this]() {
                    const auto jsonData = reply->readAll();
                    QJsonParseError jsonParseError;
                    const auto data = QJsonDocument::fromJson(jsonData, &jsonParseError).object().toVariantMap();
                    QString accessToken;
                    QString newRefreshToken = refreshToken;
                    // https://developer.okta.com/docs/reference/api/oidc/#response-properties-2
                    const QString errorString = data.value(QStringLiteral("error")).toString();
                    if (!errorString.isEmpty()) {
                        if (errorString == QLatin1String("invalid_grant") || errorString == QLatin1String("invalid_request")) {
                            newRefreshToken.clear();
                        } else {
                            qCWarning(lcOauth) << u"Error while refreshing the token:" << errorString
                                               << data.value(QStringLiteral("error_description")).toString();
                            Q_EMIT refreshError(QNetworkReply::NoError, data.value(QStringLiteral("error_description")).toString());
                            return;
                        }
                    } else if (reply->error() != QNetworkReply::NoError) {
                        qCWarning(lcOauth) << u"Error while refreshing the token:" << reply->error() << u":" << reply->errorString()
                                           << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                        Q_EMIT refreshError(reply->error(), reply->errorString());
                        return;
                    } else {
                        if (jsonParseError.error != QJsonParseError::NoError || data.isEmpty()) {
                            // Invalid or empty JSON: Network error maybe?
                            qCWarning(lcOauth) << u"Error while refreshing the token:" << jsonParseError.errorString();
                        } else {
                            QString error;
                            accessToken = getRequiredField(data, QStringLiteral("access_token"), &error).toString();
                            if (!error.isEmpty()) {
                                qCWarning(lcOauth) << u"The reply from the server did not contain all expected fields:" << error;
                            }

                            const auto refresh_token = data.find(QStringLiteral("refresh_token"));
                            if (refresh_token != data.constEnd()) {
                                newRefreshToken = refresh_token.value().toString();
                            }
                        }
                    }
                    Q_EMIT refreshFinished(accessToken, newRefreshToken);
                });
            });
            updateDynamicRegistration();
        });
        fetchWellKnown();
    });
    restore();
}

QString OCC::toString(OAuth::PromptValuesSupportedFlags s)
{
    QStringList out;
    for (auto k : {OAuth::PromptValuesSupported::consent, OAuth::PromptValuesSupported::select_account})
        if (s & k) {
            out += Utility::enumToString(k);
        }
    return out.join(QLatin1Char(' '));
}


#include "oauth.moc"
