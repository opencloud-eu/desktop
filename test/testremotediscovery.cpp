/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QtTest>
#include "testutils/syncenginetestutils.h"
#include <syncengine.h>
#include <localdiscoverytracker.h>

using namespace std::chrono_literals;
using namespace Qt::Literals::StringLiterals;
using namespace OCC;

namespace {
// keep in sync with SyncEngine::abort
QString formatAbortError(const QString &error)
{
    return u"Aborted due to: %1"_s.arg(error);
}

class FakeBrokenXmlPropfindReply : public FakePropfindReply
{
    Q_OBJECT
public:
    FakeBrokenXmlPropfindReply(FileInfo &remoteRootFileInfo, QNetworkAccessManager::Operation op,
                               const QNetworkRequest &request, QObject *parent)
        : FakePropfindReply(remoteRootFileInfo, op, request, parent) {
        QVERIFY(payload.size() > 50);
        // turncate the XML
        payload.chop(20);
    }
};

class MissingPermissionsPropfindReply : public FakePropfindReply
{
    Q_OBJECT
public:
    MissingPermissionsPropfindReply(FileInfo &remoteRootFileInfo, QNetworkAccessManager::Operation op,
                               const QNetworkRequest &request, QObject *parent)
        : FakePropfindReply(remoteRootFileInfo, op, request, parent) {
        // If the propfind contains a single file without permissions, this is a server error
        const char toRemove[] = "<oc:permissions>RDNVCKW</oc:permissions>";
        auto pos = payload.indexOf(toRemove, payload.size()/2);
        QVERIFY(pos > 0);
        payload.remove(pos, sizeof(toRemove) - 1);
    }
};

enum ErrorKind : int {
    // Lower code are corresponding to HTML error code
    InvalidXML = 1000,
    Timeout,
};

}

Q_DECLARE_METATYPE(ErrorCategory)

