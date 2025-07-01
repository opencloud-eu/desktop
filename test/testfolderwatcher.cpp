/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QtTest>

#include "common/chronoelapsedtimer.h"
#include "common/utility.h"
#include "folderwatcher.h"
#include "testutils/testutils.h"

using namespace std::chrono_literals;

namespace {
class FolderWatcherForTests : public OCC::FolderWatcher
{
    Q_OBJECT
public:
    using OCC::FolderWatcher::FolderWatcher;

    bool pathIsIgnored(const QString &) const override { return false; }
};

void touch(const QString &file)
{
#ifdef Q_OS_WIN
    OCC::TestUtils::writeRandomFile(file);
#else
    QString cmd;
    cmd = QStringLiteral("touch %1").arg(file);
    qDebug() << "Command: " << cmd;
    system(cmd.toLocal8Bit().constData());
#endif
}

void mkdir(const QString &file)
{
#ifdef Q_OS_WIN
    QDir dir;
    dir.mkdir(file);
#else
    QString cmd = QStringLiteral("mkdir %1").arg(file);
    qDebug() << "Command: " << cmd;
    system(cmd.toLocal8Bit().constData());
#endif
}

void rmdir(const QString &file)
{
#ifdef Q_OS_WIN
    QDir dir;
    dir.rmdir(file);
#else
    QString cmd = QStringLiteral("rmdir %1").arg(file);
    qDebug() << "Command: " << cmd;
    system(cmd.toLocal8Bit().constData());
#endif
}

void rm(const QString &file)
{
#ifdef Q_OS_WIN
    QFile::remove(file);
#else
    QString cmd = QStringLiteral("rm %1").arg(file);
    qDebug() << "Command: " << cmd;
    system(cmd.toLocal8Bit().constData());
#endif
}

void mv(const QString &file1, const QString &file2)
{
#ifdef Q_OS_WIN
    QFile::rename(file1, file2);
#else
    QString cmd = QStringLiteral("mv %1 %2").arg(file1, file2);
    qDebug() << "Command: " << cmd;
    system(cmd.toLocal8Bit().constData());
#endif
}
}

using namespace OCC;

class TestFolderWatcher : public QObject
{
    Q_OBJECT

    const QTemporaryDir _root = TestUtils::createTempDir();
    QString _rootPath;
    QScopedPointer<FolderWatcherForTests> _watcher;
    QScopedPointer<QSignalSpy> _pathChangedSpy;

    bool waitForPathChanged(const QString &path)
    {
        Utility::ChronoElapsedTimer t;
        // Current interval is 10s, if we didn't get a change in 15s something did not work
        // https://github.com/owncloud/client/blob/82b5a1e1bb2b05503e0774d3be5e328c4cd207e2/src/gui/folderwatcher.cpp#L40-L40
        while (t.duration() < 15s) {
            // Check if it was already reported as changed by the watcher
            for (int i = 0; i < _pathChangedSpy->size(); ++i) {
                const auto &args = _pathChangedSpy->at(i);
                if (args.first().value<QSet<QString>>().contains(path))
                    return true;
            }
            // Wait a bit and test again (don't bother checking if we timed out or not)
            _pathChangedSpy->wait(1000);
        }
        return false;
    }

#ifdef Q_OS_LINUX
#define CHECK_WATCH_COUNT(n) QCOMPARE(_watcher->testLinuxWatchCount(), (n))
#else
#define CHECK_WATCH_COUNT(n) do {} while (false)
#endif

public:
    TestFolderWatcher() {
        QDir rootDir(_root.path());
        _rootPath = rootDir.canonicalPath();
        qDebug() << "creating test directory tree in " << _rootPath;

        rootDir.mkpath(QStringLiteral("a1/b1/c1"));
        rootDir.mkpath(QStringLiteral("a1/b1/c2"));
        rootDir.mkpath(QStringLiteral("a1/b2/c1"));
        rootDir.mkpath(QStringLiteral("a1/b3/c3"));
        rootDir.mkpath(QStringLiteral("a2/b3/c3"));
        TestUtils::writeRandomFile(_rootPath + QStringLiteral("/a1/random.bin"));
        TestUtils::writeRandomFile(_rootPath + QStringLiteral("/a1/b2/todelete.bin"));
        TestUtils::writeRandomFile(_rootPath + QStringLiteral("/a2/renamefile"));
        TestUtils::writeRandomFile(_rootPath + QStringLiteral("/a1/movefile"));

        _watcher.reset(new FolderWatcherForTests);
        _watcher->init(_rootPath);
        _pathChangedSpy.reset(new QSignalSpy(_watcher.data(), &FolderWatcher::pathChanged));
    }

