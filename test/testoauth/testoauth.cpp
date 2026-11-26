/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QDesktopServices>

#include "common/asserts.h"
#include "libsync/creds/httpcredentials.h"
#include "libsync/creds/oauth.h"
#include "testutils/syncenginetestutils.h"
#include "theme.h"

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;
using namespace OCC;
namespace {
class DesktopServiceHook : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void hooked(const QUrl &);

public:
    DesktopServiceHook() { QDesktopServices::setUrlHandler("oauthtest"_L1, this, "hooked"); }
    ~DesktopServiceHook() { QDesktopServices::unsetUrlHandler("oauthtest"_L1); }
};

const QUrl sOAuthTestServer("oauthtest://someserver/opencloud"_L1);
const QString localHost{"127.0.0.1"_L1};
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
            setError(OperationCanceledError, "Operation Canceled"_L1);
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

    virtual QUrl serverUrl() const { return sOAuthTestServer; }

    virtual std::unique_ptr<AccountBasedOAuth> prepareOauth()
    {
        fakeAm = new FakeAM({}, nullptr);
        account = Account::create(QUuid::createUuid());
        account->setUrl(serverUrl());
        // the account seizes ownership over the qnam in account->setCredentials(...) by keeping a shared pointer on it
        // therefore, we should never call fakeAm->setThis(...)
        account->setCredentials(new FakeCredentials{fakeAm});
        fakeAm->setOverride([this](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *device) {
            if (req.url().path().endsWith(QLatin1String(".well-known/openid-configuration"))) {
                return this->wellKnownOpenidConfigurationReply(op, req);
            } else if (req.url().path().endsWith(QLatin1String(".well-known/webfinger"))) {
                return this->wellKnownWebfingerReply(op, req);
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
        QCOMPARE(url.path(), sOAuthTestServer.path() + "/index.php/apps/oauth2/authorize"_L1);
        QVERIFY(url.toString().startsWith(sOAuthTestServer.toString()));
        QUrlQuery query(url);
        QCOMPARE(query.queryItemValue("response_type"_L1), QLatin1String("code"));
        QCOMPARE(query.queryItemValue("client_id"_L1), _expectedClientId);
        QUrl redirectUri(query.queryItemValue("redirect_uri"_L1));
        QCOMPARE(redirectUri.host(), localHost);
        redirectUri.setQuery("code=%1&state=%2"_L1.arg(code, query.queryItemValue("state"_L1)));
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
        OC_ASSERT(req.url().path() == sOAuthTestServer.path() + "/token_endpoint"_L1);
        auto payload = std::make_unique<QBuffer>();
        payload->setData(tokenReplyPayload());
        return new FakePostReply(op, req, std::move(payload), fakeAm);
    }

    virtual QNetworkReply *statusPhpReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        OC_ASSERT(state == StartState);
        state = StatusPhpState;
        OC_ASSERT(op == QNetworkAccessManager::GetOperation);
        OC_ASSERT(req.url().toString().startsWith(serverUrl().toString()));
        OC_ASSERT(req.url().path() == serverUrl().path() + "/status.php"_L1);
        auto payload = std::make_unique<QBuffer>();
        payload->setData(statusPhpPayload());
        return new FakePostReply(op, req, std::move(payload), fakeAm);
    }

    virtual QNetworkReply *wellKnownOpenidConfigurationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        OC_ASSERT(op == QNetworkAccessManager::GetOperation);
        QJsonDocument jsondata(QJsonObject{
            {"authorization_endpoint"_L1, QJsonValue(Utility::concatUrlPath(sOAuthTestServer, "/index.php/apps/oauth2/authorize"_L1).toString())},
            {"token_endpoint"_L1, Utility::concatUrlPath(sOAuthTestServer, "token_endpoint"_L1).toString()},
            {"token_endpoint_auth_methods_supported"_L1, QJsonArray{"client_secret_post"_L1}},
        });
        return new FakePayloadReply(op, req, jsondata.toJson(), {}, fakeAm);
    }

    virtual QNetworkReply *wellKnownWebfingerReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        OC_ASSERT(op == QNetworkAccessManager::GetOperation);
        QJsonDocument jsondata(QJsonObject{
            {"subject"_L1, sOAuthTestServer.toString()},
            {"links"_L1,
                QJsonArray{QJsonObject{
                    {"rel"_L1, "http://openid.net/specs/connect/1.0/issuer"_L1},
                    {"href"_L1, "oauthtest://openidserver"_L1},
                }}},
        });
        QHttpHeaders headers;
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json"_L1);

        return new FakePayloadReply(op, req, jsondata.toJson(), headers, fakeAm);
    }

    virtual QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req)
    {
        return new FakeErrorReply(op, req, fakeAm, 404, {});
    }

    virtual QByteArray tokenReplyPayload() const
    {
        // the dummy server provides the user admin
        QJsonDocument jsondata(QJsonObject{{"access_token"_L1, "123"_L1}, {"refresh_token"_L1, "456"_L1}, {"message_url"_L1, "opencloud://success"_L1},
            {"id_token"_L1, idToken()}, {"token_type"_L1, "Bearer"_L1}});
        return jsondata.toJson();
    }

    virtual QByteArray statusPhpPayload() const
    {
        QJsonDocument jsondata(QJsonObject{{"installed"_L1, true}, {"maintenance"_L1, false}, {"needsDbUpgrade"_L1, false}, {"version"_L1, "10.5.0.10"_L1},
            {"versionstring"_L1, "10.5.0"_L1}, {"edition"_L1, "Enterprise"_L1}, {"productname"_L1, "OpenCloud"_L1}});
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
        QCOMPARE(token, "123"_L1);
        QCOMPARE(refreshToken, "456"_L1);
        gotAuthOk = true;
    }
};

