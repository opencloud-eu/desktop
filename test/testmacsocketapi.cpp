/* This software is in the public domain, furnished "as is", without warranty. */

#include "gui/socketapi/socketapisocket_mac.h"

#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

class TestMacSocketApi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testFramingAndConnectedServerDestruction()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("socket"));
        auto server = std::make_unique<SocketApiServer>();
        QVERIFY(server->listen(path));
        QSignalSpy connections(server.get(), &SocketApiServer::newConnection);
        QLocalSocket client;
        client.connectToServer(path);
        QVERIFY(client.waitForConnected());
        QTRY_COMPARE(connections.size(), 1);
        auto *socket = server->nextPendingConnection();
        QVERIFY(socket);
        client.write("FIRST\nSEC");
        client.flush();
        QTRY_VERIFY(socket->canReadLine());
        QCOMPARE(socket->readLine(), QByteArray("FIRST\n"));
        QVERIFY(!socket->canReadLine());
        client.write("OND\n");
        client.flush();
        QTRY_VERIFY(socket->canReadLine());
        QCOMPARE(socket->readLine(), QByteArray("SECOND\n"));

        // The server and its wrappers must not delete the underlying socket twice.
        server.reset();
        QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
    }
};

QTEST_GUILESS_MAIN(TestMacSocketApi)
#include "testmacsocketapi.moc"
