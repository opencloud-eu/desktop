/*
 * Tests are taken form lookup2.c and lookup8.c
 * by Bob Jenkins, December 1996, Public Domain.
 *
 * See http://burtleburtle.net/bob/hash/evahash.html
 */

#include <QTest>

#include "common/c_jhash.h"

#define HASHSTATE 1
#define HASHLEN 1
#define MAXPAIR 80
#define MAXLEN 70

inline std::uint8_t operator"" _u8(unsigned long long value)
{
    return static_cast<std::uint8_t>(value);
}

class TestJHash : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void check_c_jhash_trials()
    {
        uint8_t qa[MAXLEN + 1], qb[MAXLEN + 2], *a = &qa[0], *b = &qb[1];
        uint64_t c[HASHSTATE], d[HASHSTATE], i, j = 0, k, l, m, z;
        uint64_t e[HASHSTATE], f[HASHSTATE], g[HASHSTATE], h[HASHSTATE];
        uint64_t x[HASHSTATE], y[HASHSTATE];
        uint64_t hlen;

        for (hlen = 0; hlen < MAXLEN; ++hlen) {
            z = 0;
            for (i = 0; i < hlen; ++i) { /*----------------------- for each input byte, */
                for (j = 0; j < 8; ++j) { /*------------------------ for each input bit, */
                    for (m = 1; m < 8; ++m) { /*------------ for serveral possible initvals, */
                        for (l = 0; l < HASHSTATE; ++l)
                            e[l] = f[l] = g[l] = h[l] = x[l] = y[l] = ~0ull;

                        /*---- check that every output bit is affected by that input bit */
                        for (k = 0; k < MAXPAIR; k += 2) {
                            uint64_t finished = 1;
                            /* keys have one bit different */
                            for (l = 0; l < hlen + 1; ++l) {
                                a[l] = b[l] = 0;
                            }
                            /* have a and b be two keys differing in only one bit */
                            a[i] ^= (k << j);
                            a[i] ^= (k >> (8 - j));
                            c[0] = c_jhash64(a, hlen, m);
                            b[i] ^= ((k + 1) << j);
                            b[i] ^= ((k + 1) >> (8 - j));
                            d[0] = c_jhash64(b, hlen, m);
                            /* check every bit is 1, 0, set, and not set at least once */
                            for (l = 0; l < HASHSTATE; ++l) {
                                e[l] &= (c[l] ^ d[l]);
                                f[l] &= ~(c[l] ^ d[l]);
                                g[l] &= c[l];
                                h[l] &= ~c[l];
                                x[l] &= d[l];
                                y[l] &= ~d[l];
                                if (e[l] | f[l] | g[l] | h[l] | x[l] | y[l])
                                    finished = 0;
                            }
                            if (finished)
                                break;
                        }
                        if (k > z)
                            z = k;
                        if (k == MAXPAIR) {
                            auto format = [](int i) {
                                return QString::number(i, '0', 8);
                            };
                            QFAIL(qPrintable(
                                QStringLiteral("Some bit didn't change: %1 %2 %3 %4 %5 %6  ").arg(format(e[0]), format(f[0]), format(g[0]), format(h[0]), format(x[0]), format(y[0])) //
                                + QStringLiteral("i %1 j %2 m %3 len %4").arg(QString::number(i), QString::number(j), QString::number(m), QString::number(hlen))));
                        }
                        if (z == MAXPAIR) {
                            if (z < MAXPAIR) {
                                QVERIFY(z < MAXPAIR);
                                // print_error("%u trials needed, should be less than 40\n", z/2);
                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    void check_c_jhash_alignment_problems()
    {
        uint64_t test;
        uint8_t buf[MAXLEN + 20], *b;
        uint64_t len;
        uint8_t q[] = "This is the time for all good men to come to the aid of their country";
        uint8_t qq[] = "xThis is the time for all good men to come to the aid of their country";
        uint8_t qqq[] = "xxThis is the time for all good men to come to the aid of their country";
        uint8_t qqqq[] = "xxxThis is the time for all good men to come to the aid of their country";
        uint64_t h, i, j, ref, x, y;


        test = c_jhash64(q, sizeof(q) - 1, 0);
        QCOMPARE(test, c_jhash64(qq + 1, sizeof(q) - 1, 0));
        QCOMPARE(test, c_jhash64(qq + 1, sizeof(q) - 1, 0));
        QCOMPARE(test, c_jhash64(qqq + 2, sizeof(q) - 1, 0));
        QCOMPARE(test, c_jhash64(qqqq + 3, sizeof(q) - 1, 0));
        for (h = 0, b = buf + 1; h < 8; ++h, ++b) {
            for (i = 0; i < MAXLEN; ++i) {
                len = i;
                for (j = 0; j < i; ++j)
                    *(b + j) = 0;

                /* these should all be equal */
                ref = c_jhash64(b, len, 1);
                *(b + i) = ~0_u8;
                *(b - 1) = ~0_u8;
                x = c_jhash64(b, len, 1);
                y = c_jhash64(b, len, 1);
                QVERIFY(!(ref != x) || (ref != y));
            }
        }
    }

    void check_c_jhash_null_strings()
    {
        uint8_t buf[1];
        uint64_t h, i, t;


        buf[0] = ~0_u8;
        for (i = 0, h = 0; i < 8; ++i) {
            t = h;
            h = c_jhash64(buf, 0, h);
            QVERIFY(t != h);
            // print_error("0-byte-string check failed: t = %.8x, h = %.8x", t, h);
        }
    }

    void check_c_jhash64_trials()
    {
        uint8_t qa[MAXLEN + 1], qb[MAXLEN + 2];
        uint8_t *a, *b;
        uint64_t c[HASHSTATE], d[HASHSTATE], i, j = 0, k, l, m, z;
        uint64_t e[HASHSTATE], f[HASHSTATE], g[HASHSTATE], h[HASHSTATE];
        uint64_t x[HASHSTATE], y[HASHSTATE];
        uint64_t hlen;

        a = &qa[0];
        b = &qb[1];

        for (hlen = 0; hlen < MAXLEN; ++hlen) {
            z = 0;
            for (i = 0; i < hlen; ++i) { /*----------------------- for each byte, */
                for (j = 0; j < 8; ++j) { /*------------------------ for each bit, */
                    for (m = 0; m < 8; ++m) { /*-------- for serveral possible levels, */
                        for (l = 0; l < HASHSTATE; ++l)
                            e[l] = f[l] = g[l] = h[l] = x[l] = y[l] = ~0ul;

                        /*---- check that every input bit affects every output bit */
                        for (k = 0; k < MAXPAIR; k += 2) {
                            uint64_t finished = 1;
                            /* keys have one bit different */
                            for (l = 0; l < hlen + 1; ++l) {
                                a[l] = b[l] = 0;
                            }
                            /* have a and b be two keys differing in only one bit */
                            a[i] ^= (k << j);
                            a[i] ^= (k >> (8 - j));
                            c[0] = c_jhash64(a, hlen, m);
                            b[i] ^= ((k + 1) << j);
                            b[i] ^= ((k + 1) >> (8 - j));
                            d[0] = c_jhash64(b, hlen, m);
                            /* check every bit is 1, 0, set, and not set at least once */
                            for (l = 0; l < HASHSTATE; ++l) {
                                e[l] &= (c[l] ^ d[l]);
                                f[l] &= ~(c[l] ^ d[l]);
                                g[l] &= c[l];
                                h[l] &= ~c[l];
                                x[l] &= d[l];
                                y[l] &= ~d[l];
                                if (e[l] | f[l] | g[l] | h[l] | x[l] | y[l])
                                    finished = 0;
                            }
                            if (finished)
                                break;
                        }
                        if (k > z)
                            z = k;
                        if (k == MAXPAIR) {
#if 0
             print_error("Some bit didn't change: ");
             print_error("%.8llx %.8llx %.8llx %.8llx %.8llx %.8llx  ",
                         (long long unsigned int) e[0],
                         (long long unsigned int) f[0],
                         (long long unsigned int) g[0],
                         (long long unsigned int) h[0],
                         (long long unsigned int) x[0],
                         (long long unsigned int) y[0]);
             print_error("i %d j %d m %d len %d\n",
                         i,j,m,hlen);
#endif
                        }
                        if (z == MAXPAIR) {
                            if (z < MAXPAIR) {
#if 0
                  print_error("%lu trials needed, should be less than 40", z/2);
#endif
                                QVERIFY(z < MAXPAIR);
                            }
                            return;
                        }
                    }
                }
            }
        }
    }

    void check_c_jhash64_alignment_problems(void **state)
    {
        uint8_t buf[MAXLEN + 20], *b;
        uint64_t len;
        uint8_t q[] = "This is the time for all good men to come to the aid of their country";
        uint8_t qq[] = "xThis is the time for all good men to come to the aid of their country";
        uint8_t qqq[] = "xxThis is the time for all good men to come to the aid of their country";
        uint8_t qqqq[] = "xxxThis is the time for all good men to come to the aid of their country";
        uint8_t o[] = "xxxxThis is the time for all good men to come to the aid of their country";
        uint8_t oo[] = "xxxxxThis is the time for all good men to come to the aid of their country";
        uint8_t ooo[] = "xxxxxxThis is the time for all good men to come to the aid of their country";
        uint8_t oooo[] = "xxxxxxxThis is the time for all good men to come to the aid of their country";
        uint64_t h, i, j, ref, t, x, y;

        (void)state; /* unused */

        h = c_jhash64(q + 0, (sizeof(q) - 1), 0);
        t = h;
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(qq + 1, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(qqq + 2, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(qqqq + 3, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(o + 4, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(oo + 5, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(ooo + 6, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        h = c_jhash64(oooo + 7, (sizeof(q) - 1), 0);
        QCOMPARE(t, h);
        // , "%.8lx%.8lx\n", h, (h>>32));
        for (h = 0, b = buf + 1; h < 8; ++h, ++b) {
            for (i = 0; i < MAXLEN; ++i) {
                len = i;
                for (j = 0; j < i; ++j)
                    *(b + j) = 0;

                /* these should all be equal */
                ref = c_jhash64(b, len, 1);
                *(b + i) = ~0_u8;
                *(b - 1) = ~0_u8;
                x = c_jhash64(b, len, 1);
                y = c_jhash64(b, len, 1);
                QVERIFY(!(ref != x) || (ref != y));
#if 0
      print_error("alignment error: %.8lx %.8lx %.8lx %ld %ld\n", ref, x, y, h, i);
#endif
            }
        }
    }

    void check_c_jhash64_null_strings()
    {
        uint8_t buf[1];
        uint64_t h, i, t;

        buf[0] = ~0_u8;
        for (i = 0, h = 0; i < 8; ++i) {
            t = h;
            h = c_jhash64(buf, 0, h);
            QVERIFY(t != h);
#if 0
    print_error("0-byte-string check failed: t = %.8lx, h = %.8lx", t, h);
#endif
        }
    }
};


QTEST_APPLESS_MAIN(TestJHash)
#include "testjhash.moc"
