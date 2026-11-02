/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 */

#include "libsync/networkjobs/getfilejob.h"
#include "testutils/syncenginetestutils.h"

#include <QBuffer>
#include <QHostAddress>
#include <QPointer>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <algorithm>
#include <functional>

using namespace OCC;

namespace {

constexpr qint64 DefaultReplyReadBufferSize = 1024 * 1024;
constexpr qint64 PayloadSize = 4 * 1024 * 1024;

QByteArray responseHeaders(int status, const QByteArray &reason, qint64 contentLength, const QByteArray &extraHeaders = {})
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    response += "ETag: \"test-etag\"\r\n";
    response += "OC-ETag: \"test-etag\"\r\n";
    response += extraHeaders;
    response += "Connection: keep-alive\r\n\r\n";
    return response;
}

/**
 * A minimal local HTTP server. The test controls when the response body is
 * sent, so the client can be paused before any payload arrives.
 */
class LocalHttpServer : public QTcpServer
{
public:
    explicit LocalHttpServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                auto request = QSharedPointer<QByteArray>::create();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request] {
                    request->append(socket->readAll());
                    while (true) {
                        const qsizetype headerEnd = request->indexOf("\r\n\r\n");
                        if (headerEnd < 0) {
                            break;
                        }
                        const QByteArray currentRequest = request->left(headerEnd + 4);
                        request->remove(0, headerEnd + 4);
                        if (responder) {
                            responder(socket, currentRequest);
                        }
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    std::function<void(QTcpSocket *, const QByteArray &)> responder;
    QPointer<QTcpSocket> socket;
};

/**
 * Abort and destroy a still-running job before the test's local device and
 * server are destroyed. A finished job may already be scheduled for deletion.
 */
class JobGuard
{
public:
    explicit JobGuard(GETFileJob *job)
        : _job(job)
    {
        Q_ASSERT(_job);
    }

    ~JobGuard()
    {
        if (_job) {
            _job->abort();
            delete _job;
        }
    }

    GETFileJob *operator->() const { return _job; }
    GETFileJob *get() const { return _job; }

private:
    QPointer<GETFileJob> _job;
};

}

class TestGetFileJob : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testReplyBufferIsBoundedWithoutBandwidthManager()
    {
        auto accountState = TestUtils::createDummyAccount();
        auto account = accountState->account();

        LocalHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        account->setUrl(server.baseUrl());

        const QByteArray payload(PayloadSize, 'x');
        server.responder = [&](QTcpSocket *socket, const QByteArray &) {
            server.socket = socket;
            socket->write(responseHeaders(200, "OK", payload.size()));
        };

        QBuffer device;
        QVERIFY(device.open(QIODevice::WriteOnly));

        JobGuard job(new GETFileJob(account, server.baseUrl(), QStringLiteral("/file"), &device, {}, {}, 0, this));
        bool finished = false;
        connect(job.get(), &AbstractNetworkJob::finishedSignal, job.get(), [&] { finished = true; });

        job->start();
        QCOMPARE(job->reply()->readBufferSize(), DefaultReplyReadBufferSize);
        QTRY_VERIFY_WITH_TIMEOUT(server.socket, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200, 5000);

        // Pause the consumer before sending the body. Without a read-buffer cap,
        // Qt now pulls the complete payload into its reply buffer.
        job->setChoked(true);
        server.socket->write(payload);

        QTRY_VERIFY_WITH_TIMEOUT(job->reply()->bytesAvailable() > 0, 5000);
        qint64 maxBuffered = 0;
        for (int i = 0; i < 50; ++i) {
            QTest::qWait(10);
            maxBuffered = std::max(maxBuffered, job->reply()->bytesAvailable());
        }

        QVERIFY2(maxBuffered > 0, "The response body never reached the reply buffer");
        QVERIFY2(maxBuffered <= DefaultReplyReadBufferSize,
            qPrintable(QStringLiteral("Qt buffered %1 bytes, expected at most %2").arg(maxBuffered).arg(DefaultReplyReadBufferSize)));
        QVERIFY(!finished);

        job->setChoked(false);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        QCOMPARE(device.data(), payload);
    }

    void testLargeErrorBodyCompletes()
    {
        auto accountState = TestUtils::createDummyAccount();
        auto account = accountState->account();

        LocalHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        account->setUrl(server.baseUrl());

        const QByteArray marker = "large error marker";
        QByteArray errorBody = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                               "<d:error xmlns:d=\"DAV:\" xmlns:s=\"http://sabredav.org/ns\">\n"
                               "<s:exception>Sabre\\DAV\\Exception\\Forbidden</s:exception>\n"
                               "<s:message>"
            + marker
            + "</s:message>\n"
              "</d:error>\n";
        errorBody.append(QByteArray(2 * 1024 * 1024, ' '));

        server.responder = [&](QTcpSocket *socket, const QByteArray &) {
            socket->write(responseHeaders(400, "Bad Request", errorBody.size()));
            socket->write(errorBody);
        };

        QBuffer device;
        QVERIFY(device.open(QIODevice::WriteOnly));

        JobGuard job(new GETFileJob(account, server.baseUrl(), QStringLiteral("/error"), &device, {}, {}, 0, this));
        bool finished = false;
        int errorBufferSize = -1;
        QString error;
        connect(job.get(), &AbstractNetworkJob::finishedSignal, job.get(), [&] {
            errorBufferSize = job->reply()->readBufferSize();
            error = job->errorStringParsingBody();
            finished = true;
        });

        job->start();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);

        // Error bodies are read at once after the job finished, so the limit must
        // be lifted for them even when no bandwidth manager is involved.
        QCOMPARE(errorBufferSize, 0);
        QVERIFY2(error.contains(QString::fromUtf8(marker)), qPrintable(error));
    }

    void testLargeRedirectBodyCompletes()
    {
        auto accountState = TestUtils::createDummyAccount();
        auto account = accountState->account();

        LocalHttpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        account->setUrl(server.baseUrl());

        const QByteArray redirectBody(3 * 1024 * 1024, 'r');
        server.responder = [&](QTcpSocket *socket, const QByteArray &request) {
            if (!request.startsWith("GET /redirect ")) {
                socket->write(responseHeaders(404, "Not Found", 0));
                return;
            }
            socket->write(responseHeaders(302, "Found", redirectBody.size(), "Location: /redirect\r\n"));
            socket->write(redirectBody);
        };

        QBuffer device;
        QVERIFY(device.open(QIODevice::WriteOnly));

        JobGuard job(new GETFileJob(account, server.baseUrl(), QStringLiteral("/redirect"), &device, {}, {}, 0, this));
        QSignalSpy networkErrorSpy(job.get(), &AbstractNetworkJob::networkError);

        job->start();
        // Reaching networkError means the redirect response body was consumed
        // and AbstractNetworkJob can recover; a capped, undrained body stalls here.
        QVERIFY(networkErrorSpy.wait(5000));
        QCOMPARE(job->reply()->readBufferSize(), 0);
        QCOMPARE(job->reply()->bytesAvailable(), qint64(redirectBody.size()));
    }
};

QTEST_GUILESS_MAIN(TestGetFileJob)
#include "testgetfilejob.moc"
