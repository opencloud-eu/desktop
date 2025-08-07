/*
   This software is in the public domain, furnished "as is", without technical
   support, and with no warranty, express or implied, as to its usefulness for
   any purpose.
*/

#include "testutils/testutils.h"

#include "libsync/common/filesystembase.h"
#include "libsync/common/utility.h"
#include "libsync/discoveryinfo.h"
#include "libsync/filesystem.h"

#ifdef Q_OS_WIN
// the lnk code requires some parts we usually no include
// also the include order is relevant
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shlguid.h>
#include <shobjidl.h>

#include "common/utility_win.h"
#endif


#include <QTemporaryDir>
#include <QtTest>


using namespace Qt::Literals::StringLiterals;
using namespace std::chrono_literals;

using namespace OCC::Utility;

namespace OCC {
OPENCLOUD_SYNC_EXPORT extern bool fsCasePreserving_override;
}

class TestUtility : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testOctetsToString()
    {
        QLocale::setDefault(QLocale(QStringLiteral("en")));
        QCOMPARE(octetsToString(999), QString::fromLatin1("999 bytes"));
        QCOMPARE(octetsToString(1024), QString::fromLatin1("1 kB"));
        QCOMPARE(octetsToString(1364), QString::fromLatin1("1 kB"));

        QCOMPARE(octetsToString(9110), QString::fromLatin1("9 kB"));
        QCOMPARE(octetsToString(9910), QString::fromLatin1("10 kB"));
        QCOMPARE(octetsToString(10240), QString::fromLatin1("10 kB"));

        QCOMPARE(octetsToString(123456), QString::fromLatin1("121 kB"));
        QCOMPARE(octetsToString(1234567), QString::fromLatin1("1.2 MB"));
        QCOMPARE(octetsToString(12345678), QString::fromLatin1("11.8 MB"));
        QCOMPARE(octetsToString(123456789), QString::fromLatin1("117.7 MB"));
        QCOMPARE(octetsToString(1000LL * 1000 * 1000 * 5), QString::fromLatin1("4.66 GB"));

        QCOMPARE(octetsToString(1), QString::fromLatin1("1 bytes"));
        QCOMPARE(octetsToString(2), QString::fromLatin1("2 bytes"));
        QCOMPARE(octetsToString(1024), QString::fromLatin1("1 kB"));
        QCOMPARE(octetsToString(1024 * 1024), QString::fromLatin1("1.0 MB"));
        QCOMPARE(octetsToString(1024LL * 1024 * 1024), QString::fromLatin1("1.00 GB"));
    }

    void testLaunchOnStartup()
    {
        QString postfix = QString::number(QRandomGenerator::global()->generate());

        const QString appName = QStringLiteral("testLaunchOnStartup.%1").arg(postfix);
        const QString guiName = QStringLiteral("LaunchOnStartup GUI Name");

        QVERIFY(hasLaunchOnStartup(appName) == false);
        setLaunchOnStartup(appName, guiName, true);
        QVERIFY(hasLaunchOnStartup(appName) == true);
        setLaunchOnStartup(appName, guiName, false);
        QVERIFY(hasLaunchOnStartup(appName) == false);
    }

    void testDurationToDescriptiveString_data()
    {
        QTest::addColumn<bool>("useTranslation");
        QTest::addColumn<std::chrono::milliseconds>("input");
        QTest::addColumn<QString>("output1");
        QTest::addColumn<QString>("output2");


        const QDateTime current = QDateTime::currentDateTimeUtc();
        QTest::newRow("0ms") << false << std::chrono::milliseconds(0ms) << QStringLiteral("0 second(s)") << QString();
        QTest::newRow("5ms") << false << std::chrono::milliseconds(5ms) << QStringLiteral("0 second(s)") << QString();
        QTest::newRow("1s") << false << std::chrono::milliseconds(1s) << QStringLiteral("1 second(s)") << QString();
        QTest::newRow("1005ms") << false << std::chrono::milliseconds(1005ms) << QStringLiteral("1 second(s)") << QString();
        QTest::newRow("56123ms") << false << std::chrono::milliseconds(56123ms) << QStringLiteral("56 second(s)") << QString();
        QTest::newRow("90s") << false << std::chrono::milliseconds(90s) << QStringLiteral("2 minute(s)") << QStringLiteral("1 minute(s) 30 second(s)");
        QTest::newRow("3h") << false << std::chrono::milliseconds(3h) << QStringLiteral("3 hour(s)") << QString();
        QTest::newRow("3h + 20s") << false << std::chrono::milliseconds(3h + 20s) << QStringLiteral("3 hour(s)") << QString();
        QTest::newRow("3h + 70s") << false << std::chrono::milliseconds(3h + 70s) << QStringLiteral("3 hour(s)") << QStringLiteral("3 hour(s) 1 minute(s)");
        QTest::newRow("3h + 100s") << false << std::chrono::milliseconds(3h + 100s) << QStringLiteral("3 hour(s)") << QStringLiteral("3 hour(s) 2 minute(s)");
        QTest::newRow("4years") << false << std::chrono::milliseconds(current.addYears(4).addMonths(5).addDays(2).addSecs(23 * 60 * 60) - current)
                                << QStringLiteral("4 year(s)") << QStringLiteral("4 year(s) 5 month(s)");
        QTest::newRow("2days") << false << std::chrono::milliseconds(current.addDays(2).addSecs(23 * 60 * 60) - current) << QStringLiteral("3 day(s)")
                               << QStringLiteral("2 day(s) 23 hour(s)");


        QTest::newRow("0ms translated") << true << std::chrono::milliseconds(0ms) << QStringLiteral("0 seconds") << QString();
        QTest::newRow("5ms translated") << true << std::chrono::milliseconds(5ms) << QStringLiteral("0 seconds") << QString();
        QTest::newRow("1s translated") << true << std::chrono::milliseconds(1s) << QStringLiteral("1 second") << QString();
        QTest::newRow("1005ms translated") << true << std::chrono::milliseconds(1005ms) << QStringLiteral("1 second") << QString();
        QTest::newRow("56123ms translated") << true << std::chrono::milliseconds(56123ms) << QStringLiteral("56 seconds") << QString();
        QTest::newRow("90s translated") << true << std::chrono::milliseconds(90s) << QStringLiteral("2 minutes") << QStringLiteral("1 minute 30 seconds");
        QTest::newRow("3h translated") << true << std::chrono::milliseconds(3h) << QStringLiteral("3 hours") << QString();
        QTest::newRow("3h + 20s translated") << true << std::chrono::milliseconds(3h + 20s) << QStringLiteral("3 hours") << QString();
        QTest::newRow("3h + 70s translated") << true << std::chrono::milliseconds(3h + 70s) << QStringLiteral("3 hours") << QStringLiteral("3 hours 1 minute");
        QTest::newRow("3h + 100s translated") << true << std::chrono::milliseconds(3h + 100s) << QStringLiteral("3 hours")
                                              << QStringLiteral("3 hours 2 minutes");
        QTest::newRow("4years translated") << true << std::chrono::milliseconds(current.addYears(4).addMonths(5).addDays(2).addSecs(23 * 60 * 60) - current)
                                           << QStringLiteral("4 years") << QStringLiteral("4 years 5 months");
        QTest::newRow("2days translated") << true << std::chrono::milliseconds(current.addDays(2).addSecs(23 * 60 * 60) - current) << QStringLiteral("3 days")
                                          << QStringLiteral("2 days 23 hours");
    }


    void testDurationToDescriptiveString()
    {
        QFETCH(bool, useTranslation);
        QFETCH(std::chrono::milliseconds, input);
        QFETCH(QString, output1);
        QFETCH(QString, output2);

        QScopedPointer<QTranslator> translator;
        if (useTranslation) {
            translator.reset(new QTranslator(QCoreApplication::instance()));
            QVERIFY(translator->load(QStringLiteral("desktop_en.ts"), QStringLiteral(":/client/translations/")));
            QCoreApplication::instance()->installTranslator(translator.data());
        }

        QCOMPARE(durationToDescriptiveString1(input), output1);
        QCOMPARE(durationToDescriptiveString2(input), output2.isEmpty() ? output1 : output2);
    }

    void testTimeAgo()
    {
        // Both times in same timezone
        QDateTime d1 = QDateTime::fromString(QStringLiteral("2015-01-24T09:20:30+01:00"), Qt::ISODate);
        QDateTime d2 = QDateTime::fromString(QStringLiteral("2015-01-23T09:20:30+01:00"), Qt::ISODate);
        QString s = timeAgoInWords(d2, d1);
        QCOMPARE(s, QLatin1String("1 day(s) ago"));

        // Different timezones
        QDateTime earlyTS = QDateTime::fromString(QStringLiteral("2015-01-24T09:20:30+01:00"), Qt::ISODate);
        QDateTime laterTS = QDateTime::fromString(QStringLiteral("2015-01-24T09:20:30-01:00"), Qt::ISODate);
        s = timeAgoInWords(earlyTS, laterTS);
        QCOMPARE(s, QLatin1String("2 hour(s) ago"));

        // 'Now' in whatever timezone
        earlyTS = QDateTime::currentDateTime();
        laterTS = earlyTS;
        s = timeAgoInWords(earlyTS, laterTS );
        QCOMPARE(s, QLatin1String("now"));

        earlyTS = earlyTS.addSecs(-6);
        s = timeAgoInWords(earlyTS, laterTS );
        QCOMPARE(s, QLatin1String("less than a minute ago"));
    }

    void testFsCasePreserving()
    {
        QVERIFY(isMac() || isWindows() ? fsCasePreserving() : ! fsCasePreserving());
        QScopedValueRollback<bool> scope(OCC::fsCasePreserving_override);
        OCC::fsCasePreserving_override = 1;
        QVERIFY(fsCasePreserving());
        OCC::fsCasePreserving_override = 0;
        QVERIFY(! fsCasePreserving());
    }

    void testFileNamesEqual()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir dir2(dir.path());
        QVERIFY(dir2.mkpath(QStringLiteral("1")));
        if( !fsCasePreserving() ) {
            QVERIFY(dir2.mkpath(QStringLiteral("TEST")));
        }
        QVERIFY(dir2.mkpath(QStringLiteral("test/TESTI")));
        QVERIFY(dir2.mkpath(QStringLiteral("TESTI")));

        QString a = dir.path();
        QString b = dir.path();

        QVERIFY(fileNamesEqual(a, b));

        QVERIFY(fileNamesEqual(a + QStringLiteral("/test"), b + QStringLiteral("/test"))); // both exist
        QVERIFY(fileNamesEqual(a + QStringLiteral("/test/TESTI"), b + QStringLiteral("/test/../test/TESTI"))); // both exist

        QScopedValueRollback<bool> scope(OCC::fsCasePreserving_override, true);
        QVERIFY(fileNamesEqual(a + QStringLiteral("/test"), b + QStringLiteral("/TEST"))); // both exist

        QVERIFY(!fileNamesEqual(a + QStringLiteral("/test"), b + QStringLiteral("/test/TESTI"))); // both are different

        dir.remove();
    }

    void testIsChildOf_data()
    {
        QTest::addColumn<QString>("child");
        QTest::addColumn<QString>("parent");
        QTest::addColumn<bool>("output");
        QTest::addColumn<bool>("casePreserving");

        const auto add = [](const QString &child, const QString &parent, bool result, bool casePreserving = true) {
            const auto title = QStringLiteral("CasePreserving %1: %2 is %3 child of %4").arg(casePreserving ? QStringLiteral("yes") : QStringLiteral("no"), child, result ? QString() : QStringLiteral("not"), parent);
            QTest::addRow("%s", qUtf8Printable(title)) << child << parent << result << casePreserving;
        };
        add(QStringLiteral("/A/a"), QStringLiteral("/A"), true);
        add(QStringLiteral("/A/a"), QStringLiteral("/A/a"), true);
        add(QStringLiteral("/A/a"), QStringLiteral("/A/a/"), true);
        add(QStringLiteral("/A/a/"), QStringLiteral("/A/a/"), true);
        add(QStringLiteral("/A/a/"), QStringLiteral("/A/a"), true);
        add(QStringLiteral("C:/A/a"), QStringLiteral("C:/A"), true);
        add(QStringLiteral("C:/Aa"), QStringLiteral("C:/A"), false);
        add(QStringLiteral("C:/Aa"), QStringLiteral("C:/A"), false);
        add(QStringLiteral("A/a"), QStringLiteral("A"), true);
        add(QStringLiteral("a/a"), QStringLiteral("A"), true, true);
        add(QStringLiteral("a/a"), QStringLiteral("A"), false, false);
        add(QStringLiteral("Aa"), QStringLiteral("A"), false);
        add(QStringLiteral("A/a"), QStringLiteral("A"), true);
        add(QStringLiteral("A/a"), QStringLiteral("A/"), true);
        add(QStringLiteral("ä/a"), QStringLiteral("ä/"), true);
        add(QStringLiteral("Ä/è/a"), QStringLiteral("Ä/è/"), true);
        add(QStringLiteral("Ä/a"), QStringLiteral("Ä/"), true);
        add(QStringLiteral("Aa"), QStringLiteral("A"), false);
        add(QStringLiteral("https://foo/bar"), QStringLiteral("https://foo"), true);
        add(QStringLiteral("https://foo/bar"), QStringLiteral("http://foo"), false);
        add(QStringLiteral("https://foo/bar"), QStringLiteral("http://foo/foo"), false);
#ifdef Q_OS_WIN
        // QDir::cleanPath converts \\ only on Windows
        add(QStringLiteral("C:/Program Files/test)"), QStringLiteral("C:/Program Files"), true);
        add(QStringLiteral(R"(C:\Program Files\test)"), QStringLiteral("C:/Program Files"), true);
        add(QStringLiteral(R"(C:\Program Files\test\)"), QStringLiteral("C:/Program Files\\"), true);
        add(QStringLiteral(R"(C:\Program Files\test\\\)"), QStringLiteral("C:/Program Files/test"), true);
#endif
    }

    void testIsChildOf()
    {
        const QScopedValueRollback<bool> rollback(OCC::fsCasePreserving_override);
        QFETCH(QString, child);
        QFETCH(QString, parent);
        QFETCH(bool, output);
        QFETCH(bool, casePreserving);
        OCC::fsCasePreserving_override = casePreserving;
        QCOMPARE(OCC::FileSystem::isChildPathOf(child, parent), output);
    }

    void testSanitizeForFileName_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("output");

        QTest::newRow("")
            << "foobar"
            << "foobar";
        QTest::newRow("")
            << "a/b?c<d>e\\f:g*h|i\"j"
            << "abcdefghij";
        QTest::newRow("")
            << QString::fromLatin1("a\x01 b\x1f c\x80 d\x9f")
            << "a b c d";
    }

    void testSanitizeForFileName()
    {
        QFETCH(QString, input);
        QFETCH(QString, output);
        QCOMPARE(sanitizeForFileName(input), output);
    }

    void testNormalizeEtag()
    {
        ;

#define CHECK_NORMALIZE_ETAG(TEST, EXPECT) \
    QCOMPARE(OCC::Utility::normalizeEtag(QStringLiteral(TEST)), QStringLiteral(EXPECT));

        CHECK_NORMALIZE_ETAG("foo", "foo");
        CHECK_NORMALIZE_ETAG("\"foo\"", "foo");
        CHECK_NORMALIZE_ETAG("\"nar123\"", "nar123");
        CHECK_NORMALIZE_ETAG("", "");
        CHECK_NORMALIZE_ETAG("\"\"", "");

        /* Test with -gzip (all combinaison) */
        CHECK_NORMALIZE_ETAG("foo-gzip", "foo");
        CHECK_NORMALIZE_ETAG("\"foo\"-gzip", "foo");
        CHECK_NORMALIZE_ETAG("\"foo-gzip\"", "foo");
    }

    void testFileMetaData()
    {
        using namespace OCC::TestUtils;
        using namespace OCC::FileSystem;

        QTemporaryDir temp = createTempDir();
        QFile tempFile(temp.filePath(QStringLiteral("testfile")));
        QVERIFY(tempFile.open(QIODevice::WriteOnly));
        QByteArray data(64, 'X');
        QCOMPARE(tempFile.write(data), data.size());
        tempFile.close();

        const auto fn = tempFile.fileName();
        const QString testKey = QStringLiteral("testKey");
        const QByteArray testValue("testValue");

        QVERIFY(!Tags::get(fn, testKey).has_value());
        QVERIFY(Tags::set(fn, testKey, testValue));
        QCOMPARE(Tags::get(fn, testKey).value(), testValue);
        QVERIFY(Tags::remove(fn, testKey));
        QVERIFY(!Tags::get(fn, testKey).has_value());
    }

    void testDirMetaData()
    {
        using namespace OCC::TestUtils;
        using namespace OCC::FileSystem;

        QTemporaryDir tempDir = createTempDir();

        const auto fn = tempDir.path();
        const QString testKey = QStringLiteral("testKey");
        const QByteArray testValue("testValue");

        QVERIFY(!Tags::get(fn, testKey).has_value());
        QVERIFY(Tags::set(fn, testKey, testValue));
        QCOMPARE(Tags::get(fn, testKey).value(), testValue);
        QVERIFY(Tags::remove(fn, testKey));
        QVERIFY(!Tags::get(fn, testKey).has_value());
    }