class TestRemoteDiscovery : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase_data()
    {
        QTest::addColumn<Vfs::Mode>("vfsMode");
        QTest::addColumn<bool>("filesAreDehydrated");

        QTest::newRow("Vfs::Mode::Off") << Vfs::Mode::Off << false;

        if (VfsPluginManager::instance().isVfsPluginAvailable(Vfs::Mode::WindowsCfApi)) {
            QTest::newRow("Vfs::Mode::WindowsCfApi dehydrated") << Vfs::Mode::WindowsCfApi << true;
            QTest::newRow("Vfs::Mode::WindowsCfApi hydrated") << Vfs::Mode::WindowsCfApi << false;
        } else if (Utility::isWindows()) {
            qWarning("Skipping Vfs::Mode::WindowsCfApi");
        }
    }

    void testRemoteDiscoveryError_data()
    {
        qRegisterMetaType<ErrorCategory>();
        QTest::addColumn<int>("errorKind");
        QTest::addColumn<QString>("expectedErrorString");
        QTest::addColumn<bool>("syncSucceeds");
        QTest::addColumn<QString>("syncRootError");

        QString itemErrorMessage = "Internal Server Fake Error"_L1;

        QTest::newRow("400") << 400 << itemErrorMessage << false << QString{};
        QTest::newRow("401") << 401 << u"Fake credentials error"_s << false << QString{};
        QTest::newRow("403") << 403 << u"Fake access denied error"_s << true << QString{};
        QTest::newRow("404") << 404 << QString{} << true << u"This Space is currently unavailable"_s;
        QTest::newRow("500") << 500 << itemErrorMessage << true << QString{};
        QTest::newRow("503") << 503 << itemErrorMessage << true << QString{};
        // 200 should be an error since propfind should return 207
        QTest::newRow("200") << 200 << itemErrorMessage << false << QString{};
        QTest::newRow("InvalidXML") << +InvalidXML << "Unknown error" << false << QString{};
        QTest::newRow("Timeout") << +Timeout << "Connection timed out" << false << QString{};
    }


    // Check what happens when there is an error.
    void testRemoteDiscoveryError()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        QFETCH(int, errorKind);
        QFETCH(QString, expectedErrorString);
        QFETCH(bool, syncSucceeds);
        QFETCH(QString, syncRootError);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        // Do Some change as well
        fakeFolder.localModifier().insert("A/z1"_L1);
        fakeFolder.localModifier().insert("B/z1"_L1);
        fakeFolder.localModifier().insert("C/z1"_L1);
        fakeFolder.remoteModifier().insert("A/z2"_L1);
        fakeFolder.remoteModifier().insert("B/z2"_L1);
        fakeFolder.remoteModifier().insert("C/z2"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        auto oldLocalState = fakeFolder.currentLocalState();
        auto oldRemoteState = fakeFolder.currentRemoteState();

        QString errorFolder = Utility::concatUrlPath(OCC::TestUtils::dummyDavUrl(), "B"_L1).path();
        QString fatalErrorPrefix = u"Server replied with an error while reading directory »B«: "_s;
        fakeFolder.setServerOverride([&](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *) -> QNetworkReply * {
            if (req.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray() == "PROPFIND" && req.url().path().endsWith(errorFolder)) {
                if (errorKind == InvalidXML) {
                    return new FakeBrokenXmlPropfindReply(fakeFolder.remoteModifier(), op, req, this);
                } else if (errorKind == Timeout) {
                    return new FakeHangingReply(op, req, this);
                } else if (errorKind < 1000) {
                    return new FakeErrorReply(op, req, this, errorKind);
                }
            }
            return nullptr;
        });

        // So the test that test timeout finishes fast
        QScopedValueRollback<decltype(AbstractNetworkJob::httpTimeout)> setHttpTimeout(AbstractNetworkJob::httpTimeout, errorKind == Timeout ? 50ms : 10000s);

        ItemCompletedSpy completeSpy(fakeFolder);
        QSignalSpy errorSpy(&fakeFolder.syncEngine(), &SyncEngine::syncError);
        QCOMPARE(fakeFolder.applyLocalModificationsAndSync(), syncSucceeds);

        // The folder B should not have been sync'ed (and in particular not removed)
        QCOMPARE(oldLocalState.children["B"_L1], fakeFolder.currentLocalState().children["B"_L1]);
        QCOMPARE(oldRemoteState.children["B"_L1], fakeFolder.currentRemoteState().children["B"_L1]);
        if (!syncSucceeds) {
            QCOMPARE(errorSpy.size(), 1);
            QCOMPARE(errorSpy[0][0].toString(), formatAbortError(fatalErrorPrefix + expectedErrorString));
        } else {
            QCOMPARE(completeSpy.findItem("B"_L1)->instruction(), CSYNC_INSTRUCTION_IGNORE);
            QVERIFY(completeSpy.findItem("B"_L1)->_errorString.contains(expectedErrorString));

            // The other folder should have been sync'ed as the sync just ignored the faulty dir
            QCOMPARE(fakeFolder.currentRemoteState().children["A"_L1], fakeFolder.currentLocalState().children["A"_L1]);
            QCOMPARE(fakeFolder.currentRemoteState().children["C"_L1], fakeFolder.currentLocalState().children["C"_L1]);
            QCOMPARE(completeSpy.findItem("A/z1"_L1)->instruction(), CSYNC_INSTRUCTION_NEW);
        }

        //
        // Check the same discovery error on the sync root
        //
        errorFolder = Utility::ensureTrailingSlash(OCC::TestUtils::dummyDavUrl().path());
        fatalErrorPrefix = u"Server replied with an error while reading directory »/«: "_s;
        errorSpy.clear();
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(errorSpy.size(), 1);
        QCOMPARE(errorSpy[0][0].toString(), formatAbortError(syncRootError.isEmpty() ? QString(fatalErrorPrefix + expectedErrorString) : syncRootError));
    }

    void testMissingData()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo(), vfsMode, filesAreDehydrated);
        fakeFolder.remoteModifier().insert("good"_L1);
        fakeFolder.remoteModifier().insert("noetag"_L1);
        fakeFolder.remoteModifier().find("noetag"_L1)->etag.clear();
        fakeFolder.remoteModifier().insert("nofileid"_L1);
        fakeFolder.remoteModifier().find("nofileid"_L1)->fileId.clear();
        fakeFolder.remoteModifier().mkdir("nopermissions"_L1);
        fakeFolder.remoteModifier().insert("nopermissions/A"_L1);

        fakeFolder.setServerOverride([&](QNetworkAccessManager::Operation op, const QNetworkRequest &req, QIODevice *) -> QNetworkReply * {
            if (req.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray() == "PROPFIND" && req.url().path().endsWith(QLatin1String("nopermissions")))
                return new MissingPermissionsPropfindReply(fakeFolder.remoteModifier(), op, req, this);
            return nullptr;
        });

        ItemCompletedSpy completeSpy(fakeFolder);
        QVERIFY(!fakeFolder.applyLocalModificationsAndSync());

        QCOMPARE(completeSpy.findItem("good"_L1)->instruction(), CSYNC_INSTRUCTION_NEW);
        QCOMPARE(completeSpy.findItem("noetag"_L1)->instruction(), CSYNC_INSTRUCTION_ERROR);
        QCOMPARE(completeSpy.findItem("nofileid"_L1)->instruction(), CSYNC_INSTRUCTION_ERROR);
        QCOMPARE(completeSpy.findItem("nopermissions"_L1)->instruction(), CSYNC_INSTRUCTION_NEW);
        QCOMPARE(completeSpy.findItem("nopermissions/A"_L1)->instruction(), CSYNC_INSTRUCTION_ERROR);
        QVERIFY(completeSpy.findItem("noetag"_L1)->_errorString.contains("etag"_L1));
        QVERIFY(completeSpy.findItem("nofileid"_L1)->_errorString.contains("id"_L1));
        QVERIFY(completeSpy.findItem("nopermissions/A"_L1)->_errorString.contains(QStringLiteral("permissions")));
    }
};

QTEST_GUILESS_MAIN(TestRemoteDiscovery)
#include "testremotediscovery.moc"
