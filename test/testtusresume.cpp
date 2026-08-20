/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */
#include "owncloudpropagator_p.h"

#include <QTest>

using namespace OCC;

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
};

QTEST_GUILESS_MAIN(TestTusResume)
#include "testtusresume.moc"
