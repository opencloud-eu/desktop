/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include "libsync/csync_exclude.h"
#include "testutils/syncenginetestutils.h"
#include <QtTest>

using namespace Qt::Literals::StringLiterals;

using namespace OCC;

class StatusPushSpy : public QSignalSpy
{
    SyncEngine &_syncEngine;
public:
    StatusPushSpy(SyncEngine &syncEngine)
        : QSignalSpy(&syncEngine.syncFileStatusTracker(), &SyncFileStatusTracker::fileStatusChanged)
        , _syncEngine(syncEngine)
    {
    }

    SyncFileStatus statusOf(const QString &relativePath) const
    {
        const QFileInfo file(_syncEngine.localPath(), relativePath);
        // Start from the end to get the latest status
        for (auto it = crbegin(); it != crend(); ++it) {
            const auto info = QFileInfo(it->at(0).toString());
            if (info == file) {
                return it->at(1).value<SyncFileStatus>();
            }
        }
        return SyncFileStatus();
    }

    bool statusEmittedBefore(const QString &firstPath, const QString &secondPath) const {
        QFileInfo firstFile(_syncEngine.localPath(), firstPath);
        QFileInfo secondFile(_syncEngine.localPath(), secondPath);
        // Start from the end to get the latest status
        int i = size() - 1;
        for (; i >= 0; --i) {
            if (QFileInfo(at(i)[0].toString()) == secondFile)
                break;
            else if (QFileInfo(at(i)[0].toString()) == firstFile)
                return false;
        }
        for (; i >= 0; --i) {
            if (QFileInfo(at(i)[0].toString()) == firstFile)
                return true;
        }
        return false;
    }
};

//
// IMPORTANT: these tests are only run with VFS off, because the tests assume a very controlled order of status changes.
// When using some form of VFS, the event loop has to run in order to fulfill requests from the OS, and therefore things
// like stopping execution in `execUntilBeforePropagation()` are very hard to do.
//
class TestSyncFileStatusTracker : public QObject
{
    Q_OBJECT

    void verifyThatPushMatchesPull(const FakeFolder &fakeFolder, const StatusPushSpy &statusSpy) {
        QString root = fakeFolder.localPath();
        QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next().mid(root.size());
            auto pushedStatus = statusSpy.statusOf(filePath);
            if (pushedStatus != SyncFileStatus()) {
                QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus(filePath), pushedStatus);
            }
        }
    }

