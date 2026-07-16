/*
 *    This software is in the public domain, furnished "as is", without technical
 *    support, and with no warranty, express or implied, as to its usefulness for
 *    any purpose.
 *
 */

#include <QtTest>

#include "creds/jwt.h"

using namespace OCC;

namespace {
QByteArray encodeSegment(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
}

class TestJwt : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testBase64UrlPayload()
    {
        // The base64url encoding of this payload contains both '-' and '_'
        // ('?' and '>' each at a payload offset that is 2 mod 3, RFC 7515 section 2).
        // A standard-alphabet Base64 decode drops these characters and corrupts
        // every byte after them.
        const QByteArray payloadPart = "eyJhdWQiOiJPcGVuQ2xvdWREZXNrdG9wIiwicGljIjoiaHR0cHM6Ly94LmV4YW1wbGUvYWE_dj0xIiwibm90ZSI6ImI-dGFnIn0";
        QVERIFY(payloadPart.contains('_'));
        QVERIFY(payloadPart.contains('-'));

        const JWT jwt("eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9." + payloadPart + ".c2lnbmF0dXJl");
        QVERIFY(jwt.isValid());
        QCOMPARE(jwt.payload().value(QLatin1String("aud")).toString(), QStringLiteral("OpenCloudDesktop"));
        QCOMPARE(jwt.payload().value(QLatin1String("pic")).toString(), QStringLiteral("https://x.example/aa?v=1"));
        QCOMPARE(jwt.payload().value(QLatin1String("note")).toString(), QStringLiteral("b>tag"));
    }

    void testRoundTrip()
    {
        const QJsonObject header = {{QStringLiteral("alg"), QStringLiteral("RS256")}, {QStringLiteral("typ"), QStringLiteral("JWT")}};
        const QJsonObject payload = {{QStringLiteral("aud"), QStringLiteral("OpenCloudDesktop")}, {QStringLiteral("pic"), QStringLiteral("https://x.example/aa?v=1")}};

        const JWT jwt(encodeSegment(header) + "." + encodeSegment(payload) + ".c2lnbmF0dXJl");
        QVERIFY(jwt.isValid());
        QCOMPARE(jwt.header(), header);
        QCOMPARE(jwt.payload(), payload);

        const JWT reparsed(jwt.serialize());
        QVERIFY(reparsed.isValid());
        QCOMPARE(reparsed.payload(), payload);
    }

    void testMalformedToken()
    {
        QVERIFY(!JWT("").isValid());
        QVERIFY(!JWT("onlyonepart").isValid());
        QVERIFY(!JWT("two.parts").isValid());
        QVERIFY(!JWT("not!base64.not!base64.c2lnbmF0dXJl").isValid());
    }

    void testStrictDecodingRejectsInvalidCharacters()
    {
        // An invalid character inside an otherwise valid payload segment: a
        // permissive decode skips it and accepts a silently corrupted token,
        // strict decoding must reject the whole segment.
        const QByteArray payload = encodeSegment({{QStringLiteral("aud"), QStringLiteral("OpenCloudDesktop")}});
        QVERIFY(JWT("eyJhbGciOiJSUzI1NiJ9." + payload + ".c2lnbmF0dXJl").isValid());
        QVERIFY(!JWT("eyJhbGciOiJSUzI1NiJ9." + QByteArray(payload).insert(4, '!') + ".c2lnbmF0dXJl").isValid());
    }
};

QTEST_GUILESS_MAIN(TestJwt)
#include "testjwt.moc"
