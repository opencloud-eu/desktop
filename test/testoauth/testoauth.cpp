/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QDesktopServices>
#include <QtTest/QtTest>

#include "common/asserts.h"
#include "libsync/creds/oauth.h"
#include "testutils/syncenginetestutils.h"
#include "theme.h"

using namespace std::chrono_literals;
using namespace OCC;
namespace {
class DesktopServiceHook : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void hooked(const QUrl &);

public:
    DesktopServiceHook() { QDesktopServices::setUrlHandler(QStringLiteral("oauthtest"), this, "hooked"); }
    ~DesktopServiceHook() { QDesktopServices::unsetUrlHandler(QStringLiteral("oauthtest")); }
};

const QUrl sOAuthTestServer(QStringLiteral("oauthtest://someserver/opencloud"));
const QString localHost{QStringLiteral("127.0.0.1")};
}

class FakePostReply : public QNetworkReply
{
    Q_OBJECT
public:
    std::unique_ptr<QIODevice> payload;
    bool aborted = false;

    FakePostReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, std::unique_ptr<QIODevice> payload_, QObject *parent)
        : QNetworkReply{parent}
        , payload{std::move(payload_)}
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        open(QIODevice::ReadOnly);
        payload->open(QIODevice::ReadOnly);
        QMetaObject::invokeMethod(this, &FakePostReply::respond, Qt::QueuedConnection);
    }

    Q_INVOKABLE virtual void respond()
    {
        if (aborted) {
            setError(OperationCanceledError, QStringLiteral("Operation Canceled"));
            Q_EMIT metaDataChanged();
            checkedFinished();
            return;
        }
        setHeader(QNetworkRequest::ContentLengthHeader, payload->size());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        Q_EMIT metaDataChanged();
        if (bytesAvailable())
            Q_EMIT readyRead();
        checkedFinished();
    }

    void abort() override { aborted = true; }

    void checkedFinished()
    {
        if (!isFinished()) {
            setFinished(true);
            Q_EMIT finished();
        }
    }

    qint64 bytesAvailable() const override
    {
        if (aborted)
            return 0;
        return payload->bytesAvailable();
    }

    qint64 readData(char *data, qint64 maxlen) override { return payload->read(data, maxlen); }
};

// Reply with a small delay
class SlowFakePostReply : public FakePostReply
{
    Q_OBJECT
public:
    using FakePostReply::FakePostReply;
    void respond() override
    {
        // override of FakePostReply::respond, will call the real one with a delay.
        QTimer::singleShot(100ms, this, [this] { this->FakePostReply::respond(); });
    }
};


class OAuthTestCase : public QObject
{
    Q_OBJECT
    DesktopServiceHook desktopServiceHook;

protected:
    QString _expectedClientId = Theme::instance()->oauthClientId();

public:
    enum State { StartState, StatusPhpState, BrowserOpened, TokenAsked, CustomState } state = StartState;
    Q_ENUM(State);

    bool replyToBrowserOk = false;
    bool gotAuthOk = false;
    virtual bool done() const { return replyToBrowserOk && gotAuthOk; }

    FakeAM *fakeAm = nullptr;
    QNetworkAccessManager realQNAM;
    QPointer<QNetworkReply> browserReply = nullptr;
    QString code = generateEtag();
    OCC::AccountPtr account;

    std::unique_ptr<AccountBasedOAuth> oauth;

