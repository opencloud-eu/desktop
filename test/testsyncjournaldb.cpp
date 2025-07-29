/*
 *    This software is in the public domain, furnished "as is", without technical
 *       support, and with no warranty, express or implied, as to its usefulness for
 *          any purpose.
 *          */

#include "folderman.h"
#include "syncfileitem.h"


#include <QtTest>

#include "common/syncjournaldb.h"
#include "common/syncjournalfilerecord.h"

#include "testutils/testutils.h"

using namespace Qt::Literals::StringLiterals;
using namespace OCC;

class TestSyncJournalDB : public QObject
{
    Q_OBJECT

    QTemporaryDir _tempDir;

public:
    TestSyncJournalDB()
        : _db((_tempDir.path() + QStringLiteral("/sync.db")))
    {
        QVERIFY(_tempDir.isValid());
    }

    qint64 dropMsecs(const QDateTime &time)
    {
        return Utility::qDateTimeToTime_t(time);
    }

private Q_SLOTS:

    void initTestCase()
    {
    }

    void cleanupTestCase()
    {
        const QString file = _db.databaseFilePath();
        QFile::remove(file);
    }

    void testFileRecord()
    {
        QVERIFY(!_db.getFileRecord(u"nonexistant"_s).isValid());

        auto item = TestUtils::dummyItem(u"foo"_s);
        // Use a value that exceeds uint32 and isn't representable by the
        // signed int being cast to uint64 either (like uint64::max would be)
        item._inode = std::numeric_limits<quint32>::max() + 12ull;
        item._modtime = dropMsecs(QDateTime::currentDateTime());
        item._type = ItemTypeDirectory;
        item._etag = u"789789"_s;
        item._fileId = "abcd"_ba;
        item._remotePerm = RemotePermissions::fromDbValue("RW");
        item._size = 213089055;
        item._checksumHeader = "MD5:mychecksum";

        const auto record = [&] { return SyncJournalFileRecord::fromSyncFileItem(item); };
        QVERIFY(_db.setFileRecord(record()));

        SyncJournalFileRecord storedRecord = _db.getFileRecord(u"foo"_s);
        QVERIFY(storedRecord.isValid());
        QVERIFY(storedRecord == record());

        // Update checksum
        item._checksumHeader = "ADLER32:newchecksum";
        _db.updateFileRecordChecksum(QStringLiteral("foo"), "newchecksum", CheckSums::fromByteArray("Adler32"));
        storedRecord = _db.getFileRecord(u"foo"_s);
        QVERIFY(storedRecord.isValid());
        QVERIFY(storedRecord == record());

        // Update metadata
        item._modtime = dropMsecs(QDateTime::currentDateTime().addDays(1));
        // try a value that only fits uint64, not int64
        item._inode = std::numeric_limits<quint64>::max() - std::numeric_limits<quint32>::max() - 1;
        item._type = ItemTypeFile;
        item._etag = u"789FFF"_s;
        item._fileId = "efg"_ba;
        item._remotePerm = RemotePermissions::fromDbValue("NV");
        item._size = 289055;
        _db.setFileRecord(record());
        storedRecord = _db.getFileRecord(u"foo"_s);
        QVERIFY(storedRecord.isValid());
        QVERIFY(storedRecord == record());

        QVERIFY(_db.deleteFileRecord(u"foo"_s));
        storedRecord = _db.getFileRecord(u"foo"_s);
        QVERIFY(!storedRecord.isValid());
    }

    void testFileRecordChecksum()
    {
        // Try with and without a checksum
        {
            auto item = TestUtils::dummyItem(u"foo-checksum"_s);
            item._checksumHeader = "MD5:mychecksum"_ba;
            const auto record = SyncJournalFileRecord::fromSyncFileItem(item);
            QVERIFY(_db.setFileRecord(record));

            SyncJournalFileRecord storedRecord = _db.getFileRecord(item.destination());
            QVERIFY(storedRecord.isValid());
            QCOMPARE(storedRecord.path(), record.path());
            QCOMPARE(storedRecord.remotePerm(), record.remotePerm());
            QCOMPARE(storedRecord.checksumHeader(), record.checksumHeader());

            // qDebug()<< "OOOOO " << storedRecord._modtime.toTime_t() << record._modtime.toTime_t();

            // Attention: compare time_t types here, as QDateTime seem to maintain
            // milliseconds internally, which disappear in sqlite. Go for full seconds here.
            QVERIFY(storedRecord.modtime() == record.modtime());
            QVERIFY(storedRecord == record);
        }
        {
            auto item = TestUtils::dummyItem(u"foo-nochecksum"_s);
            item._remotePerm = RemotePermissions::fromDbValue("RW");
            const auto record = SyncJournalFileRecord::fromSyncFileItem(item);
            QVERIFY(_db.setFileRecord(record));

            SyncJournalFileRecord storedRecord = _db.getFileRecord(item.destination());
            QVERIFY(storedRecord.isValid());
            QVERIFY(storedRecord == record);
        }
    }

