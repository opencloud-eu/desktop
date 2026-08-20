/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */
#include <syncengine.h>

#include "testutils/syncenginetestutils.h"
#include "testutils/testutils.h"

#include <QtTest>

using namespace OCC;

// Integration coverage for the self-heal hardening: a *transient* connectivity error on a
// single file must not abort the whole sync run. Pre-fix, classifyError mapped the network
// error to FatalError, which returns up to propagator()->abort() and stops the entire run,
// so every file queued after the failing one was silently never uploaded. Now it is a
// per-file NormalError + another-pass: the healthy files still sync (self-heal), the failing
// one is retried/blacklisted.
class TestSelfHeal : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testTransientUploadErrorDoesNotAbortRun()
    {
        FakeFolder fakeFolder(FileInfo::A12_B12_C12_S12());

        // Serial uploads so the failing file (a unique size) is processed before the
        // healthy ones -> a whole-run abort (the pre-fix behaviour) would leave the
        // healthy files un-synced, which is exactly what this test detects.
        auto opts = fakeFolder.syncEngine().syncOptions();
        opts._parallelNetworkJobs = [] { return 0; };
        fakeFolder.syncEngine().setSyncOptions(opts);

        const int failSize = 137;
        int nFail = 0;
        QObject parent;
        fakeFolder.setServerOverride([&](QNetworkAccessManager::Operation op, const QNetworkRequest &request, QIODevice *) -> QNetworkReply * {
            const QString path = request.url().path();
            if (op == QNetworkAccessManager::PutOperation && path.contains(QLatin1String("a_fatal"))) {
                ++nFail;
                // A transient network-level error (not an HTTP code) on this one file.
                auto *reply = new FakeErrorReply(op, request, &parent, 0);
                reply->setError(QNetworkReply::TimeoutError, QStringLiteral("fake transient timeout"));
                return reply;
            }
            return nullptr; // everything else: normal server behaviour
        });

        // "Z/" so these come after the template dirs; within Z, "a_fatal" sorts first.
        // The local dir must exist before inserting files into it.
        fakeFolder.localModifier().mkdir(QStringLiteral("Z"));
        fakeFolder.localModifier().insert(QStringLiteral("Z/a_fatal"), static_cast<quint64>(failSize));
        fakeFolder.localModifier().insert(QStringLiteral("Z/b_ok"), quint64(100));
        fakeFolder.localModifier().insert(QStringLiteral("Z/c_ok"), quint64(100));

        // The failing file is retried per-file and eventually blacklisted; the healthy
        // files converge. A couple of passes to let any retry settle.
        // The overall result is false (a_fatal errors), which is fine — the discriminator
        // is whether the *healthy* files still made it to the server.
        [[maybe_unused]] const bool pass1 = fakeFolder.applyLocalModificationsAndSync();
        [[maybe_unused]] const bool pass2 = fakeFolder.applyLocalModificationsAndSync();

        QVERIFY2(nFail > 0, "the transient-error injection never fired");
        // Self-heal: the healthy files synced despite the transient failure on a_fatal.
        QVERIFY2(fakeFolder.currentRemoteState().find(QStringLiteral("Z/b_ok")) != nullptr,
            "Z/b_ok was not uploaded -> a transient error on another file aborted the whole run");
        QVERIFY2(fakeFolder.currentRemoteState().find(QStringLiteral("Z/c_ok")) != nullptr,
            "Z/c_ok was not uploaded -> a transient error on another file aborted the whole run");
    }
};

QTEST_GUILESS_MAIN(TestSelfHeal)
#include "testselfheal.moc"