    virtual std::unique_ptr<AccountBasedOAuth> prepareOauth()
    {
        fakeAm = new FakeAM({}, nullptr);
        account = Account::create(QUuid::createUuid());
        account->setUrl(sOAuthTestServer);
        // the account seizes ownership over the qnam in account->setCredentials(...) by keeping a shared pointer on it
        // therefore, we should never call fakeAm->setThis(...)
        account->setCredentials(new FakeCredentials{fakeAm});
        fakeAm->setOverride([this](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *device) {
            if (req.url().path().endsWith(QLatin1String(".well-known/openid-configuration"))) {
                return this->wellKnownReply(op, req);
            } else if (req.url().path().endsWith(QLatin1String("status.php"))) {
                return this->statusPhpReply(op, req);
            } else if (req.url().path().endsWith(QLatin1String("clients-registrations"))) {
                return this->clientRegistrationReply(op, req);
            }
            OC_ASSERT(device);
            OC_ASSERT(device && device->bytesAvailable() > 0); // OAuth2 always sends around POST data.
            return this->tokenReply(op, req, device);
        });

        QObject::connect(&desktopServiceHook, &DesktopServiceHook::hooked, this, &OAuthTestCase::openBrowserHook);

        auto out = std::make_unique<AccountBasedOAuth>(account);
        QObject::connect(out.get(), &OAuth::result, this, &OAuthTestCase::oauthResult);
        return out;
    }

    virtual void test()
    {
        oauth = prepareOauth();
        oauth->startAuthentication();

        QSignalSpy spy(oauth.get(), &OCC::OAuth::authorisationLinkChanged);
        if (spy.wait()) {
            oauth->openBrowser();
        }

        QTRY_VERIFY(done());
    }

