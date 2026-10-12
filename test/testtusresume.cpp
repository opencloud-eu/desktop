/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */
#include "owncloudpropagator_p.h"
#include "testutils/syncenginetestutils.h"
#include "testutils/testutils.h"

#include <QTest>

using namespace OCC;
using namespace std::chrono_literals;

namespace {

// A minimal tus server on top of the fake remote: creation-with-upload POST, PATCH and HEAD.
// Completed uploads are written to the fake remote tree.
class FakeTusServer
{
public:
    struct Upload
    {
        QString path;
        qint64 length = 0;
        qint64 mtime = 0;
        QByteArray data;
    };

    // called before a PATCH is applied; returning a status != 0 fails the request with it
    std::function<int(const QString &id, Upload &upload, qint64 newSize)> patchHook;

    QHash<QString, Upload> uploads;
    QByteArrayList requests;
    // the status for requests to uploads the server does not know (404, or 403 for an expired transfer token)
    int unknownUploadStatus = 404;
    // when false, the final response lacks ETag and OC-Perm, so the client has to PROPFIND the file (opencloud-eu/desktop#1165)
    bool sendFinalHeaders = true;
    // remote path -> status for failing PROPFINDs on that path
    QHash<QString, int> propfindErrors;

    explicit FakeTusServer(FakeFolder &folder)
        : _folder(folder)
    {
        auto cap = TestUtils::testCapabilities();
        cap.insert(QStringLiteral("files"),
            QVariantMap{{QStringLiteral("tus_support"),
                QVariantMap{{QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("resumable"), QStringLiteral("1.0.0")},
                    {QStringLiteral("extension"), QStringLiteral("creation,creation-with-upload")}, {QStringLiteral("max_chunk_size"), 1000}}}});
        _folder.account()->setCapabilities({_folder.account()->url(), cap});
        _folder.setServerOverride(
            [this](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device) { return handle(op, request, device); });
    }

private:
    QNetworkReply *handle(QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *device)
    {
        QByteArray verb = request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
        if (verb.isEmpty()) {
            switch (op) {
            case QNetworkAccessManager::HeadOperation:
                verb = "HEAD";
                break;
            case QNetworkAccessManager::PostOperation:
                verb = "POST";
                break;
            default:
                break;
            }
        }
        const QString path = request.url().path();
        if (verb == "PROPFIND") {
            for (auto it = propfindErrors.cbegin(); it != propfindErrors.cend(); ++it) {
                if (path.endsWith(QLatin1Char('/') + it.key())) {
                    requests.append(verb);
                    return new FakeErrorReply(op, request, &_folder, it.value());
                }
            }
            return nullptr;
        }
        if (verb == "POST" && request.hasRawHeader("Upload-Length")) {
            requests.append(verb);
            Upload upload;
            upload.length = request.rawHeader("Upload-Length").toLongLong();
            const auto metadata = request.rawHeader("Upload-Metadata").split(',');
            for (const auto &entry : metadata) {
                const auto kv = entry.split(' ');
                const auto value = QByteArray::fromBase64(kv.value(1));
                if (kv.value(0) == "filename") {
                    upload.path = QString::fromUtf8(value).mid(1);
                } else if (kv.value(0) == "mtime") {
                    upload.mtime = value.toLongLong();
                }
            }
            upload.data = device->readAll();
            const QString id = QString::number(++_nextId);
            uploads.insert(id, upload);
            return respond(op, request, id);
        }
        if (!path.contains(QLatin1String("/tus/"))) {
            return nullptr;
        }
        requests.append(verb);
        const QString id = path.section(QLatin1Char('/'), -1);
        if (!uploads.contains(id)) {
            return new FakeErrorReply(op, request, &_folder, unknownUploadStatus);
        }
        if (verb == "PATCH") {
            auto &upload = uploads[id];
            if (request.rawHeader("Upload-Offset").toLongLong() != upload.data.size()) {
                return new FakeErrorReply(op, request, &_folder, 409);
            }
            const QByteArray chunk = device->readAll();
            if (patchHook) {
                if (const int status = patchHook(id, upload, upload.data.size() + chunk.size())) {
                    return new FakeErrorReply(op, request, &_folder, status);
                }
            }
            upload.data.append(chunk);
        }
        return respond(op, request, id);
    }