class TestHttpCredentials : public HttpCredentials
{
public:
    TestHttpCredentials(FakeAM::Override override, const QString &refreshToken)
        : HttpCredentials("initial-access-token"_L1)
        , _override(std::move(override))
    {
        _refreshToken = refreshToken;
    }

    OCC::AccessManager *createAM() const override
    {
        auto *am = new FakeAM({}, nullptr);
        am->setOverride(_override);
        return am;
    }

    void restartOauth() override { }
    void fetchFromKeychain() override { }
    void persist() override { }

    QString refreshToken() const { return _refreshToken; }
    bool refreshRetryPending() const { return _refreshRetryTimer.isActive(); }
    void setReachability(QNetworkInformation::Reachability reachability) { slotReachabilityChanged(reachability); }
    void setBehindCaptivePortal(bool behindCaptivePortal) { slotCaptivePortalChanged(behindCaptivePortal); }

private:
    FakeAM::Override _override;
};

struct RefreshServer
{
    using ReplyFactory = std::function<QNetworkReply *(QNetworkAccessManager::Operation, const QNetworkRequest &, QObject *)>;

    QObject replyParent;
    int statusRequests = 0;
    int tokenRequests = 0;

    ReplyFactory statusReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
        const QJsonDocument json(QJsonObject{
            {"installed"_L1, true},
            {"maintenance"_L1, false},
            {"version"_L1, "10.0.0"_L1},
        });
        return new FakePayloadReply(op, req, json.toJson(), {}, parent);
    };

    ReplyFactory webfingerReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
        const QJsonDocument json(QJsonObject{
            {"subject"_L1, sOAuthTestServer.toString()},
            {"links"_L1,
                QJsonArray{
                    QJsonObject{
                        {"rel"_L1, "http://openid.net/specs/connect/1.0/issuer"_L1},
                        {"href"_L1, "oauthtest://openidserver"_L1},
                    },
                }},
        });
        QHttpHeaders headers;
        headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json"_L1);
        return new FakePayloadReply(op, req, json.toJson(), headers, parent);
    };

    ReplyFactory openidConfigurationReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
        const QJsonDocument json(QJsonObject{
            {"authorization_endpoint"_L1, "oauthtest://openidserver/authorize"_L1},
            {"token_endpoint"_L1, "oauthtest://openidserver/token_endpoint"_L1},
        });
        return new FakePayloadReply(op, req, json.toJson(), {}, parent);
    };

    ReplyFactory tokenReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
        const QJsonDocument json(QJsonObject{{"access_token"_L1, "123"_L1}, {"token_type"_L1, "Bearer"_L1}});
        return new FakePayloadReply(op, req, json.toJson(), {}, parent);
    };

    FakeAM::Override override()
    {
        return [this](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *) -> QNetworkReply * {
            const QString path = req.url().path();
            if (path.endsWith(QLatin1String("status.php"))) {
                ++statusRequests;
                return statusReply(op, req, &replyParent);
            }
            if (path.endsWith(QLatin1String(".well-known/openid-configuration"))) {
                return openidConfigurationReply(op, req, &replyParent);
            }
            if (path.endsWith(QLatin1String(".well-known/webfinger"))) {
                return webfingerReply(op, req, &replyParent);
            }
            if (req.url().toString().contains(QLatin1String("token_endpoint"))) {
                ++tokenRequests;
                return tokenReply(op, req, &replyParent);
            }
            return new FakePayloadReply(op, req, {}, {}, &replyParent);
        };
    }
};
Q_DECLARE_METATYPE(RefreshServer::ReplyFactory)