private Q_SLOTS:
    void parentsGetSyncStatusUploadDownload() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.localModifier().appendByte("B/b1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        fakeFolder.remoteModifier().appendByte("C/c1"_L1);
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("C/c1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("B/b2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("C/c2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        statusSpy.clear();

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C/c1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void parentsGetSyncStatusNewFileUploadDownload() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.localModifier().insert("B/b0"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        fakeFolder.remoteModifier().insert("C/c0"_L1);
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("C/c0"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("C/c1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        statusSpy.clear();

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C/c0"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void parentsGetSyncStatusNewDirDownload() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.remoteModifier().mkdir("D"_L1);
        fakeFolder.remoteModifier().insert("D/d0"_L1);
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("D"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("D/d0"_L1), SyncFileStatus(SyncFileStatus::StatusSync));

        statusSpy.clear();
        fakeFolder.execUntilItemCompleted("D"_L1);
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("D"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("D/d0"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        statusSpy.clear();
        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("D"_L1), SyncFileStatus(SyncFileStatus::StatusNone));
        QCOMPARE(statusSpy.statusOf("D/d0"_L1), SyncFileStatus(SyncFileStatus::StatusNone));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void parentsGetSyncStatusNewDirUpload() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.localModifier().mkdir("D"_L1);
        fakeFolder.localModifier().insert("D/d0"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("D"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("D/d0"_L1), SyncFileStatus(SyncFileStatus::StatusSync));

        statusSpy.clear();
        fakeFolder.execUntilItemCompleted("D"_L1);
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("D"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("D/d0"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        statusSpy.clear();
        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("D"_L1), SyncFileStatus(SyncFileStatus::StatusNone));
        QCOMPARE(statusSpy.statusOf("D/d0"_L1), SyncFileStatus(SyncFileStatus::StatusNone));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void parentsGetSyncStatusDeleteUpDown() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.remoteModifier().remove("B/b1"_L1);
        fakeFolder.localModifier().remove("C/c1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        // Discovered as remotely removed, pending for local removal.
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("B/b2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("C/c2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        statusSpy.clear();

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void warningStatusForExcludedFile() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.syncEngine().addManualExclude("A/a1"_L1);
        fakeFolder.syncEngine().addManualExclude("B"_L1);
        fakeFolder.localModifier().appendByte("A/a1"_L1);
        fakeFolder.localModifier().appendByte("B/b1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));
        QEXPECT_FAIL("", "csync will stop at ignored directories without traversing children, so we don't currently push the status for newly ignored children of an ignored directory.", Continue);
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));
        QEXPECT_FAIL("", "csync will stop at ignored directories without traversing children, so we don't currently push the status for newly ignored children of an ignored directory.", Continue);
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));
        QEXPECT_FAIL("", "csync will stop at ignored directories without traversing children, so we don't currently push the status for newly ignored children of an ignored directory.", Continue);
        QCOMPARE(statusSpy.statusOf("B/b2"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        statusSpy.clear();

        // Clears the exclude expr above
        fakeFolder.syncEngine().clearManualExcludes();
        fakeFolder.syncEngine().addManualExclude(u".sync_journal.db*"_s);
        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        statusSpy.clear();

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void warningStatusForExcludedFile_CasePreserving() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.syncEngine().addManualExclude("B"_L1);
        fakeFolder.serverErrorPaths().append("A/a1"_L1);
        fakeFolder.localModifier().appendByte("A/a1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        fakeFolder.syncOnce();
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("B"_L1), SyncFileStatus(SyncFileStatus::StatusExcluded));

        // Should still get the status for different casing on macOS and Windows.
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("a"_L1),
            SyncFileStatus(Utility::fsCasePreserving() ? SyncFileStatus::StatusWarning : SyncFileStatus::StatusNone));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/A1"_L1),
            SyncFileStatus(Utility::fsCasePreserving() ? SyncFileStatus::StatusError : SyncFileStatus::StatusNone));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("b"_L1),
            SyncFileStatus(Utility::fsCasePreserving() ? SyncFileStatus::StatusExcluded : SyncFileStatus::StatusNone));
    }

    void parentsGetWarningStatusForError() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.serverErrorPaths().append("A/a1"_L1);
        fakeFolder.serverErrorPaths().append("B/b0"_L1);
        fakeFolder.localModifier().appendByte("A/a1"_L1);
        fakeFolder.localModifier().insert("B/b0"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        statusSpy.clear();

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        statusSpy.clear();

        // Remove the error and start a second sync, the blacklist should kick in
        fakeFolder.serverErrorPaths().clear();
        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        // A/a1 and B/b0 should be on the black list for the next few seconds
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        statusSpy.clear();
        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        statusSpy.clear();

        // Start a third sync, this time together with a real file to sync
        fakeFolder.localModifier().appendByte("C/c1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        // The root should show SYNC even though there is an error underneath,
        // since C/c1 is syncing and the SYNC status has priority.
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("C/c1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        statusSpy.clear();
        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a2"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C/c1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        statusSpy.clear();

        // Another sync after clearing the blacklist entry, everything should return to order.
        fakeFolder.syncEngine().journal()->wipeErrorBlacklistEntry("A/a1"_L1);
        fakeFolder.syncEngine().journal()->wipeErrorBlacklistEntry("B/b0"_L1);
        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        statusSpy.clear();
        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B/b0"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void parentsGetWarningStatusForError_SibblingStartsWithPath() {
        // A is a parent of A/a1, but A/a is not even if it's a substring of A/a1
        // clang-format off
        FakeFolder fakeFolder{{QString{},{
            {"A"_L1, {
                {"a"_L1, 4},
                {"a1"_L1, 4}
            }}}}};
        // clang-format on
        fakeFolder.serverErrorPaths().append("A/a1"_L1);
        fakeFolder.localModifier().appendByte("A/a1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        // The SyncFileStatusTraker won't push any status for all of them, test with a pull.
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));

        fakeFolder.execUntilFinished();
        // We use string matching for paths in the implementation,
        // an error should affect only parents and not every path that starts with the problem path.
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a1"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(fakeFolder.syncEngine().syncFileStatusTracker().fileStatus("A/a"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
    }

    // Even for status pushes immediately following each other, macOS
    // can sometimes have 1s delays between updates, so make sure that
    // children are marked as OK before their parents do.
    void childOKEmittedBeforeParent() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.localModifier().appendByte("B/b1"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        fakeFolder.remoteModifier().appendByte("C/c1"_L1);
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.syncOnce();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QVERIFY(statusSpy.statusEmittedBefore("B/b1"_L1, "B"_L1));
        QVERIFY(statusSpy.statusEmittedBefore("C/c1"_L1, "C"_L1));
        QVERIFY(statusSpy.statusEmittedBefore("B"_L1, QString()));
        QVERIFY(statusSpy.statusEmittedBefore("C"_L1, QString()));
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B/b1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("C/c1"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
    }

    void sharedStatus() {
        SyncFileStatus sharedUpToDateStatus(SyncFileStatus::StatusUpToDate);
        sharedUpToDateStatus.setShared(true);

        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.remoteModifier().insert("S/s0"_L1);
        fakeFolder.remoteModifier().appendByte("S/s1"_L1);
        fakeFolder.remoteModifier().find("A/a1"_L1)->isShared = true; // becomes shared
        fakeFolder.remoteModifier().find("A"_L1, true); // change the etags of the parent

        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        // We don't care about the shared flag for the sync status,
        // Mac and Windows won't show it and we can't know it for new files.
        QCOMPARE(statusSpy.statusOf("S"_L1).tag(), SyncFileStatus::StatusSync);
        QCOMPARE(statusSpy.statusOf("S/s0"_L1).tag(), SyncFileStatus::StatusSync);
        QCOMPARE(statusSpy.statusOf("S/s1"_L1).tag(), SyncFileStatus::StatusSync);

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("S"_L1), sharedUpToDateStatus);
        QEXPECT_FAIL("", "We currently only know if a new file is shared on the second sync, after a PROPFIND.", Continue);
        QCOMPARE(statusSpy.statusOf("S/s0"_L1), sharedUpToDateStatus);
        QCOMPARE(statusSpy.statusOf("S/s1"_L1), sharedUpToDateStatus);
        QCOMPARE(statusSpy.statusOf("B/b1"_L1).shared(), false);
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), sharedUpToDateStatus);

        QCOMPARE(fakeFolder.currentLocalState(), fakeFolder.currentRemoteState());
    }

    void renameError() {
        FakeFolder fakeFolder{FileInfo::A12_B12_C12_S12()};
        fakeFolder.serverErrorPaths().append("A/a1"_L1);
        fakeFolder.localModifier().rename("A/a1"_L1, "A/a1m"_L1);
        fakeFolder.localModifier().rename("B/b1"_L1, "B/b1m"_L1);
        QVERIFY(fakeFolder.applyLocalModificationsWithoutSync());
        StatusPushSpy statusSpy(fakeFolder.syncEngine());

        fakeFolder.scheduleSync();
        fakeFolder.execUntilBeforePropagation();

        verifyThatPushMatchesPull(fakeFolder, statusSpy);

        QCOMPARE(statusSpy.statusOf("A/a1m"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), statusSpy.statusOf("A/a1notexist"_L1));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusSync));
        QCOMPARE(statusSpy.statusOf("B/b1m"_L1), SyncFileStatus(SyncFileStatus::StatusSync));

        fakeFolder.execUntilFinished();
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf("A/a1m"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), statusSpy.statusOf("A/a1notexist"_L1));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        QCOMPARE(statusSpy.statusOf("B/b1m"_L1), SyncFileStatus(SyncFileStatus::StatusUpToDate));
        statusSpy.clear();

        QVERIFY(!fakeFolder.syncOnce());
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        statusSpy.clear();
        QVERIFY(!fakeFolder.syncOnce());
        verifyThatPushMatchesPull(fakeFolder, statusSpy);
        QCOMPARE(statusSpy.statusOf("A/a1m"_L1), SyncFileStatus(SyncFileStatus::StatusError));
        QCOMPARE(statusSpy.statusOf("A/a1"_L1), statusSpy.statusOf("A/a1notexist"_L1));
        QCOMPARE(statusSpy.statusOf("A"_L1), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf(QString()), SyncFileStatus(SyncFileStatus::StatusWarning));
        QCOMPARE(statusSpy.statusOf("B"_L1), SyncFileStatus(SyncFileStatus::StatusNone));
        QCOMPARE(statusSpy.statusOf("B/b1m"_L1), SyncFileStatus(SyncFileStatus::StatusNone));
        statusSpy.clear();
    }

};

QTEST_GUILESS_MAIN(TestSyncFileStatusTracker)
#include "testsyncfilestatustracker.moc"