    QNetworkReply *respond(QNetworkAccessManager::Operation op, const QNetworkRequest &request, const QString &id)
    {
        const Upload upload = uploads.value(id);
        QHttpHeaders headers;
        headers.append(QHttpHeaders::WellKnownHeader::Location, Utility::concatUrlPath(_folder.account()->url(), QStringLiteral("tus/") + id).toString());
        headers.append("Upload-Offset", QByteArray::number(upload.data.size()));
        headers.append("Upload-Length", QByteArray::number(upload.length));
        if (upload.data.size() == upload.length) {
            uploads.remove(id);
            auto &remote = _folder.remoteModifier();
            remote.insert(upload.path, static_cast<quint64>(upload.length), upload.data.isEmpty() ? 'W' : upload.data.at(0));
            FileInfo *fi = remote.find(upload.path);
            fi->setLastModifiedFromSecondsUTC(upload.mtime);
            if (sendFinalHeaders) {
                // like the server, report the etag, file id and permissions with the last chunk
                headers.append("OC-ETag", fi->etag.toUtf8());
                headers.append("ETag", fi->etag.toUtf8());
                headers.append("OC-FileID", fi->fileId);
                headers.append("OC-Perm", "WDNVCKR");
            }
        }
        return new FakePayloadReply(op, request, {}, headers, &_folder);
    }

    FakeFolder &_folder;
    int _nextId = 0;
};

QString tusFile()
{
    return QStringLiteral("A/tusfile");
}
const quint64 tusFileSize = 2500; // three chunks of at most 1000 bytes

}

