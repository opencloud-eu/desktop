/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QtTest>
#include "testutils/syncenginetestutils.h"
#include <syncengine.h>

using namespace OCC;
using namespace Qt::StringLiterals;

class TestSyncDelete : public QObject
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

    void testDeleteDirectoryWithNewFile()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        // Remove a directory on the server with new files on the client
        fakeFolder.remoteModifier().remove("A"_L1);
        fakeFolder.localModifier().insert("A/hello.txt"_L1);

        // Symetry
        fakeFolder.localModifier().remove("B"_L1);
        fakeFolder.remoteModifier().insert("B/hello.txt"_L1);

        QVERIFY(fakeFolder.applyLocalModificationsAndSync());

        // A/a1 must be gone because the directory was removed on the server, but hello.txt must be there
        QVERIFY(!fakeFolder.currentRemoteState().find("A/a1"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("A/hello.txt"_L1));

        // Symetry
        QVERIFY(!fakeFolder.currentRemoteState().find("B/b1"_L1));
        QVERIFY(fakeFolder.currentRemoteState().find("B/hello.txt"_L1));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void issue1329()
    {
        QFETCH_GLOBAL(Vfs::Mode, vfsMode);
        QFETCH_GLOBAL(bool, filesAreDehydrated);

        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12(), vfsMode, filesAreDehydrated);

        fakeFolder.localModifier().remove("B"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());

        // Add a directory that was just removed in the previous sync:
        fakeFolder.localModifier().mkdir("B"_L1);
        fakeFolder.localModifier().insert("B/b1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsAndSync());
        QVERIFY(fakeFolder.currentRemoteState().find("B/b1"_L1));
        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }
};

QTEST_GUILESS_MAIN(TestSyncDelete)
#include "testsyncdelete.moc"