    void testDownloadInfo()
    {
        typedef SyncJournalDb::DownloadInfo Info;
        Info record = _db.getDownloadInfo(QStringLiteral("nonexistant"));
        QVERIFY(!record._valid);

        record._errorCount = 5;
        record._etag = "ABCDEF";
        record._valid = true;
        record._tmpfile = QStringLiteral("/tmp/foo");
        _db.setDownloadInfo(QStringLiteral("foo"), record);

        Info storedRecord = _db.getDownloadInfo(QStringLiteral("foo"));
        QVERIFY(storedRecord == record);

        _db.setDownloadInfo(QStringLiteral("foo"), Info());
        Info wipedRecord = _db.getDownloadInfo(QStringLiteral("foo"));
        QVERIFY(!wipedRecord._valid);
    }

    void testUploadInfo()
    {
        typedef SyncJournalDb::UploadInfo Info;
        Info record = _db.getUploadInfo(QStringLiteral("nonexistant"));
        QVERIFY(!record._valid);

        record._errorCount = 5;
        record._chunk = 12;
        record._transferid = 812974891;
        record._size = 12894789147;
        record._modtime = dropMsecs(QDateTime::currentDateTime());
        record._valid = true;
        _db.setUploadInfo(QStringLiteral("foo"), record);

        Info storedRecord = _db.getUploadInfo(QStringLiteral("foo"));
        QVERIFY(storedRecord == record);

        _db.setUploadInfo(QStringLiteral("foo"), Info());
        Info wipedRecord = _db.getUploadInfo(QStringLiteral("foo"));
        QVERIFY(!wipedRecord._valid);
    }

    void testConflictRecord()
    {
        ConflictRecord record;
        record.path = "abc";
        record.baseFileId = "def";
        record.baseModtime = 1234;
        record.baseEtag = "ghi";

        QVERIFY(!_db.conflictRecord(record.path).isValid());

        _db.setConflictRecord(record);
        auto newRecord = _db.conflictRecord(record.path);
        QVERIFY(newRecord.isValid());
        QCOMPARE(newRecord.path, record.path);
        QCOMPARE(newRecord.baseFileId, record.baseFileId);
        QCOMPARE(newRecord.baseModtime, record.baseModtime);
        QCOMPARE(newRecord.baseEtag, record.baseEtag);

        _db.deleteConflictRecord(record.path);
        QVERIFY(!_db.conflictRecord(record.path).isValid());
    }

    void testAvoidReadFromDbOnNextSync()
    {
        auto invalidEtag = u"_invalid_"_s;
        auto initialEtag = u"etag"_s;
        auto makeEntry = [&](const QAnyStringView &path, ItemType type) {
            auto item = TestUtils::dummyItem(path.toString());
            item._type = type;
            item._etag = initialEtag;
            item._remotePerm = RemotePermissions::fromDbValue("RW");
            _db.setFileRecord(SyncJournalFileRecord::fromSyncFileItem(item));
        };
        auto getEtag = [&](const QAnyStringView &path) { return _db.getFileRecord(path.toString()).etag(); };

        const auto dirType = ItemTypeDirectory;
        const auto fileType = ItemTypeFile;

        makeEntry("foodir", dirType);
        makeEntry("otherdir", dirType);
        makeEntry("foo%", dirType); // wildcards don't apply
        makeEntry("foodi_", dirType); // wildcards don't apply
        makeEntry("foodir/file", fileType);
        makeEntry("foodir/subdir", dirType);
        makeEntry("foodir/subdir/file", fileType);
        makeEntry("foodir/otherdir", dirType);
        makeEntry("fo", dirType); // prefix, but does not match
        makeEntry("foodir/sub", dirType); // prefix, but does not match
        makeEntry("foodir/subdir/subsubdir", dirType);
        makeEntry("foodir/subdir/subsubdir/file", fileType);
        makeEntry("foodir/subdir/otherdir", dirType);

        _db.schedulePathForRemoteDiscovery(QByteArray("foodir/subdir"));

        // Direct effects of parent directories being set to _invalid_
        QCOMPARE(getEtag("foodir"), invalidEtag);
        QCOMPARE(getEtag("foodir/subdir"), invalidEtag);
        QCOMPARE(getEtag("foodir/subdir/subsubdir"), initialEtag);

        QCOMPARE(getEtag("foodir/file"), initialEtag);
        QCOMPARE(getEtag("foodir/subdir/file"), initialEtag);
        QCOMPARE(getEtag("foodir/subdir/subsubdir/file"), initialEtag);

        QCOMPARE(getEtag("fo"), initialEtag);
        QCOMPARE(getEtag("foo%"), initialEtag);
        QCOMPARE(getEtag("foodi_"), initialEtag);
        QCOMPARE(getEtag("otherdir"), initialEtag);
        QCOMPARE(getEtag("foodir/otherdir"), initialEtag);
        QCOMPARE(getEtag("foodir/sub"), initialEtag);
        QCOMPARE(getEtag("foodir/subdir/otherdir"), initialEtag);

        // Indirect effects: setFileRecord() calls filter etags
        initialEtag = u"etag2"_s;

        makeEntry("foodir", dirType);
        QCOMPARE(getEtag("foodir"), invalidEtag);
        makeEntry("foodir/subdir", dirType);
        QCOMPARE(getEtag("foodir/subdir"), invalidEtag);
        makeEntry("foodir/subdir/subsubdir", dirType);
        QCOMPARE(getEtag("foodir/subdir/subsubdir"), initialEtag);
        makeEntry("fo", dirType);
        QCOMPARE(getEtag("fo"), initialEtag);
        makeEntry("foodir/sub", dirType);
        QCOMPARE(getEtag("foodir/sub"), initialEtag);
    }