// Regression coverage for the TUS-resume 409 handling (opencloud-eu/desktop#898):
// a stale/diverged resume answered with 409 Upload-Offset mismatch must be
// recoverable, never a wedge. The transport-level recovery (re-query the server's
// current offset with a HEAD and continue from there) lives in
// PropagateUploadFileTUS::slotChunkFinished(); this pins the classifyError()
// fallback contract for when that path is not taken.
class TestTusResume : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A 409 is recoverable: SoftError + another pass. Pre-fix it fell through to
    // the default NormalError with anotherSyncNeeded left unset (a silent,
    // un-prioritised drop) and the upload wedged near 100%.
    void test409ConflictIsRecoverable()
    {
        bool anotherSyncNeeded = false;
        QCOMPARE(classifyError(QNetworkReply::ContentConflictError, 409, &anotherSyncNeeded), SyncFileItem::SoftError);
        QVERIFY(anotherSyncNeeded);
    }

    // An interrupted upload resumes from the offset the server reports.
    void testResumeFromServerOffset()
    {
        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12());
        FakeTusServer server(fakeFolder);
        fakeFolder.localModifier().insert(tusFile(), tusFileSize, 'T');
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        // the second chunk fails, the upload stays on the server
        bool failed = false;
        server.patchHook = [&](const QString &, FakeTusServer::Upload &, qint64) { return std::exchange(failed, true) ? 0 : 500; };
        QVERIFY(!fakeFolder.syncOnce());
        QVERIFY(fakeFolder.syncJournal().getUploadInfo(tusFile())._valid);

        fakeFolder.syncJournal().wipeErrorBlacklist();
        server.requests.clear();
        QVERIFY(fakeFolder.syncOnce());
        QCOMPARE(server.requests, (QByteArrayList{"HEAD", "PATCH", "PATCH"}));
        QCOMPARE(fakeFolder.currentRemoteState().find(tusFile())->contentSize, tusFileSize);
        QVERIFY(!fakeFolder.syncJournal().getUploadInfo(tusFile())._valid);
    }

    void testStaleResumeInfoRestartsUpload_data()
    {
        QTest::addColumn<int>("status");
        QTest::newRow("404 Not Found") << 404;
        QTest::newRow("410 Gone") << 410;
        QTest::newRow("403 Forbidden (expired transfer token)") << 403;
    }

    // The saved resume info points at an upload the server no longer has (rejected or expired),
    // or at a URL whose transfer token expired. The client must forget it and upload the file
    // again instead of retrying the dead URL on every sync.
    void testStaleResumeInfoRestartsUpload()
    {
        QFETCH(int, status);
        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12());
        FakeTusServer server(fakeFolder);
        server.unknownUploadStatus = status;
        fakeFolder.localModifier().insert(tusFile(), tusFileSize, 'T');
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        bool failed = false;
        server.patchHook = [&](const QString &, FakeTusServer::Upload &, qint64) { return std::exchange(failed, true) ? 0 : 500; };
        QVERIFY(!fakeFolder.syncOnce());
        QVERIFY(fakeFolder.syncJournal().getUploadInfo(tusFile())._valid);

        // the server drops the upload
        server.uploads.clear();

        fakeFolder.syncJournal().wipeErrorBlacklist();
        server.requests.clear();
        QVERIFY(fakeFolder.syncOnce());
        QCOMPARE(server.requests, (QByteArrayList{"HEAD", "POST", "PATCH", "PATCH"}));
        QCOMPARE(fakeFolder.currentRemoteState().find(tusFile())->contentSize, tusFileSize);
        QVERIFY(!fakeFolder.syncJournal().getUploadInfo(tusFile())._valid);
    }

    // The server rejects the completed upload with 460 Checksum Mismatch and discards it.
    // The next attempt has to start a new upload.
    void testChecksumMismatchClearsResumeInfo()
    {
        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12());
        FakeTusServer server(fakeFolder);
        fakeFolder.localModifier().insert(tusFile(), tusFileSize, 'T');
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        server.patchHook = [&](const QString &id, FakeTusServer::Upload &upload, qint64 newSize) {
            if (newSize == upload.length) {
                server.uploads.remove(id);
                return 460;
            }
            return 0;
        };
        QVERIFY(!fakeFolder.syncOnce());
        QVERIFY(!fakeFolder.syncJournal().getUploadInfo(tusFile())._valid);

        server.patchHook = nullptr;
        fakeFolder.syncJournal().wipeErrorBlacklist();
        server.requests.clear();
        QVERIFY(fakeFolder.syncOnce());
        QCOMPARE(server.requests, (QByteArrayList{"POST", "PATCH", "PATCH"}));
        QCOMPARE(fakeFolder.currentRemoteState().find(tusFile())->contentSize, tusFileSize);
    }

    void testPropfindAfterUploadFails_data()
    {
        QTest::addColumn<quint64>("size");
        QTest::addColumn<int>("status");
        for (const quint64 size : {quint64{0}, tusFileSize}) {
            for (const int status : {503, 500, 404}) {
                QTest::addRow("size %llu, %d", size, status) << size << status;
            }
        }
    }

    // The final upload response lacks ETag or OC-Perm, the client asks for them with a PROPFIND
    // and that fails. The item has to fail instead of the sync hanging forever, and the next
    // sync has to complete (opencloud-eu/desktop#1165).
    void testPropfindAfterUploadFails()
    {
        QFETCH(quint64, size);
        QFETCH(int, status);
        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12());
        FakeTusServer server(fakeFolder);
        server.sendFinalHeaders = false;
        server.propfindErrors.insert(tusFile(), status);
        fakeFolder.localModifier().insert(tusFile(), size, 'T');
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        {
            ItemCompletedSpy completeSpy(fakeFolder);
            QSignalSpy finishedSpy(&fakeFolder.syncEngine(), &SyncEngine::finished);
            fakeFolder.scheduleSync();
            // don't use syncOnce(), a hanging sync would only time out after an hour
            QVERIFY(finishedSpy.wait(10s));
            QVERIFY(!finishedSpy.first().first().toBool());
            QVERIFY(server.requests.contains("PROPFIND"));
            const auto item = completeSpy.findItem(tusFile());
            QVERIFY(item);
            QVERIFY(item->_status != SyncFileItem::Success);
            QVERIFY(!item->_errorString.isEmpty());
        }

        // the server recovers
        server.propfindErrors.clear();
        fakeFolder.syncJournal().wipeErrorBlacklist();
        QVERIFY(fakeFolder.syncOnce());
        QCOMPARE(fakeFolder.currentRemoteState().find(tusFile())->contentSize, size);
        QVERIFY(fakeFolder.syncJournal().getFileRecord(tusFile()).isValid());
    }
};

QTEST_GUILESS_MAIN(TestTusResume)
#include "testtusresume.moc"
