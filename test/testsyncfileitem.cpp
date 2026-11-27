/*
 *    This software is in the public domain, furnished "as is", without technical
 *       support, and with no warranty, express or implied, as to its usefulness for
 *          any purpose.
 *          */

#include <QtTest>

#include "syncfileitem.h"

using namespace OCC;
using namespace Qt::StringLiterals;

class TestSyncFileItem : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
    }

    void cleanupTestCase() {
    }

    void testComparator_data() {
        QTest::addColumn<SyncFileItem>("a");
        QTest::addColumn<SyncFileItem>("b");
        QTest::addColumn<SyncFileItem>("c");

        QTest::newRow("a1") << SyncFileItem("client"_L1) << SyncFileItem("client/build"_L1) << SyncFileItem("client-build"_L1);
        QTest::newRow("a2") << SyncFileItem("test/t1"_L1) << SyncFileItem("test/t2"_L1) << SyncFileItem("test/t3"_L1);
        QTest::newRow("a3") << SyncFileItem("ABCD"_L1) << SyncFileItem("abcd"_L1) << SyncFileItem("zzzz"_L1);

        SyncFileItem movedItem1("folder/source/file.f"_L1);
        movedItem1._renameTarget = "folder/destination/file.f"_L1;
        movedItem1.setInstruction(CSYNC_INSTRUCTION_RENAME);

        QTest::newRow("move1") << SyncFileItem("folder/destination"_L1) << movedItem1 << SyncFileItem("folder/destination-2"_L1);
        QTest::newRow("move2") << SyncFileItem("folder/destination/1"_L1) << movedItem1 << SyncFileItem("folder/source"_L1);
        QTest::newRow("move3") << SyncFileItem("abc"_L1) << movedItem1 << SyncFileItem("ijk"_L1);
    }

    void testComparator() {
        QFETCH( SyncFileItem , a );
        QFETCH( SyncFileItem , b );
        QFETCH( SyncFileItem , c );

        QVERIFY(a < b);
        QVERIFY(b < c);
        QVERIFY(a < c);

        QVERIFY(!(b < a));
        QVERIFY(!(c < b));
        QVERIFY(!(c < a));

        QVERIFY(!(a < a));
        QVERIFY(!(b < b));
        QVERIFY(!(c < c));
    }
};

QTEST_GUILESS_MAIN(TestSyncFileItem)
#include "testsyncfileitem.moc"