    void testRecursiveDelete()
    {
        auto makeEntry = [&](const QAnyStringView &path) {
            auto item = TestUtils::dummyItem(path.toString());
            item._remotePerm = RemotePermissions::fromDbValue("RW");

            _db.setFileRecord(SyncJournalFileRecord::fromSyncFileItem(item));
        };

        QStringList elements{u"foo"_s, u"foo/file"_s, u"bar"_s, u"moo"_s, u"moo/file"_s, u"foo%bar"_s, u"foo bla bar/file"_s, u"fo_"_s, u"fo_/file"_s};
        for (const auto &elem : std::as_const(elements))
            makeEntry(elem);

        auto checkElements = [&]() {
            bool ok = true;
            for (const auto &elem : std::as_const(elements)) {
                SyncJournalFileRecord record = _db.getFileRecord(elem);
                if (!record.isValid()) {
                    qWarning() << "Missing record: " << elem;
                    ok = false;
                }
            }
            return ok;
        };

        _db.deleteFileRecord(QStringLiteral("moo"), true);
        elements.removeAll(u"moo"_s);
        elements.removeAll(u"moo/file"_s);
        QVERIFY(checkElements());

        _db.deleteFileRecord(u"fo_"_s, true);
        elements.removeAll(u"fo_"_s);
        elements.removeAll(u"fo_/file"_s);
        QVERIFY(checkElements());

        _db.deleteFileRecord(u"foo%bar"_s, true);
        elements.removeAll(u"foo%bar"_s);
        QVERIFY(checkElements());
    }

