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

constexpr qint64 ReplyReadBufferSize = 16 * 1024;
constexpr qint64 PayloadSize = 1 * 1024 * 1024;

QByteArray responseHeaders(int status, const QByteArray &reason, qint64 contentLength)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    response += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    response += "ETag: \"test-etag\"\r\n";
    response += "OC-ETag: \"test-etag\"\r\n";
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
                auto responded = QSharedPointer<bool>::create(false);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request, responded] {
                    request->append(socket->readAll());
                    if (*responded || !request->contains("\r\n\r\n")) {
                        return;
                    }
                    *responded = true;
                    if (responder) {
                        responder(socket);
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    std::function<void(QTcpSocket *)> responder;
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
        server.responder = [&](QTcpSocket *socket) {
            server.socket = socket;
            socket->write(responseHeaders(200, "OK", payload.size()));
        };

        QBuffer device;
        QVERIFY(device.open(QIODevice::WriteOnly));

        JobGuard job(new GETFileJob(account, server.baseUrl(), QStringLiteral("/file"), &device, {}, {}, 0, this));
        bool finished = false;
        connect(job.get(), &AbstractNetworkJob::finishedSignal, job.get(), [&] { finished = true; });

        job->start();
        QCOMPARE(job->reply()->readBufferSize(), ReplyReadBufferSize);
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
        QVERIFY2(maxBuffered <= ReplyReadBufferSize,
            qPrintable(QStringLiteral("Qt buffered %1 bytes, expected at most %2").arg(maxBuffered).arg(ReplyReadBufferSize)));
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
        errorBody.append(QByteArray(64 * 1024, ' '));

        server.responder = [&](QTcpSocket *socket) {
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
};

QTEST_GUILESS_MAIN(TestGetFileJob)
#include "testgetfilejob.moc"