    virtual void openBrowserHook(const QUrl &url)
    {
        QCOMPARE(state, StatusPhpState);
        state = BrowserOpened;
        QCOMPARE(url.path(), sOAuthTestServer.path() + QStringLiteral("/index.php/apps/oauth2/authorize"));
        QVERIFY(url.toString().startsWith(sOAuthTestServer.toString()));
        QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("response_type")), QLatin1String("code"));
        QCOMPARE(query.queryItemValue(QStringLiteral("client_id")), _expectedClientId);
        QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri")));
        QCOMPARE(redirectUri.host(), localHost);
        redirectUri.setQuery(QStringLiteral("code=%1&state=%2").arg(code, query.queryItemValue(QStringLiteral("state"))));
        createBrowserReply(QNetworkRequest(redirectUri));
    }

    virtual QNetworkReply *createBrowserReply(const QNetworkRequest &request)
    {
        auto r = request;
        // don't follow the redirect to opencloud://success
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        browserReply = realQNAM.get(r);
        QObject::connect(browserReply, &QNetworkReply::finished, this, &OAuthTestCase::browserReplyFinished);
        return browserReply;
    }

    virtual void browserReplyFinished()
    {
        QCOMPARE(sender(), browserReply.data());
        QCOMPARE(state, TokenAsked);
        browserReply->deleteLater();
        QCOMPARE(QNetworkReply::NoError, browserReply->error());
        QCOMPARE(browserReply->rawHeader("Location"), QByteArray("opencloud://success"));
        replyToBrowserOk = true;
    }

    virtual QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req, [[maybe_unused]] QIODevice *device)
    {
        OC_ASSERT(state == BrowserOpened);
        state = TokenAsked;
        OC_ASSERT(op == QNetworkAccessManager::PostOperation);
        OC_ASSERT(req.url().toString().startsWith(sOAuthTestServer.toString()));
        OC_ASSERT(req.url().path() == sOAuthTestServer.path() + QStringLiteral("/token_endpoint"));
        auto payload = std::make_unique<QBuffer>();
        payload->setData(tokenReplyPayload());
        return new FakePostReply(op, req, std::move(payload), fakeAm);
    }

    virtual QNetworkReply *statusPhpReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        OC_ASSERT(state == StartState);
        state = StatusPhpState;
        OC_ASSERT(op == QNetworkAccessManager::GetOperation);
        OC_ASSERT(req.url().toString().startsWith(sOAuthTestServer.toString()));
        OC_ASSERT(req.url().path() == sOAuthTestServer.path() + QStringLiteral("/status.php"));
        auto payload = std::make_unique<QBuffer>();
        payload->setData(statusPhpPayload());
        return new FakePostReply(op, req, std::move(payload), fakeAm);
    }

    virtual QNetworkReply *wellKnownReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        OC_ASSERT(op == QNetworkAccessManager::GetOperation);
        QJsonDocument jsondata(QJsonObject{
            {QStringLiteral("authorization_endpoint"),
                QJsonValue(Utility::concatUrlPath(sOAuthTestServer, QStringLiteral("/index.php/apps/oauth2/authorize")).toString())},
            {QStringLiteral("token_endpoint"), Utility::concatUrlPath(sOAuthTestServer, QStringLiteral("token_endpoint")).toString()},
            {QStringLiteral("token_endpoint_auth_methods_supported"), QJsonArray{QStringLiteral("client_secret_post")}},
        });
        return new FakePayloadReply(op, req, jsondata.toJson(), fakeAm);
    }

    virtual QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        return new FakeErrorReply(op, req, fakeAm, 404, {});
    }

    virtual QByteArray tokenReplyPayload() const
    {
        // the dummy server provides the user admin
        QJsonDocument jsondata(QJsonObject{{QStringLiteral("access_token"), QStringLiteral("123")}, {QStringLiteral("refresh_token"), QStringLiteral("456")},
            {QStringLiteral("message_url"), QStringLiteral("opencloud://success")}, {QStringLiteral("id_token"), idToken()},
            {QStringLiteral("token_type"), QStringLiteral("Bearer")}});
        return jsondata.toJson();
    }

    virtual QByteArray statusPhpPayload() const
    {
        QJsonDocument jsondata(
            QJsonObject{{QStringLiteral("installed"), true}, {QStringLiteral("maintenance"), false}, {QStringLiteral("needsDbUpgrade"), false},
                {QStringLiteral("version"), QStringLiteral("10.5.0.10")}, {QStringLiteral("versionstring"), QStringLiteral("10.5.0")},
                {QStringLiteral("edition"), QStringLiteral("Enterprise")}, {QStringLiteral("productname"), QStringLiteral("OpenCloud")}});
        return jsondata.toJson();
    }

    virtual QString idToken() const
    {
        /* https://10015.io/tools/jwt-encoder-decoder with sample key
        {
          "amr": [
            "pwd",
            "pop",
            "hwk",
            "user",
            "pin",
            "mfa"
          ],
          "at_hash": "jEL4ptHeYx4eQa847tOVoQ",
          "aud": [
            "OpenCloudDesktop"
          ],
          "auth_time": 1737560752,
          "azp": "OpenCloudDesktop",
          "client_id": "OpenCloudDesktop",
          "email": "admin@admin.admin",
          "email_verified": true,
          "exp": 1739884152,
          "iat": 1739880552,
          "iss": "oauthtest://someserver/opencloud",
          "jti": "e2db5f2d-6bcc-42d7-a20f-46955d7ab6b4",
          "name": "Admin",
          "preferred_username": "admin",
          "sub": "f4a04b62-e17a-4a98-bcc6-63345ded5a25"
        }
         */
        return QStringLiteral(
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJhbXIiOlsicHdkIiwicG9wIiwiaHdrIiwidXNlciIsInBpbiIsIm1mYSJdLCJhdF9oYXNoIjoiakVMNHB0SGVZeDRlUWE4NDd0T1ZvUSIsImF1ZCI6WyJPcGVuQ2xvdWREZXNrdG9wIl0sIm"
            "F1dGhfdGltZSI6MTczNzU2MDc1MiwiYXpwIjoiT3BlbkNsb3VkRGVza3RvcCIsImNsaWVudF9pZCI6Ik9wZW5DbG91ZERlc2t0b3AiLCJlbWFpbCI6ImFkbWluQGFkbWluLmFkbWluIiwiZW1h"
            "aWxfdmVyaWZpZWQiOnRydWUsImV4cCI6MTczOTg4NDE1MiwiaWF0IjoxNzM5ODgwNTUyLCJpc3MiOiJvYXV0aHRlc3Q6Ly9zb21lc2VydmVyL29wZW5jbG91ZCIsImp0aSI6ImUyZGI1ZjJkLT"
            "ZiY2MtNDJkNy1hMjBmLTQ2OTU1ZDdhYjZiNCIsIm5hbWUiOiJBZG1pbiIsInByZWZlcnJlZF91c2VybmFtZSI6ImFkbWluIiwic3ViIjoiZjRhMDRiNjItZTE3YS00YTk4LWJjYzYtNjMzNDVk"
            "ZWQ1YTI1In0.wj3NyKWaDhWWwui6lxGdmJEGUyqCsNYCRJFTbgIUeC4");
    }

    virtual void oauthResult(OAuth::Result result, const QString &token, const QString &refreshToken)
    {
        QCOMPARE(result, OAuth::LoggedIn);
        QCOMPARE(state, TokenAsked);
        QCOMPARE(token, QStringLiteral("123"));
        QCOMPARE(refreshToken, QStringLiteral("456"));
        gotAuthOk = true;
    }
};

