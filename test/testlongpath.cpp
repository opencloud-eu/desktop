/*
 * libcsync -- a library to sync a directory with another
 *
 * Copyright (c) 2013 by Klaas Freitag <freitag@owncloud.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "common/filesystembase.h"
#include "filesystem.h"
#include "libsync/csync.h"
#include "testutils/testutils.h"

#include <QTemporaryFile>
#include <QTest>

using namespace Qt::StringLiterals;

class TestLongWindowsPath : public QObject
{
    Q_OBJECT

private Q_SLOTS:
#ifdef Q_OS_WIN
    void check_long_win_path()
    {
        {
            const auto path = "C://DATA/FILES/MUSIC/MY_MUSIC.mp3"_L1; // check a short path
            const auto exp_path = "\\\\?\\C:\\\\DATA\\FILES\\MUSIC\\MY_MUSIC.mp3"_L1;
            QString new_short = OCC::FileSystem::longWinPath(path);
            QCOMPARE(new_short, exp_path);
        }

        {
            const auto path = "\\\\foo\\bar/MY_MUSIC.mp3"_L1;
            const auto exp_path = "\\\\foo\\bar\\MY_MUSIC.mp3"_L1;
            QString new_short = OCC::FileSystem::longWinPath(path);
            QCOMPARE(new_short, exp_path);
        }

        {
            const auto path = "//foo\\bar/MY_MUSIC.mp3"_L1;
            const auto exp_path = "\\\\foo\\bar\\MY_MUSIC.mp3"_L1;
            QString new_short = OCC::FileSystem::longWinPath(path);
            QCOMPARE(new_short, exp_path);
        }

        {
            const auto path = "\\foo\\bar"_L1;
            const auto exp_path = "\\\\?\\foo\\bar"_L1;
            QString new_short = OCC::FileSystem::longWinPath(path);
            QCOMPARE(new_short, exp_path);
        }

        {
            const auto path = "/foo/bar"_L1;
            const auto exp_path = "\\\\?\\foo\\bar"_L1;
            QString new_short = OCC::FileSystem::longWinPath(path);
            QCOMPARE(new_short, exp_path);
        }

        const auto longPath = u"D://alonglonglonglong/blonglonglonglong/clonglonglonglong/dlonglonglonglong/"
                              "elonglonglonglong/flonglonglonglong/glonglonglonglong/hlonglonglonglong/ilonglonglonglong/"
                              "jlonglonglonglong/klonglonglonglong/llonglonglonglong/mlonglonglonglong/nlonglonglonglong/"
                              "olonglonglonglong/file.txt"_s;
        const auto longPathConv = u"\\\\?\\D:\\\\alonglonglonglong\\blonglonglonglong\\clonglonglonglong\\dlonglonglonglong\\"
                                  "elonglonglonglong\\flonglonglonglong\\glonglonglonglong\\hlonglonglonglong\\ilonglonglonglong\\"
                                  "jlonglonglonglong\\klonglonglonglong\\llonglonglonglong\\mlonglonglonglong\\nlonglonglonglong\\"
                                  "olonglonglonglong\\file.txt"_s;

        QString new_long = OCC::FileSystem::longWinPath(longPath);
        // printf( "XXXXXXXXXXXX %s %d\n", new_long, mem_reserved);

        QCOMPARE(new_long, longPathConv);

        // printf( "YYYYYYYYYYYY %ld\n", strlen(new_long));
        QCOMPARE(new_long.length(), 286);
    }
#endif


    void testLongPathStat_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("long") << u"/alonglonglonglong/blonglonglonglong/clonglonglonglong/dlonglonglonglong/"
                                 "elonglonglonglong/flonglonglonglong/glonglonglonglong/hlonglonglonglong/ilonglonglonglong/"
                                 "jlonglonglonglong/klonglonglonglong/llonglonglonglong/mlonglonglonglong/nlonglonglonglong/"
                                 "olonglonglonglong/file.txt"_s;
        QTest::newRow("long emoji") << u"/alonglonglonglong/blonglonglonglong/clonglonglonglong/dlonglonglonglong/"
                                       "elonglonglonglong/flonglonglonglong/glonglonglonglong/hlonglonglonglong/ilonglonglonglong/"
                                       "jlonglonglonglong/klonglonglonglong/llonglonglonglong/mlonglonglonglong/nlonglonglonglong/"
                                       "olonglonglonglong/file🐷.txt"_s;
        QTest::newRow("long russian") << u"/alonglonglonglong/blonglonglonglong/clonglonglonglong/dlonglonglonglong/"
                                         "elonglonglonglong/flonglonglonglong/glonglonglonglong/hlonglonglonglong/ilonglonglonglong/"
                                         "jlonglonglonglong/klonglonglonglong/llonglonglonglong/mlonglonglonglong/nlonglonglonglong/"
                                         "olonglonglonglong/собственное.txt"_s;
        QTest::newRow("long arabic") << u"/alonglonglonglong/blonglonglonglong/clonglonglonglong/dlonglonglonglong/"
                                        "elonglonglonglong/flonglonglonglong/glonglonglonglong/hlonglonglonglong/ilonglonglonglong/"
                                        "jlonglonglonglong/klonglonglonglong/llonglonglonglong/mlonglonglonglong/nlonglonglonglong/"
                                        "olonglonglonglong/السحاب.txt"_s;
        QTest::newRow("long chinese") << u"/alonglonglonglong/blonglonglonglong/clonglonglonglong/dlonglonglonglong/"
                                         "elonglonglonglong/flonglonglonglong/glonglonglonglong/hlonglonglonglong/ilonglonglonglong/"
                                         "jlonglonglonglong/klonglonglonglong/llonglonglonglong/mlonglonglonglong/nlonglonglonglong/"
                                         "olonglonglonglong/自己的云.txt"_s;
    }

    void testLongPathStat()
    {
        auto tmp = OCC::TestUtils::createTempDir();
        QFETCH(QString, name);
        const QFileInfo longPath(tmp.path() + name);

        const auto data = QByteArrayLiteral("hello");
        qDebug() << longPath;
        QVERIFY(longPath.dir().mkpath("."_L1));

        QFile file(longPath.filePath());
        QVERIFY(file.open(QFile::WriteOnly));
        QCOMPARE(file.write(data.constData()), data.size());
        file.close();

        const auto size = OCC::FileSystem::getSize(OCC::FileSystem::toFilesystemPath(longPath.filePath()));
        QCOMPARE(size, data.size());
        QCOMPARE(size, longPath.size());

        QVERIFY(tmp.remove());
    }
};

QTEST_GUILESS_MAIN(TestLongWindowsPath)
#include "testlongpath.moc"