class TestOAuth : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBasic()
    {
        // Initial-auth success path: only result(LoggedIn) fires.
        struct Test : OAuthTestCase
        {
            std::unique_ptr<QSignalSpy> refreshErrorSpy;
            std::unique_ptr<QSignalSpy> refreshFinishedSpy;

            std::unique_ptr<AccountBasedOAuth> prepareOauth() override
            {
                auto out = OAuthTestCase::prepareOauth();
                refreshErrorSpy = std::make_unique<QSignalSpy>(out.get(), &OCC::AccountBasedOAuth::refreshError);
                refreshFinishedSpy = std::make_unique<QSignalSpy>(out.get(), &OCC::AccountBasedOAuth::refreshFinished);
                return out;
            }
        } test;
        test.test();
        QCOMPARE(test.refreshErrorSpy->count(), 0);
        QCOMPARE(test.refreshFinishedSpy->count(), 0);
    }


    void testWrongUser()
    {
        struct Test : OAuthTestCase
        {
            QByteArray tokenReplyPayload() const override
            {
                // the dummy server provides the user admin
                QJsonDocument jsondata(QJsonObject{{"access_token"_L1, "123"_L1}, {"refresh_token"_L1, "456"_L1}, {"message_url"_L1, "OpenCloud://success"_L1},
                    {"user_id"_L1, "wrong_user"_L1}, {"token_type"_L1, "Bearer"_L1}});
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

    void testUnparseableIdToken()
    {
        struct Test : OAuthTestCase
        {
            int corruptSegment = 0;

            QString idToken() const override
            {
                // corrupt one segment with a character that is not valid base64url
                QStringList parts = OAuthTestCase::idToken().split(QLatin1Char('.'));
                parts[corruptSegment].insert(4, QLatin1Char('!'));
                return parts.join(QLatin1Char('.'));
            }

            void browserReplyFinished() override
            {
                QCOMPARE(sender(), browserReply.data());
                QCOMPARE(state, TokenAsked);
                QCOMPARE(browserReply->error(), QNetworkReply::InternalServerError);
                QVERIFY(QString::fromUtf8(browserReply->readAll()).contains("could not be parsed"_L1));
                browserReply->deleteLater();
                replyToBrowserOk = true;
            }

            void oauthResult(OAuth::Result result, const QString &, const QString &) override
            {
                QCOMPARE(result, OAuth::Error);
                gotAuthOk = true;
            }
        };
        // a corrupted header (segment 0) and a corrupted payload (segment 1) must both be rejected
        for (int segment : {0, 1}) {
            Test test;
            test.corruptSegment = segment;
            test.test();
        }
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
                        socket->connectToHost("localhost"_L1, port);
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
        // Inverse of testRefreshWebFingerError: an error on the initial-auth
        // flow must emit result(Error) only, never refreshError or refreshFinished.
        struct Test : OAuthTestCase
        {
            QScopedValueRollback<decltype(AbstractNetworkJob::httpTimeout)> rollback;
            std::unique_ptr<QSignalSpy> refreshErrorSpy;
            std::unique_ptr<QSignalSpy> refreshFinishedSpy;

            Test()
                : rollback(AbstractNetworkJob::httpTimeout, 50ms)
            {
            }

            std::unique_ptr<AccountBasedOAuth> prepareOauth() override
            {
                auto out = OAuthTestCase::prepareOauth();
                refreshErrorSpy = std::make_unique<QSignalSpy>(out.get(), &OCC::AccountBasedOAuth::refreshError);
                refreshFinishedSpy = std::make_unique<QSignalSpy>(out.get(), &OCC::AccountBasedOAuth::refreshFinished);
                return out;
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
        QCOMPARE(test.refreshErrorSpy->count(), 0);
        QCOMPARE(test.refreshFinishedSpy->count(), 0);
    }

    void testDynamicRegistrationFailFallback()
    {
        // similar to testWellKnown but the server announces dynamic client registration
        // when this fails we fall back to the default client id and secret
        struct Test : OAuthTestCase
        {
            Test() { }

            QNetworkReply *wellKnownOpenidConfigurationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {"authorization_endpoint"_L1, QJsonValue("oauthtest://openidserver"_L1 + sOAuthTestServer.path() + "/index.php/apps/oauth2/authorize"_L1)},
                    {"token_endpoint"_L1, "oauthtest://openidserver/token_endpoint"_L1},
                    {"registration_endpoint"_L1, "%1/clients-registrations"_L1.arg(localHost)},
                    {"token_endpoint_auth_methods_supported"_L1, QJsonArray{"client_secret_basic"_L1}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), {}, fakeAm);
            }

            void openBrowserHook(const QUrl &url) override
            {
                OC_ASSERT(url.host() == "openidserver"_L1);
                QUrl url2 = url;
                url2.setHost(sOAuthTestServer.host());
                OAuthTestCase::openBrowserHook(url2);
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                OC_ASSERT(request.url().toString().startsWith("oauthtest://openidserver/token_endpoint"_L1));
                auto req = request;
                qDebug() << request.url() << request.url().query();
                req.setUrl(QUrl(request.url().toString().replace(
                    QLatin1String("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + "/token_endpoint"_L1)));
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

            QNetworkReply *wellKnownOpenidConfigurationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {"authorization_endpoint"_L1, QJsonValue("oauthtest://openidserver"_L1 + sOAuthTestServer.path() + "/index.php/apps/oauth2/authorize"_L1)},
                    {"token_endpoint"_L1, "oauthtest://openidserver/token_endpoint"_L1},
                    {"registration_endpoint"_L1, "%1/clients-registrations"_L1.arg(localHost)},
                    {"token_endpoint_auth_methods_supported"_L1, QJsonArray{"client_secret_basic"_L1, "client_secret_post"_L1}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), {}, fakeAm);
            }

            void openBrowserHook(const QUrl &url) override
            {
                OC_ASSERT(url.host() == "openidserver"_L1);
                QUrl url2 = url;
                url2.setHost(sOAuthTestServer.host());
                OAuthTestCase::openBrowserHook(url2);
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                OC_ASSERT(request.url().toString().startsWith("oauthtest://openidserver/token_endpoint"_L1));
                auto req = request;
                qDebug() << request.url() << request.url().query();
                req.setUrl(QUrl(request.url().toString().replace(
                    QLatin1String("oauthtest://openidserver/token_endpoint"), sOAuthTestServer.toString() + "/token_endpoint"_L1)));
                return OAuthTestCase::tokenReply(op, req, device);
            }

            QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request) override
            {
                return new FakePayloadReply(op, request, {}, {}, fakeAm);
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
            Test() { _expectedClientId = "3e4ea0f3-59ea-434a-92f2-b0d3b54443e9"_L1; }

            QNetworkReply *wellKnownOpenidConfigurationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {"authorization_endpoint"_L1, QJsonValue("oauthtest://openidserver"_L1 + sOAuthTestServer.path() + "/index.php/apps/oauth2/authorize"_L1)},
                    {"token_endpoint"_L1, "oauthtest://openidserver/token_endpoint"_L1},
                    {"registration_endpoint"_L1, "%1/clients-registrations"_L1.arg(localHost)},
                    {"token_endpoint_auth_methods_supported"_L1, QJsonArray{"client_secret_basic"_L1}},

                });
                return new FakePayloadReply(op, req, jsondata.toJson(), {}, fakeAm);
            }

            void openBrowserHook(const QUrl &url) override
            {
                OC_ASSERT(url.host() == "openidserver"_L1);
                QUrl url2 = url;
                url2.setHost(sOAuthTestServer.host());
                OAuthTestCase::openBrowserHook(url2);
            }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(browserReply);
                OC_ASSERT(request.url().toString().startsWith("oauthtest://openidserver/token_endpoint"_L1));
                auto req = request;
                qDebug() << request.url() << request.url().query();
                req.setUrl(
                    QUrl(request.url().toString().replace("oauthtest://openidserver/token_endpoint"_L1, sOAuthTestServer.toString() + "/token_endpoint"_L1)));
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

                auto *out = new FakePayloadReply(op, request, payload, {}, fakeAm);
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


    void testTokenRefreshDoesNotRegisterClient()
    {
        // The refresh token is bound to the client used during the login.
        // Without stored registration data the login used the default client, so a refresh must not register a new one.
        struct Test : OAuthTestCase
        {
            bool registrationRequested = false;

            QNetworkReply *wellKnownOpenidConfigurationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                const QJsonDocument jsondata(QJsonObject{
                    {"authorization_endpoint"_L1, QJsonValue("oauthtest://openidserver"_L1 + sOAuthTestServer.path() + "/index.php/apps/oauth2/authorize"_L1)},
                    {"token_endpoint"_L1, "oauthtest://openidserver/token_endpoint"_L1},
                    {"registration_endpoint"_L1, "%1/clients-registrations"_L1.arg(localHost)},
                    {"token_endpoint_auth_methods_supported"_L1, QJsonArray{"client_secret_post"_L1}},
                });
                return new FakePayloadReply(op, req, jsondata.toJson(), {}, fakeAm);
            }

            void openBrowserHook(const QUrl &) override { Q_UNREACHABLE(); }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) override
            {
                OC_ASSERT(request.url().toString().startsWith("oauthtest://openidserver/token_endpoint"_L1));
                auto req = request;
                const auto query = QUrlQuery(QString::fromUtf8(device->peek(device->size())));
                OC_ASSERT(query.queryItemValue("refresh_token"_L1) == QLatin1String("foo"));
                OC_ASSERT(query.queryItemValue("client_id"_L1) == Theme::instance()->oauthClientId());
                OC_ASSERT(query.queryItemValue("client_secret"_L1) == Theme::instance()->oauthClientSecret());
                req.setUrl(
                    QUrl(request.url().toString().replace("oauthtest://openidserver/token_endpoint"_L1, sOAuthTestServer.toString() + "/token_endpoint"_L1)));

                // OAuthTestCase::tokenReply expects BrowserOpened
                state = BrowserOpened;

                return OAuthTestCase::tokenReply(op, req, device);
            }

            QNetworkReply *clientRegistrationReply(QNetworkAccessManager::Operation op, const QNetworkRequest &request) override
            {
                registrationRequested = true;
                return OAuthTestCase::clientRegistrationReply(op, request);
            }

            void test() override
            {
                oauth = prepareOauth();
                QSignalSpy spy(oauth.get(), &OCC::AccountBasedOAuth::refreshFinished);
                QSignalSpy refreshErrorSpy(oauth.get(), &OCC::AccountBasedOAuth::refreshError);
                QSignalSpy resultSpy(oauth.get(), &OCC::OAuth::result);
                oauth->refreshAuthentication("foo"_L1);
                QVERIFY(spy.wait());
                QCOMPARE(spy.first().at(0).toString(), "123"_L1);
                QCOMPARE(spy.first().at(1).toString(), "456"_L1);
                QCOMPARE(oauth->clientId(), Theme::instance()->oauthClientId());
                QVERIFY(!registrationRequested);
                QCOMPARE(refreshErrorSpy.count(), 0);
                QCOMPARE(resultSpy.count(), 0);
            }
        } test;
        test.test();
    }

    void testRefreshWebFingerError()
    {
        // A failing WebFinger reply during refresh must emit refreshError
        // (and only refreshError), otherwise HttpCredentials hangs.
        struct Test : OAuthTestCase
        {
            Test() { }

            QNetworkReply *wellKnownWebfingerReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                return new FakeErrorReply(op, req, fakeAm, 500, {});
            }

            void openBrowserHook(const QUrl &) override { Q_UNREACHABLE(); }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation, const QNetworkRequest &, QIODevice *) override { Q_UNREACHABLE(); }

            void oauthResult(OAuth::Result, const QString &, const QString &) override { Q_UNREACHABLE(); }

            virtual void test() override
            {
                oauth = prepareOauth();
                QSignalSpy refreshErrorSpy(oauth.get(), &OCC::AccountBasedOAuth::refreshError);
                QSignalSpy refreshFinishedSpy(oauth.get(), &OCC::AccountBasedOAuth::refreshFinished);
                QSignalSpy resultSpy(oauth.get(), &OCC::OAuth::result);
                oauth->refreshAuthentication("foo"_L1);
                QVERIFY(refreshErrorSpy.wait());
                QCOMPARE(refreshErrorSpy.count(), 1);
                QCOMPARE(refreshFinishedSpy.count(), 0);
                QCOMPARE(resultSpy.count(), 0);
            }
        } test;
        test.test();
    }

    void testInitialAuthWebFingerError()
    {
        // True mirror of testRefreshWebFingerError on the initial-auth flow:
        // a failing WebFinger reply during startAuthentication must emit
        // result(Error) and never refreshError or refreshFinished. Exercises
        // the same gated branches in OAuth::fetchWellKnown with _isRefreshingToken
        // false.
        struct Test : OAuthTestCase
        {
            std::unique_ptr<QSignalSpy> refreshErrorSpy;
            std::unique_ptr<QSignalSpy> refreshFinishedSpy;

            Test() { }

            std::unique_ptr<AccountBasedOAuth> prepareOauth() override
            {
                auto out = OAuthTestCase::prepareOauth();
                refreshErrorSpy = std::make_unique<QSignalSpy>(out.get(), &OCC::AccountBasedOAuth::refreshError);
                refreshFinishedSpy = std::make_unique<QSignalSpy>(out.get(), &OCC::AccountBasedOAuth::refreshFinished);
                return out;
            }

            QNetworkReply *wellKnownWebfingerReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                return new FakeErrorReply(op, req, fakeAm, 500, {});
            }

            void openBrowserHook(const QUrl &) override { Q_UNREACHABLE(); }

            QNetworkReply *tokenReply(QNetworkAccessManager::Operation, const QNetworkRequest &, QIODevice *) override { Q_UNREACHABLE(); }

            void oauthResult(OAuth::Result result, const QString &, const QString &) override
            {
                QCOMPARE(state, StatusPhpState);
                QCOMPARE(result, OAuth::Error);
                gotAuthOk = true;
                replyToBrowserOk = true;
            }
        } test;
        test.test();
        QCOMPARE(test.refreshErrorSpy->count(), 0);
        QCOMPARE(test.refreshFinishedSpy->count(), 0);
    }

    void testInternationalizedDomainName_data()
    {
        QTest::addColumn<QString>("serverHost");
        QTest::addColumn<bool>("unicodeHost");
        QTest::addColumn<QString>("subjectHost");
        QTest::addColumn<bool>("accepted");

        QTest::newRow("example.com: punycode server, url-encoded subject") << u"xn--bcher-kva.example.com"_s << true << u"b%C3%BCcher.example.com"_s << true;
        QTest::newRow("example.com: punycode server, unicode subject") << u"xn--bcher-kva.example.com"_s << true << u"b\u00FCcher.example.com"_s << true;
        QTest::newRow("example.com: punycode server, punycode subject") << u"xn--bcher-kva.example.com"_s << true << u"xn--bcher-kva.example.com"_s << true;
        QTest::newRow("example.com: punycode server, uppercase subject") << u"xn--bcher-kva.example.com"_s << true << u"B\u00DCCHER.example.com"_s << true;
        QTest::newRow("example.com: punycode server, decomposed subject") << u"xn--bcher-kva.example.com"_s << true << u"bu\u0308cher.example.com"_s << true;
        QTest::newRow("example.com: punycode server, different host subject") << u"xn--bcher-kva.example.com"_s << true << u"bucher.example.com"_s << false;
        QTest::newRow("example.com: punycode server, different tld subject") << u"xn--bcher-kva.example.com"_s << true << u"xn--bcher-kva.example"_s << false;
        QTest::newRow("example.com: unicode server, url-encoded subject") << u"b\u00FCcher.example.com"_s << true << u"b%C3%BCcher.example.com"_s << true;
        QTest::newRow("example.com: unicode server, unicode subject") << u"b\u00FCcher.example.com"_s << true << u"b\u00FCcher.example.com"_s << true;
        QTest::newRow("example.com: unicode server, punycode subject") << u"b\u00FCcher.example.com"_s << true << u"xn--bcher-kva.example.com"_s << true;

        QTest::newRow("example: punycode server, url-encoded subject") << u"xn--bcher-kva.example"_s << false << u"b%C3%BCcher.example"_s << true;
        QTest::newRow("example: punycode server, unicode subject") << u"xn--bcher-kva.example"_s << false << u"b\u00FCcher.example"_s << true;
        QTest::newRow("example: punycode server, punycode subject") << u"xn--bcher-kva.example"_s << false << u"xn--bcher-kva.example"_s << true;
        QTest::newRow("example: punycode server, uppercase subject") << u"xn--bcher-kva.example"_s << false << u"B\u00DCCHER.example"_s << true;
        QTest::newRow("example: punycode server, decomposed subject") << u"xn--bcher-kva.example"_s << false << u"bu\u0308cher.example"_s << true;
        QTest::newRow("example: punycode server, different host subject") << u"xn--bcher-kva.example"_s << false << u"bucher.example"_s << false;
        QTest::newRow("example: punycode server, different tld subject") << u"xn--bcher-kva.example"_s << false << u"xn--bcher-kva.example.com"_s << false;
        QTest::newRow("example: unicode server, url-encoded subject") << u"b\u00FCcher.example"_s << false << u"b%C3%BCcher.example"_s << true;
        QTest::newRow("example: unicode server, unicode subject") << u"b\u00FCcher.example"_s << false << u"b\u00FCcher.example"_s << true;
        QTest::newRow("example: unicode server, punycode subject") << u"b\u00FCcher.example"_s << false << u"xn--bcher-kva.example"_s << true;

        QTest::newRow("example.com: sharp s server, punycode subject") << u"stra\u00DFe.example.com"_s << true << u"xn--strae-oqa.example.com"_s << true;
        QTest::newRow("example.com: sharp s server, ss subject") << u"stra\u00DFe.example.com"_s << true << u"strasse.example.com"_s << false;
    }

    void testInternationalizedDomainName()
    {
        QFETCH(QString, serverHost);
        QFETCH(bool, unicodeHost);
        QFETCH(QString, subjectHost);
        QFETCH(bool, accepted);

        const QUrl serverUrl(u"oauthtest://%1/opencloud"_s.arg(serverHost));
        const QString subject = u"oauthtest://%1/opencloud"_s.arg(subjectHost);
        QCOMPARE(serverUrl.host().contains(u"xn--"_s), !unicodeHost);

        struct Test : OAuthTestCase
        {
            QUrl _serverUrl;
            QString _subject;
            QString _resource;

            QUrl serverUrl() const override { return _serverUrl; }

            void oauthResult(OAuth::Result, const QString &, const QString &) override { }

            QNetworkReply *wellKnownWebfingerReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req) override
            {
                OC_ASSERT(op == QNetworkAccessManager::GetOperation);
                _resource = QUrlQuery(req.url()).queryItemValue("resource"_L1);
                QJsonDocument jsondata(QJsonObject{
                    {"subject"_L1, _subject},
                    {"links"_L1,
                        QJsonArray{QJsonObject{
                            {"rel"_L1, "http://openid.net/specs/connect/1.0/issuer"_L1},
                            {"href"_L1, "oauthtest://openidserver"_L1},
                        }}},
                });
                QHttpHeaders headers;
                headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json"_L1);

                return new FakePayloadReply(op, req, jsondata.toJson(), headers, fakeAm);
            }
        } test;
        test._serverUrl = serverUrl;
        test._subject = subject;

        test.oauth = test.prepareOauth();
        QSignalSpy resultSpy(test.oauth.get(), &OCC::OAuth::result);
        QSignalSpy authorisationLinkSpy(test.oauth.get(), &OCC::OAuth::authorisationLinkChanged);
        test.oauth->startAuthentication();
        if (accepted) {
            QVERIFY(authorisationLinkSpy.wait());
            QCOMPARE(resultSpy.count(), 0);
        } else {
            QVERIFY(resultSpy.wait());
            QCOMPARE(resultSpy.first().first().value<OCC::OAuth::Result>(), OCC::OAuth::Error);
            QCOMPARE(authorisationLinkSpy.count(), 0);
        }
        QVERIFY(test._resource.section(QLatin1Char('/'), 2, 2).contains("xn--"_L1));
        QVERIFY(Utility::urlEqual(QUrl(test._resource), serverUrl));
    }

    void testTransientRefreshDoesNotEmitAuthFailed()
    {
        RefreshServer server;
        server.tokenReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
            auto *reply = new FakeErrorReply(op, req, parent, 408);
            reply->setError(QNetworkReply::TimeoutError, "Request timed out"_L1);
            return reply;
        };

        auto account = Account::create(QUuid::createUuid());
        account->setUrl(sOAuthTestServer);
        auto *creds = new TestHttpCredentials(server.override(), "fake-refresh-token"_L1);
        account->setCredentials(creds);

        QSignalSpy authFailedSpy(creds, &AbstractCredentials::authenticationFailed);
        QSignalSpy authStartedSpy(creds, &AbstractCredentials::authenticationStarted);

        QVERIFY(creds->refreshAccessToken());

        // the retry is scheduled in TokenRefreshDefaultTimeoutOneError (30s), so only one attempt happens in this window
        QTRY_VERIFY_WITH_TIMEOUT(server.tokenRequests >= 1, 5000);
        QCOMPARE(authStartedSpy.count(), 1);
        QCOMPARE(authFailedSpy.count(), 0);
    }

    void testInvalidGrantLogsOut()
    {
        RefreshServer server;
        server.tokenReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
            const QJsonDocument json(QJsonObject{
                {"error"_L1, "invalid_grant"_L1},
                {"error_description"_L1, "Token is not active"_L1},
            });
            return new FakeErrorReply(op, req, parent, 400, json.toJson());
        };

        auto account = Account::create(QUuid::createUuid());
        account->setUrl(sOAuthTestServer);
        auto *creds = new TestHttpCredentials(server.override(), "fake-refresh-token"_L1);
        account->setCredentials(creds);

        QSignalSpy authFailedSpy(creds, &AbstractCredentials::authenticationFailed);
        QSignalSpy fetchedSpy(creds, &AbstractCredentials::fetched);

        QVERIFY(creds->refreshAccessToken());

        QTRY_VERIFY_WITH_TIMEOUT(authFailedSpy.count() == 1, 5000);
        QCOMPARE(fetchedSpy.count(), 1);
        QCOMPARE(server.tokenRequests, 1);
        QVERIFY(creds->refreshToken().isEmpty());
    }

    void testRetryRefreshOnReconnect()
    {
        RefreshServer server;
        server.tokenReply = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
            return new FakeErrorReply(op, req, parent, 503);
        };

        auto account = Account::create(QUuid::createUuid());
        account->setUrl(sOAuthTestServer);
        auto *creds = new TestHttpCredentials(server.override(), "fake-refresh-token"_L1);
        account->setCredentials(creds);

        QVERIFY(creds->refreshAccessToken());
        QTRY_VERIFY(creds->refreshRetryPending());
        QCOMPARE(server.tokenRequests, 1);

        creds->setReachability(QNetworkInformation::Reachability::Disconnected);
        creds->setReachability(QNetworkInformation::Reachability::Local);
        creds->setBehindCaptivePortal(true);
        QTest::qWait(100ms);
        QCOMPARE(server.tokenRequests, 1);
        QVERIFY(creds->refreshRetryPending());

        creds->setReachability(QNetworkInformation::Reachability::Online);
        QVERIFY(!creds->refreshRetryPending());
        QTRY_COMPARE(server.tokenRequests, 2);
        QTRY_VERIFY(creds->refreshRetryPending());

        creds->setBehindCaptivePortal(false);
        QTRY_COMPARE(server.tokenRequests, 3);
        QTRY_VERIFY(creds->refreshRetryPending());

        creds->forgetSensitiveData();
        QVERIFY(!creds->refreshRetryPending());
        creds->setReachability(QNetworkInformation::Reachability::Online);
        QTest::qWait(100ms);
        QCOMPARE(server.tokenRequests, 3);
    }

    void testPersistentErrorsDontLogOut_data()
    {
        using ReplyFactory = RefreshServer::ReplyFactory;
        QTest::addColumn<QString>("endpoint");
        QTest::addColumn<ReplyFactory>("reply");

        const auto error = [](int code) -> ReplyFactory {
            return [code](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
                return new FakeErrorReply(op, req, parent, code);
            };
        };
        const auto oauthError = [](const QString &errorCode) -> ReplyFactory {
            return [errorCode](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
                return new FakeErrorReply(op, req, parent, 400, QJsonDocument(QJsonObject{{"error"_L1, errorCode}}).toJson());
            };
        };
        const auto html = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
            QHttpHeaders headers;
            headers.append(QHttpHeaders::WellKnownHeader::ContentType, "text/html"_L1);
            return new FakePayloadReply(op, req, QByteArrayLiteral("<html><body>Please log in to the hotspot</body></html>"), headers, parent);
        };
        const auto noAccessToken = [](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QObject *parent) -> QNetworkReply * {
            return new FakePayloadReply(op, req, QJsonDocument(QJsonObject{{"token_type"_L1, "Bearer"_L1}}).toJson(), {}, parent);
        };

        QTest::newRow("status.php 503") << u"status"_s << error(503);
        QTest::newRow("status.php captive portal") << u"status"_s << ReplyFactory(html);
        QTest::newRow("webfinger 502") << u"webfinger"_s << error(502);
        QTest::newRow("webfinger captive portal") << u"webfinger"_s << ReplyFactory(html);
        QTest::newRow("openid-configuration 500") << u"openid"_s << error(500);
        QTest::newRow("token 404") << u"token"_s << error(404);
        QTest::newRow("token 503") << u"token"_s << error(503);
        QTest::newRow("token captive portal") << u"token"_s << ReplyFactory(html);
        QTest::newRow("token without access_token") << u"token"_s << ReplyFactory(noAccessToken);
        QTest::newRow("token invalid_request") << u"token"_s << oauthError(u"invalid_request"_s);
        QTest::newRow("token temporarily_unavailable") << u"token"_s << oauthError(u"temporarily_unavailable"_s);
        QTest::newRow("token server_error") << u"token"_s << oauthError(u"server_error"_s);
    }

    void testPersistentErrorsDontLogOut()
    {
        QFETCH(QString, endpoint);
        QFETCH(RefreshServer::ReplyFactory, reply);

        const auto oldTimeout = HttpCredentials::TokenRefreshDefaultTimeoutOneError;
        HttpCredentials::TokenRefreshDefaultTimeoutOneError = 0s;
        const auto restoreTimeout = qScopeGuard([oldTimeout] { HttpCredentials::TokenRefreshDefaultTimeoutOneError = oldTimeout; });

        RefreshServer server;
        if (endpoint == QLatin1String("status")) {
            server.statusReply = reply;
        } else if (endpoint == QLatin1String("webfinger")) {
            server.webfingerReply = reply;
        } else if (endpoint == QLatin1String("openid")) {
            server.openidConfigurationReply = reply;
        } else {
            server.tokenReply = reply;
        }

        auto account = Account::create(QUuid::createUuid());
        account->setUrl(sOAuthTestServer);
        auto *creds = new TestHttpCredentials(server.override(), "fake-refresh-token"_L1);
        account->setCredentials(creds);

        QSignalSpy authFailedSpy(creds, &AbstractCredentials::authenticationFailed);
        QSignalSpy fetchedSpy(creds, &AbstractCredentials::fetched);

        QVERIFY(creds->refreshAccessToken());

        // the old implementation gave up after 3 attempts
        QTRY_VERIFY_WITH_TIMEOUT(server.statusRequests >= 6, 10000);
        QCOMPARE(authFailedSpy.count(), 0);
        QCOMPARE(fetchedSpy.count(), 0);
        QCOMPARE(creds->refreshToken(), "fake-refresh-token"_L1);
    }
};


QTEST_MAIN(TestOAuth)
#include "testoauth.moc"