class TestOAuth : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBasic()
    {
        OAuthTestCase test;
        test.test();
    }


    void testWrongUser()
    {
        struct Test : OAuthTestCase
        {
            QByteArray tokenReplyPayload() const override
            {
                // the dummy server provides the user admin
                QJsonDocument jsondata(QJsonObject{{QStringLiteral("access_token"), QStringLiteral("123")},
                    {QStringLiteral("refresh_token"), QStringLiteral("456")}, {QStringLiteral("message_url"), QStringLiteral("OpenCloud://success")},
                    {QStringLiteral("user_id"), QStringLiteral("wrong_user")}, {QStringLiteral("token_type"), QStringLiteral("Bearer")}});
                return jsondata.toJson();
            }

            void browserReplyFinished() override
            {
                QCOMPARE(sender(), browserReply.data());
                QCOMPARE(state, TokenAsked);
                browserReply->deleteLater();
                QCOMPARE(QNetworkReply::AuthenticationRequiredError, browserReply->error());
            }

            bool done() const override { return true; }
        };
        Test test;
        test.test();
    }

    // Test for https://github.com/owncloud/client/pull/6057
    void testCloseBrowserDontCrash()
    {
        struct Test : OAuthTestCase
        {
            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req, [[maybe_unused]] QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                // simulate the fact that the browser is closing the connection
                browserReply->abort();

                OC_ASSERT(state == BrowserOpened);
                state = TokenAsked;

                auto payload = std::make_unique<QBuffer>();
                payload->setData(tokenReplyPayload());
                return new SlowFakePostReply(op, req, std::move(payload), fakeAm);
            }

            void browserReplyFinished() override
            {
                QCOMPARE(sender(), browserReply.data());
                QCOMPARE(browserReply->error(), QNetworkReply::OperationCanceledError);
                replyToBrowserOk = true;
            }
        } test;
        test.test();
    }

    void testRandomConnections()
    {
        // Test that we can send random garbage to the litening socket and it does not prevent the connection
        struct Test : OAuthTestCase
        {
            QNetworkReply *createBrowserReply(const QNetworkRequest &request) override
            {
                QTimer::singleShot(0, this, [this, request] {
                    auto port = request.url().port();
                    state = CustomState;
                    const QVector<QByteArray> payloads = {
                        "GET FOFOFO HTTP 1/1\n\n",
                        "GET /?code=invalie HTTP 1/1\n\n",
                        "GET /?code=xxxxx&bar=fff",
                        QByteArray("\0\0\0", 3),
                        QByteArray("GET \0\0\0 \n\n\n\n\n\0", 14),
                        QByteArray("GET /?code=éléphant\xa5 HTTP\n"),
                        QByteArray("\n\n\n\n"),
                    };
                    for (const auto &x : payloads) {
                        auto socket = new QTcpSocket(this);
                        socket->connectToHost(QStringLiteral("localhost"), port);
                        QVERIFY(socket->waitForConnected());
                        socket->write(x);
                    }

                    // Do the actual request a bit later
                    QTimer::singleShot(100ms, this, [this, request] {
                        QCOMPARE(state, CustomState);
                        state = BrowserOpened;
                        this->OAuthTestCase::createBrowserReply(request);
                    });
                });
                return nullptr;
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *device) override
            {
                if (state == CustomState)
                    return new FakeErrorReply{op, req, this, 500};
                return OAuthTestCase::tokenReply(op, req, device);
            }

            void oauthResult(OAuth::Result result, const QString &token, const QString &refreshToken) override
            {
                if (state != CustomState) {
                    return OAuthTestCase::oauthResult(result, token, refreshToken);
                }
                QCOMPARE(result, OAuth::Error);
            }
        } test;
        test.test();
    }


    void testTimeout()
    {
        struct Test : OAuthTestCase
        {
            QScopedValueRollback<std::chrono::seconds> rollback;

            Test()
                : rollback(AbstractNetworkJob::httpTimeout, 1s)
            {
            }

            QNetworkReply *statusPhpReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                return new FakeHangingReply(op, req, fakeAm);
            }

            void oauthResult(OAuth::Result result, const QString &token, const QString &refreshToken) override
            {
                Q_UNUSED(token);
                Q_UNUSED(refreshToken);

                QCOMPARE(state, StartState);
                QCOMPARE(result, OAuth::Error);
                gotAuthOk = true;
                replyToBrowserOk = true;
            }
        } test;
        test.test();
    }

    void testDynamicRegistrationFailFallback()
    {
        // similar to testWellKnown but the server announces dynamic client registration
        // when this fails we fall back to the default client id and secret
        struct Test : OAuthTestCase
        {
            Test() { }

            QNetworkReply *wellKnownReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {QStringLiteral("authorization_endpoint"),
                        QJsonValue(QStringLiteral("oauthtest://openidserver") + sOAuthTestServer.path() + QStringLiteral("/index.php/apps/oauth2/authorize"))},
                    {QStringLiteral("token_endpoint"), QStringLiteral("oauthtest://openidserver/token_endpoint")},
                    {QStringLiteral("registration_endpoint"), QStringLiteral("%1/clients-registrations").arg(localHost)},
                    {QStringLiteral("token_endpoint_auth_methods_supported"), QJsonArray{QStringLiteral("client_secret_basic")}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), fakeAm);
            }

            void openBrowserHook(const QUrl &url) override
            {
                OC_ASSERT(url.host() == QStringLiteral("openidserver"));
                QUrl url2 = url;
                url2.setHost(sOAuthTestServer.host());
                OAuthTestCase::openBrowserHook(url2);
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                OC_ASSERT(request.url().toString().startsWith(QStringLiteral("oauthtest://openidserver/token_endpoint")));
                auto req = request;
                qDebug() << request.url() << request.url().query();
                req.setUrl(QUrl(request.url().toString().replace(
                    QLatin1String("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + QStringLiteral("/token_endpoint"))));
                return OAuthTestCase::tokenReply(op, req, device);
            }
        } test;
        test.test();
    }

    void testDynamicRegistrationFailFallback2()
    {
        // similar to testWellKnown but the server announces dynamic client registration
        // when this fails we fall back to the default client id and secret
        struct Test : OAuthTestCase
        {
            Test() { }

            QNetworkReply *wellKnownReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {QStringLiteral("authorization_endpoint"),
                        QJsonValue(QStringLiteral("oauthtest://openidserver") + sOAuthTestServer.path() + QStringLiteral("/index.php/apps/oauth2/authorize"))},
                    {QStringLiteral("token_endpoint"), QStringLiteral("oauthtest://openidserver/token_endpoint")},
                    {QStringLiteral("registration_endpoint"), QStringLiteral("%1/clients-registrations").arg(localHost)},
                    {QStringLiteral("token_endpoint_auth_methods_supported"),
                        QJsonArray{QStringLiteral("client_secret_basic"), QStringLiteral("client_secret_post")}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), fakeAm);
            }

            void openBrowserHook(const QUrl &url) override
            {
                OC_ASSERT(url.host() == QStringLiteral("openidserver"));
                QUrl url2 = url;
                url2.setHost(sOAuthTestServer.host());
                OAuthTestCase::openBrowserHook(url2);
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                OC_ASSERT(request.url().toString().startsWith(QStringLiteral("oauthtest://openidserver/token_endpoint")));
                auto req = request;
                qDebug() << request.url() << request.url().query();
                req.setUrl(QUrl(request.url().toString().replace(
                    QLatin1String("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + QStringLiteral("/token_endpoint"))));
                return OAuthTestCase::tokenReply(op, req, device);
            }

            QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request) override
            {
                return new FakePayloadReply(op, request, {}, fakeAm);
            }

        } test;
        test.test();
    }

    void testDynamicRegistration()
    {
        // similar to testWellKnown but the server announces dynamic client registration
        // this means that the client id and secret are provided by the server
        struct Test : OAuthTestCase
        {
            Test() { _expectedClientId = QStringLiteral("3e4ea0f3-59ea-434a-92f2-b0d3b54443e9"); }

            QNetworkReply *wellKnownReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {QStringLiteral("authorization_endpoint"),
                        QJsonValue(QStringLiteral("oauthtest://openidserver") + sOAuthTestServer.path() + QStringLiteral("/index.php/apps/oauth2/authorize"))},
                    {QStringLiteral("token_endpoint"), QStringLiteral("oauthtest://openidserver/token_endpoint")},
                    {QStringLiteral("registration_endpoint"), QStringLiteral("%1/clients-registrations").arg(localHost)},
                    {QStringLiteral("token_endpoint_auth_methods_supported"), QJsonArray{QStringLiteral("client_secret_basic")}},

                });
                return new FakePayloadReply(op, req, jsondata.toJson(), fakeAm);
            }

            void openBrowserHook(const QUrl &url) override
            {
                OC_ASSERT(url.host() == QStringLiteral("openidserver"));
                QUrl url2 = url;
                url2.setHost(sOAuthTestServer.host());
                OAuthTestCase::openBrowserHook(url2);
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                OC_ASSERT(request.url().toString().startsWith(QStringLiteral("oauthtest://openidserver/token_endpoint")));
                auto req = request;
                qDebug() << request.url() << request.url().query();
                req.setUrl(QUrl(request.url().toString().replace(
                    QStringLiteral("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + QStringLiteral("/token_endpoint"))));
                return OAuthTestCase::tokenReply(op, req, device);
            }

            QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request) override
            {
                const QByteArray payload(QByteArrayLiteral(
                    "{\"redirect_uris\":[\"http://"
                    "127.0.0.1\"],\"token_endpoint_auth_method\":\"client_secret_basic\",\"grant_types\":[\"authorization_code\",\"refresh_token\"],\"response_"
                    "types\":[\"code\",\"none\"],\"client_id\":\"3e4ea0f3-59ea-434a-92f2-b0d3b54443e9\",\"client_secret\":\"rmoEXFc1Z5tGTApxanBW7STlWODqRTYx\","
                    "\"client_name\":\"OpenCloud 3.0.0.0\",\"scope\":\"web-origins address phone offline_access "
                    "microprofile-jwt\",\"subject_type\":\"public\",\"request_uris\":[],\"tls_client_certificate_bound_access_tokens\":false,\"client_id_"
                    "issued_at\":1663074650,\"client_secret_expires_at\":0,\"registration_client_uri\":\"https://someserver.de/auth/realms/opencloud/"
                    "clients-registrations/openid-connect/"
                    "3e4ea0f3-59ea-434a-92f2-b0d3b54443e9\",\"registration_access_token\":"
                    "\"eyJhbGciOiJIUzI1NiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICIzYjQ2YWVkYi00Y2I3LTRiMGItODA5Ny1lNjRmOGQ5ZWY2YjQifQ."
                    "eyJleHAiOjAsImlhdCI6MTY2MzA3NDY1MCwianRpIjoiNTlkZWIzNTktNTBmZS00YTUyLWFmNTItZjFjNDg3ZTFlOWRmIiwiaXNzIjoiaHR0cHM6Ly9rZXljbG9hay5vd25jbG91ZC"
                    "5jbG91ZHNwZWljaGVyLWJheWVybi5kZS9hdXRoL3JlYWxtcy9vY2lzIiwiYXVkIjoiaHR0cHM6Ly9rZXljbG9hay5vd25jbG91ZC5jbG91ZHNwZWljaGVyLWJheWVybi5kZS9hdXRo"
                    "L3JlYWxtcy9vY2lzIiwidHlwIjoiUmVnaXN0cmF0aW9uQWNjZXNzVG9rZW4iLCJyZWdpc3RyYXRpb25fYXV0aCI6ImFub255bW91cyJ9."
                    "v1giSvpnKw1hTtBYZaqdp3JqnZ5mvCKYhQDKkT7x8Us\",\"backchannel_logout_session_required\":false,\"require_pushed_authorization_requests\":"
                    "false}"));

                auto *out = new FakePayloadReply(op, request, payload, fakeAm);
                out->setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 201);
                return out;
            }

            QString idToken() const override
            {
                // same as the parent implementation but  with the current client id
                return QStringLiteral(
                    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                    "eyJhbXIiOlsicHdkIiwicG9wIiwiaHdrIiwidXNlciIsInBpbiIsIm1mYSJdLCJhdF9oYXNoIjoiakVMNHB0SGVZeDRlUWE4NDd0T1ZvUSIsImF1ZCI6WyIzZTRlYTBmMy01OWVhLT"
                    "QzNGEtOTJmMi1iMGQzYjU0NDQzZTkiXSwiYXV0aF90aW1lIjoxNzM3NTYwNzUyLCJhenAiOiJPcGVuQ2xvdWREZXNrdG9wIiwiY2xpZW50X2lkIjoiT3BlbkNsb3VkRGVza3RvcCIs"
                    "ImVtYWlsIjoiYWRtaW5AYWRtaW4uYWRtaW4iLCJlbWFpbF92ZXJpZmllZCI6dHJ1ZSwiZXhwIjoxNzM5ODg0MTUyLCJpYXQiOjE3Mzk4ODA1NTIsImlzcyI6Im9hdXRodGVzdDovL3"
                    "NvbWVzZXJ2ZXIvb3BlbmNsb3VkIiwianRpIjoiZTJkYjVmMmQtNmJjYy00MmQ3LWEyMGYtNDY5NTVkN2FiNmI0IiwibmFtZSI6IkFkbWluIiwicHJlZmVycmVkX3VzZXJuYW1lIjoi"
                    "YWRtaW4iLCJzdWIiOiJmNGEwNGI2Mi1lMTdhLTRhOTgtYmNjNi02MzM0NWRlZDVhMjUifQ.UVjqXnuHFiu2iIPOW8qXze_a8tVMk03kuxoN4FKxhoY");
            }

        } test;
        test.test();
    }


    void testDynamicTokenRefresh()
    {
        // simulate a token refresh with dynamic registration
        struct Test : OAuthTestCase
        {
            QString _expectedClientSecret = QStringLiteral("rmoEXFc1Z5tGTApxanBW7STlWODqRTYx");
            Test() { _expectedClientId = QStringLiteral("3e4ea0f3-59ea-434a-92f2-b0d3b54443e9"); }

            QNetworkReply *wellKnownReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {QStringLiteral("authorization_endpoint"),
                        QJsonValue(QStringLiteral("oauthtest://openidserver") + sOAuthTestServer.path() + QStringLiteral("/index.php/apps/oauth2/authorize"))},
                    {QStringLiteral("token_endpoint"), QStringLiteral("oauthtest://openidserver/token_endpoint")},
                    {QStringLiteral("registration_endpoint"), QStringLiteral("%1/clients-registrations").arg(localHost)},
                    // this test explicitly check for the client secret in the post body
                    {QStringLiteral("token_endpoint_auth_methods_supported"), QJsonArray{QStringLiteral("client_secret_post")}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), fakeAm);
            }

            void openBrowserHook(const QUrl &) override { Q_UNREACHABLE(); }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(request.url().toString().startsWith(QStringLiteral("oauthtest://openidserver/token_endpoint")));
                auto req = request;
                const auto query = QUrlQuery(QString::fromUtf8(device->peek(device->size())));
                OC_ASSERT(query.queryItemValue(QStringLiteral("refresh_token")) == QLatin1String("foo"));

                qDebug() << request.url() << request.url().query() << device->peek(device->size());
                req.setUrl(QUrl(request.url().toString().replace(
                    QStringLiteral("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + QStringLiteral("/token_endpoint"))));

                // OAuthTestCase::tokenReply expects BrowserOpened
                state = BrowserOpened;

                return OAuthTestCase::tokenReply(op, req, device);
            }

            QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request) override
            {
                auto *out = new FakePayloadReply(op, request, TestUtils::getPayload("testDynamicTokenRefresh/clientRegistrationReply.json"), fakeAm);
                out->setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 201);
                return out;
            }

            virtual void test() override
            {
                oauth = prepareOauth();
                QSignalSpy spy(oauth.get(), &OCC::AccountBasedOAuth::refreshFinished);
                oauth->refreshAuthentication(QStringLiteral("foo"));
                QVERIFY(spy.wait());
                QCOMPARE(oauth->clientId(), QStringLiteral("d8a5d1f4-dabc-4e6a-a9a9-da729cff8ab0"));
                QCOMPARE(oauth->clientSecret(), QString());
            }

        } test;
        test.test();
    }


    void testDynamicTokenRefreshFailingRegistration()
    {
        // similar to testDynamicTokenRefresh but the dynamic registration fails and we fall back to the defaul
        // client id and secret
        struct Test : OAuthTestCase
        {
            Test() { }

            QNetworkReply *wellKnownReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {QStringLiteral("authorization_endpoint"),
                        QJsonValue(QStringLiteral("oauthtest://openidserver") + sOAuthTestServer.path() + QStringLiteral("/index.php/apps/oauth2/authorize"))},
                    {QStringLiteral("token_endpoint"), QStringLiteral("oauthtest://openidserver/token_endpoint")},
                    {QStringLiteral("registration_endpoint"), QStringLiteral("%1/clients-registrations").arg(localHost)},
                    // this test explicitly check for the client secret in the post body
                    {QStringLiteral("token_endpoint_auth_methods_supported"), QJsonArray{QStringLiteral("client_secret_post")}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), fakeAm);
            }

            void openBrowserHook(const QUrl &) override { Q_UNREACHABLE(); }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(request.url().toString().startsWith(QStringLiteral("oauthtest://openidserver/token_endpoint")));
                auto req = request;
                const auto query = QUrlQuery(QString::fromUtf8(device->peek(device->size())));
                OC_ASSERT(query.queryItemValue(QStringLiteral("refresh_token")) == QLatin1String("foo"));
                OC_ASSERT(query.queryItemValue(QStringLiteral("client_id")) == _expectedClientId);
                OC_ASSERT(query.queryItemValue(QStringLiteral("client_secret")) == Theme::instance()->oauthClientSecret());
                req.setUrl(QUrl(request.url().toString().replace(
                    QStringLiteral("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + QStringLiteral("/token_endpoint"))));

                // OAuthTestCase::tokenReply expects BrowserOpened
                state = BrowserOpened;

                return OAuthTestCase::tokenReply(op, req, device);
            }

            QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request) override
            {
                return new FakePayloadReply(op, request, {}, fakeAm);
            }

            virtual void test() override
            {
                oauth = prepareOauth();
                QSignalSpy spy(oauth.get(), &OCC::AccountBasedOAuth::refreshFinished);
                oauth->refreshAuthentication(QStringLiteral("foo"));

                QVERIFY(spy.wait());
            }

        } test;
        test.test();
    }
};


QTEST_MAIN(TestOAuth)
#include "testoauth.moc"