#ifdef Q_OS_WIN
    void testUtilityUnixTimeToLargeIntegerFiletime()
    {
        // Sun Jan 31 2038 23:00:00 GMT+0000
        time_t y2k38 = 2148591600;
        LARGE_INTEGER out;
        OCC::Utility::UnixTimeToLargeIntegerFiletime(y2k38, &out);
        // (2148591600 * 10000000) + 116444736000000000
        QCOMPARE(out.QuadPart, 137930652000000000);
    }

    void testLNK()
    {
        auto mkLNK = [](const std::wstring &path, const std::filesystem::path &target) {
            // https://learn.microsoft.com/en-us/windows/win32/shell/links?redirectedfrom=MSDN#Shellink_Creating_Shortcut
            IShellLink *psl;
            HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)&psl);
            if (SUCCEEDED(hres)) {
                IPersistFile *ppf;

                // Set the path to the shortcut target and add the description.
                psl->SetPath(path.data());
                psl->SetDescription(L"A test lnk");

                // Query IShellLink for the IPersistFile interface, used for saving the
                // shortcut in persistent storage.
                hres = psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf);

                if (SUCCEEDED(hres)) {
                    hres = ppf->Save(target.native().data(), true);
                    if (SUCCEEDED(hres)) {
                        qDebug() << "Created lnk" << target << "->" << path;
                    } else {
                        qCritical() << "Failed to create lnk: Save" << OCC::Utility::formatWinError(hres);
                    }
                    ppf->Release();
                } else {
                    qCritical() << "Failed to create lnk: QueryInterface" << OCC::Utility::formatWinError(hres);
                }
                psl->Release();
            } else {
                qCritical() << "Failed to create lnk: CoCreateInstance" << OCC::Utility::formatWinError(hres);
            }
        };

        // the test relies on different change times for this binary and the lnk, sleep to be sure
        QThread::currentThread()->sleep(1s);

        const auto tmp = OCC::TestUtils::createTempDir();

        {
            // historically Qt handles llnk filles as symlinks and resolves them
            // ensure w don't do the same
            const QString qtTarget = tmp.filePath(u"test.lnk"_s);
            const auto target = OCC::FileSystem::toFilesystemPath(qtTarget);
            // we must not use toFilesystemPath as we would get a \\?\ path which is not supported with shortcuts
            mkLNK(qApp->applicationFilePath().toStdWString(), target);
            auto entry = std::filesystem::directory_entry{target};
            OCC::LocalInfo fileInfo(entry, ItemTypeFile);
            const auto qFileInfo = QFileInfo(qtTarget);
            const auto qFileInfoTarget = QFileInfo(qtTarget);

            QVERIFY(qFileInfo.exists());
            QVERIFY(entry.exists());

            // fails as Qt resolves the path
            QCOMPARE_NE(entry.file_size(), qFileInfo.size());
            QCOMPARE_NE(OCC::FileSystem::fileTimeToTime_t(entry.last_write_time()), OCC::Utility::qDateTimeToTime_t(qFileInfo.metadataChangeTime()));

            QCOMPARE(entry.file_size(), fileInfo.size());
            QCOMPARE_NE(entry.file_size(), qFileInfoTarget.size());
            QCOMPARE(OCC::FileSystem::fileTimeToTime_t(entry.last_write_time()), fileInfo.modtime());
        }

        {
            const QString qtTarget = tmp.filePath(u"bad_test.lnk"_s);
            const auto target = OCC::FileSystem::toFilesystemPath(qtTarget);
            // create an invalid link
            mkLNK(L"", target);
            auto entry = std::filesystem::directory_entry{target};
            OCC::LocalInfo fileInfo(entry, ItemTypeFile);
            const auto qFileInfo = QFileInfo(qtTarget);

            QVERIFY(!qFileInfo.exists());
            QVERIFY(entry.exists());

            // Qt resolves the path, the target is invalid
            QCOMPARE(0, qFileInfo.size());
            QCOMPARE(0, OCC::Utility::qDateTimeToTime_t(qFileInfo.metadataChangeTime()));

            QCOMPARE(entry.file_size(), fileInfo.size());
            QCOMPARE(OCC::FileSystem::fileTimeToTime_t(entry.last_write_time()), fileInfo.modtime());
        }
    }