    void testPinState()
    {
        auto make = [&](const QByteArray &path, PinState state) {
            _db.internalPinStates().setForPath(path, state);
            auto pinState = _db.internalPinStates().rawForPath(path);
            QVERIFY(pinState);
            QCOMPARE(*pinState, state);
        };
        auto get = [&](const QByteArray &path) -> PinState {
            auto state = _db.internalPinStates().effectiveForPath(path);
            if (!state) {
                QTest::qFail("couldn't read pin state", __FILE__, __LINE__);
                return PinState::Inherited;
            }
            return *state;
        };
        auto getRecursive = [&](const QByteArray &path) -> PinState {
            auto state = _db.internalPinStates().effectiveForPathRecursive(path);
            if (!state) {
                QTest::qFail("couldn't read pin state", __FILE__, __LINE__);
                return PinState::Inherited;
            }
            return *state;
        };
        auto getRaw = [&](const QByteArray &path) -> PinState {
            auto state = _db.internalPinStates().rawForPath(path);
            if (!state) {
                QTest::qFail("couldn't read pin state", __FILE__, __LINE__);
                return PinState::Inherited;
            }
            return *state;
        };

        _db.internalPinStates().wipeForPathAndBelow("");
        auto list = _db.internalPinStates().rawList();
        QCOMPARE(list->size(), 0);

        // Make a thrice-nested setup
        make("", PinState::AlwaysLocal);
        make("local", PinState::AlwaysLocal);
        make("online", PinState::OnlineOnly);
        make("inherit", PinState::Inherited);
        for (auto base : {"local/", "online/", "inherit/"}) {
            make(QByteArray(base) + "inherit", PinState::Inherited);
            make(QByteArray(base) + "local", PinState::AlwaysLocal);
            make(QByteArray(base) + "online", PinState::OnlineOnly);

            for (auto base2 : {"local/", "online/", "inherit/"}) {
                make(QByteArray(base) + base2 + "inherit", PinState::Inherited);
                make(QByteArray(base) + base2 + "local", PinState::AlwaysLocal);
                make(QByteArray(base) + base2 + "online", PinState::OnlineOnly);
            }
        }

        list = _db.internalPinStates().rawList();
        QCOMPARE(list->size(), 4 + 9 + 27);

        // Baseline direct checks (the fallback for unset root pinstate is AlwaysLocal)
        QCOMPARE(get(""), PinState::AlwaysLocal);
        QCOMPARE(get("local"), PinState::AlwaysLocal);
        QCOMPARE(get("online"), PinState::OnlineOnly);
        QCOMPARE(get("inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("nonexistant"), PinState::AlwaysLocal);
        QCOMPARE(get("online/local"), PinState::AlwaysLocal);
        QCOMPARE(get("local/online"), PinState::OnlineOnly);
        QCOMPARE(get("inherit/local"), PinState::AlwaysLocal);
        QCOMPARE(get("inherit/online"), PinState::OnlineOnly);
        QCOMPARE(get("inherit/inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("inherit/nonexistant"), PinState::AlwaysLocal);

        // Inheriting checks, level 1
        QCOMPARE(get("local/inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("local/nonexistant"), PinState::AlwaysLocal);
        QCOMPARE(get("online/inherit"), PinState::OnlineOnly);
        QCOMPARE(get("online/nonexistant"), PinState::OnlineOnly);

        // Inheriting checks, level 2
        QCOMPARE(get("local/inherit/inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("local/local/inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("local/local/nonexistant"), PinState::AlwaysLocal);
        QCOMPARE(get("local/online/inherit"), PinState::OnlineOnly);
        QCOMPARE(get("local/online/nonexistant"), PinState::OnlineOnly);
        QCOMPARE(get("online/inherit/inherit"), PinState::OnlineOnly);
        QCOMPARE(get("online/local/inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("online/local/nonexistant"), PinState::AlwaysLocal);
        QCOMPARE(get("online/online/inherit"), PinState::OnlineOnly);
        QCOMPARE(get("online/online/nonexistant"), PinState::OnlineOnly);

        // Spot check the recursive variant
        QCOMPARE(getRecursive(""), PinState::Inherited);
        QCOMPARE(getRecursive("local"), PinState::Inherited);
        QCOMPARE(getRecursive("online"), PinState::Inherited);
        QCOMPARE(getRecursive("inherit"), PinState::Inherited);
        QCOMPARE(getRecursive("online/local"), PinState::Inherited);
        QCOMPARE(getRecursive("online/local/inherit"), PinState::AlwaysLocal);
        QCOMPARE(getRecursive("inherit/inherit/inherit"), PinState::AlwaysLocal);
        QCOMPARE(getRecursive("inherit/online/inherit"), PinState::OnlineOnly);
        QCOMPARE(getRecursive("inherit/online/local"), PinState::AlwaysLocal);
        make("local/local/local/local", PinState::AlwaysLocal);
        QCOMPARE(getRecursive("local/local/local"), PinState::AlwaysLocal);
        QCOMPARE(getRecursive("local/local/local/local"), PinState::AlwaysLocal);

        // Check changing the root pin state
        make("", PinState::OnlineOnly);
        QCOMPARE(get("local"), PinState::AlwaysLocal);
        QCOMPARE(get("online"), PinState::OnlineOnly);
        QCOMPARE(get("inherit"), PinState::OnlineOnly);
        QCOMPARE(get("nonexistant"), PinState::OnlineOnly);
        make("", PinState::AlwaysLocal);
        QCOMPARE(get("local"), PinState::AlwaysLocal);
        QCOMPARE(get("online"), PinState::OnlineOnly);
        QCOMPARE(get("inherit"), PinState::AlwaysLocal);
        QCOMPARE(get("nonexistant"), PinState::AlwaysLocal);

        // Wiping
        QCOMPARE(getRaw("local/local"), PinState::AlwaysLocal);
        _db.internalPinStates().wipeForPathAndBelow("local/local");
        QCOMPARE(getRaw("local"), PinState::AlwaysLocal);
        QCOMPARE(getRaw("local/local"), PinState::Inherited);
        QCOMPARE(getRaw("local/local/local"), PinState::Inherited);
        QCOMPARE(getRaw("local/local/online"), PinState::Inherited);
        list = _db.internalPinStates().rawList();
        QCOMPARE(list->size(), 4 + 9 + 27 - 4);

        // Wiping everything
        _db.internalPinStates().wipeForPathAndBelow("");
        QCOMPARE(getRaw(""), PinState::Inherited);
        QCOMPARE(getRaw("local"), PinState::Inherited);
        QCOMPARE(getRaw("online"), PinState::Inherited);
        list = _db.internalPinStates().rawList();
        QCOMPARE(list->size(), 0);
    }

private:
    SyncJournalDb _db;
};

QTEST_GUILESS_MAIN(TestSyncJournalDB)
#include "testsyncjournaldb.moc"
