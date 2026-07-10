/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */
#include "owncloudpropagator_p.h"

#include <QTest>

using namespace OCC;

// Regression coverage for classifyError() in owncloudpropagator_p.h. These guard the
// self-heal hardening: a recoverable error must never abort the whole sync run nor be
// finalized as a silent gap -- it must be a per-file retryable status that requests
// another pass. Each case below fails against the pre-hardening classifyError.
class TestClassifyError : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A *transient* connectivity error on one file must NOT abort the entire run.
    // Pre-fix it returned FatalError (-> propagator()->abort()), wedging a large
    // multi-day sync on a single blip. It must be a per-file NormalError + retry.
    void testTransientNetworkErrorIsRetryable()
    {
        for (const auto nerror : {
                 QNetworkReply::ConnectionRefusedError,
                 QNetworkReply::HostNotFoundError,
                 QNetworkReply::TimeoutError,
                 QNetworkReply::TemporaryNetworkFailureError,
                 QNetworkReply::NetworkSessionFailedError,
                 QNetworkReply::ProxyConnectionRefusedError,
                 QNetworkReply::ProxyConnectionClosedError,
                 QNetworkReply::ProxyTimeoutError,
             }) {
            bool anotherSyncNeeded = false;
            QCOMPARE(classifyError(nerror, 0, &anotherSyncNeeded), SyncFileItem::NormalError);
            QVERIFY(anotherSyncNeeded);
        }
    }

    // Genuinely fatal connectivity errors stay FatalError and do not request a retry.
    void testGenuinelyFatalStaysFatal()
    {
        bool anotherSyncNeeded = false;
        QCOMPARE(classifyError(QNetworkReply::SslHandshakeFailedError, 0, &anotherSyncNeeded), SyncFileItem::FatalError);
        QVERIFY(!anotherSyncNeeded);
    }

    // A genuinely retryable server code we already handled stays non-fatal (guard
    // against the hardening accidentally broadening fatality).
    void testLockedStaysNonFatal()
    {
        bool anotherSyncNeeded = false;
        const auto status = classifyError(QNetworkReply::ContentConflictError, 423, &anotherSyncNeeded);
        QVERIFY(status != SyncFileItem::FatalError);
        QVERIFY(anotherSyncNeeded);
    }
};

QTEST_GUILESS_MAIN(TestClassifyError)
#include "testclassifyerror.moc"