#endif

    void testFilesystem()
    {
        // validate that the different ways to access file metadata behave the same
        auto entry = std::filesystem::directory_entry{OCC::FileSystem::toFilesystemPath(qApp->applicationFilePath())};
        OCC::LocalInfo fileInfo(entry, ItemTypeFile);
        QFileInfo qFileInfo(OCC::FileSystem::fromFilesystemPath(entry.path()));

        QCOMPARE(entry.file_size(), fileInfo.size());
        QCOMPARE(entry.file_size(), qFileInfo.size());
        QCOMPARE(entry.file_size(), OCC::FileSystem::getSize(entry.path()));

        QCOMPARE(OCC::FileSystem::fileTimeToTime_t(entry.last_write_time()), fileInfo.modtime());
        QCOMPARE(OCC::FileSystem::fileTimeToTime_t(entry.last_write_time()), OCC::Utility::qDateTimeToTime_t(qFileInfo.metadataChangeTime()));
        QCOMPARE(OCC::FileSystem::fileTimeToTime_t(entry.last_write_time()), OCC::FileSystem::getModTime(entry.path()));

        quint64 inode = 0;
        QVERIFY(OCC::FileSystem::getInode(entry.path(), &inode));
        QCOMPARE(fileInfo.inode(), inode);
    }
};

QTEST_GUILESS_MAIN(TestUtility)
#include "testutility.moc"