    int countFolders(const QString &path)
    {
        int n = 0;
        const auto &entryList = QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &sub : entryList)
            n += 1 + countFolders(path + QLatin1Char('/') + sub);
        return n;
    }

private Q_SLOTS:
    void init()
    {
        _pathChangedSpy->clear();
        CHECK_WATCH_COUNT(countFolders(_rootPath) + 1);
    }

    void cleanup()
    {
        CHECK_WATCH_COUNT(countFolders(_rootPath) + 1);
    }

    void testACreate() { // create a new file
        QString file(_rootPath + QStringLiteral("/foo.txt"));
        QString cmd;
        cmd = QStringLiteral("echo \"xyz\" > %1").arg(file);
        qDebug() << "Command: " << cmd;
        system(cmd.toLocal8Bit().constData());

        QVERIFY(waitForPathChanged(file));
    }

    void testATouch() { // touch an existing file.
        QString file(_rootPath + QStringLiteral("/a1/random.bin"));
        touch(file);
        QVERIFY(waitForPathChanged(file));
    }

    void testCreateADir() {
        QString file(_rootPath + QStringLiteral("/a1/b1/new_dir"));
        mkdir(file);
        QVERIFY(waitForPathChanged(file));

        // Notifications from that new folder arrive too
        QString file2(_rootPath + QStringLiteral("/a1/b1/new_dir/contained"));
        touch(file2);
        QVERIFY(waitForPathChanged(file2));
    }

    void testRemoveADir() {
        QString file(_rootPath + QStringLiteral("/a1/b3/c3"));
        rmdir(file);
        QVERIFY(waitForPathChanged(file));
    }

    void testRemoveAFile() {
        QString file(_rootPath + QStringLiteral("/a1/b2/todelete.bin"));
        QVERIFY(QFile::exists(file));
        rm(file);
        QVERIFY(!QFile::exists(file));

        QVERIFY(waitForPathChanged(file));
    }

    void testRenameAFile() {
        QString file1(_rootPath + QStringLiteral("/a2/renamefile"));
        QString file2(_rootPath + QStringLiteral("/a2/renamefile.renamed"));
        QVERIFY(QFile::exists(file1));
        mv(file1, file2);
        QVERIFY(QFile::exists(file2));

        QVERIFY(waitForPathChanged(file1));
        QVERIFY(waitForPathChanged(file2));
    }

    void testMoveAFile() {
        QString old_file(_rootPath + QStringLiteral("/a1/movefile"));
        QString new_file(_rootPath + QStringLiteral("/a2/movefile.renamed"));
        QVERIFY(QFile::exists(old_file));
        mv(old_file, new_file);
        QVERIFY(QFile::exists(new_file));

        QVERIFY(waitForPathChanged(old_file));
        QVERIFY(waitForPathChanged(new_file));
    }

    void testRenameDirectorySameBase() {
        QString old_file(_rootPath + QStringLiteral("/a1/b1"));
        QString new_file(_rootPath + QStringLiteral("/a1/brename"));
        QVERIFY(QFile::exists(old_file));
        mv(old_file, new_file);
        QVERIFY(QFile::exists(new_file));

        QVERIFY(waitForPathChanged(old_file));
        QVERIFY(waitForPathChanged(new_file));

        // Verify that further notifications end up with the correct paths

        QString file(_rootPath + QStringLiteral("/a1/brename/c1/random.bin"));
        touch(file);
        QVERIFY(waitForPathChanged(file));

        QString dir(_rootPath + QStringLiteral("/a1/brename/newfolder"));
        mkdir(dir);
        QVERIFY(waitForPathChanged(dir));
    }

    void testRenameDirectoryDifferentBase() {
        QString old_file(_rootPath + QStringLiteral("/a1/brename"));
        QString new_file(_rootPath + QStringLiteral("/bren"));
        QVERIFY(QFile::exists(old_file));
        mv(old_file, new_file);
        QVERIFY(QFile::exists(new_file));

        QVERIFY(waitForPathChanged(old_file));
        QVERIFY(waitForPathChanged(new_file));

        // Verify that further notifications end up with the correct paths

        QString file(_rootPath + QStringLiteral("/bren/c1/random.bin"));
        touch(file);
        QVERIFY(waitForPathChanged(file));

        QString dir(_rootPath + QStringLiteral("/bren/newfolder2"));
        mkdir(dir);
        QVERIFY(waitForPathChanged(dir));
    }
};

#ifdef Q_OS_MAC
    QTEST_MAIN(TestFolderWatcher)
#else
    QTEST_GUILESS_MAIN(TestFolderWatcher)
#endif

#include "testfolderwatcher.moc"
