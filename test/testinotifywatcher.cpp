/*
 *    This software is in the public domain, furnished "as is", without technical
 *       support, and with no warranty, express or implied, as to its usefulness for
 *          any purpose.
 *          */

#include <QtTest>

#include "folderwatcher_linux.h"
#include "testutils/testutils.h"


using namespace OCC;
using namespace Qt::StringLiterals;

class TestInotifyWatcher: public FolderWatcherPrivate
{
    Q_OBJECT

private:
    QString _root;

private Q_SLOTS:
    void initTestCase() {
        _root = QDir::tempPath() + "/test_"_L1 + QString::number(QRandomGenerator::global()->generate());
        qDebug() << u"creating test directory tree in " << _root;
        QDir rootDir(_root);

        rootDir.mkpath(_root + "/a1/b1/c1"_L1);
        rootDir.mkpath(_root + "/a1/b1/c2"_L1);
        rootDir.mkpath(_root + "/a1/b2/c1"_L1);
        rootDir.mkpath(_root + "/a1/b3/c3"_L1);
        rootDir.mkpath(_root + "/a2/b3/c3"_L1);
    }

    // Test the recursive path listing function findFoldersBelow
    void testDirsBelowPath() {
        QStringList dirs;

        bool ok = findFoldersBelow(QDir(_root), dirs);
        QVERIFY(dirs.indexOf(_root + "/a1"_L1) > -1);
        QVERIFY(dirs.indexOf(_root + "/a1/b1"_L1) > -1);
        QVERIFY(dirs.indexOf(_root + "/a1/b1/c1"_L1) > -1);
        QVERIFY(dirs.indexOf(_root + "/a1/b1/c2"_L1) > -1);

        QVERIFY(TestUtils::writeRandomFile(_root + "/a1/rand1.dat"_L1));
        QVERIFY(TestUtils::writeRandomFile(_root + "/a1/b1/rand2.dat"_L1));
        QVERIFY(TestUtils::writeRandomFile(_root + "/a1/b1/c1/rand3.dat"_L1));

        QVERIFY(dirs.indexOf(_root + "/a1/b2"_L1) > -1);
        QVERIFY(dirs.indexOf(_root + "/a1/b2/c1"_L1) > -1);
        QVERIFY(dirs.indexOf(_root + "/a1/b3"_L1) > -1);
        QVERIFY(dirs.indexOf(_root + "/a1/b3/c3"_L1) > -1);

        QVERIFY(dirs.indexOf(_root + "/a2"_L1));
        QVERIFY(dirs.indexOf(_root + "/a2/b3"_L1));
        QVERIFY(dirs.indexOf(_root + "/a2/b3/c3"_L1));

        QVERIFY2(dirs.count() == 11, "Directory count wrong.");

        QVERIFY2(ok, "findFoldersBelow failed.");
    }

    void cleanupTestCase() {
        if( _root.startsWith(QDir::tempPath() )) {
            system("rm -rf %1"_L1.arg(_root).toLocal8Bit().constData());
        }
    }
};

QTEST_GUILESS_MAIN(TestInotifyWatcher)
#include "testinotifywatcher.moc"
