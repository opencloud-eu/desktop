/*
 * c_jhash.c Jenkins Hash
 *
 * Copyright (c) 1997 Bob Jenkins <bob_jenkins@burtleburtle.net>
 *
 * lookup8.c, by Bob Jenkins, January 4 1997, Public Domain.
 * hash(), hash2(), hash3, and _c_mix() are externally useful functions.
 * Routines to test the hash are included if SELF_TEST is defined.
 * You can use this free for any purpose.  It has no warranty.
 *
 * See http://burtleburtle.net/bob/hash/evahash.html
 */

/**
 * @file common/c_jhash.h
 *
 * @brief Interface of the cynapses jhash implementation
 *
 * @defgroup cynJHashInternals cynapses libc jhash function
 * @ingroup cynLibraryAPI
 *
 * @{
 */
#ifndef _C_JHASH_H
#define _C_JHASH_H

#include <QtCore/qglobal.h>
#include <stdint.h>

/**
 * _c_mix64 -- Mix 3 64-bit values reversibly.
 *
 * _c_mix64() takes 48 machine instructions, but only 24 cycles on a superscalar
 * machine (like Intel's new MMX architecture).  It requires 4 64-bit
 * registers for 4::2 parallelism.
 * All 1-bit deltas, all 2-bit deltas, all deltas composed of top bits of
 * (a,b,c), and all deltas of bottom bits were tested.  All deltas were
 * tested both on random keys and on keys that were nearly all zero.
 * These deltas all cause every bit of c to change between 1/3 and 2/3
 * of the time (well, only 113/400 to 287/400 of the time for some
 * 2-bit delta).  These deltas all cause at least 80 bits to change
 * among (a,b,c) when the _c_mix is run either forward or backward (yes it
 * is reversible).
 * This implies that a hash using _c_mix64 has no funnels.  There may be
 * characteristics with 3-bit deltas or bigger, I didn't test for
 * those.
 */
#define _c_mix64(a, b, c)                                                                                                                                      \
    {                                                                                                                                                          \
        a -= b;                                                                                                                                                \
        a -= c;                                                                                                                                                \
        a ^= (c >> 43);                                                                                                                                        \
        b -= c;                                                                                                                                                \
        b -= a;                                                                                                                                                \
        b ^= (a << 9);                                                                                                                                         \
        c -= a;                                                                                                                                                \
        c -= b;                                                                                                                                                \
        c ^= (b >> 8);                                                                                                                                         \
        a -= b;                                                                                                                                                \
        a -= c;                                                                                                                                                \
        a ^= (c >> 38);                                                                                                                                        \
        b -= c;                                                                                                                                                \
        b -= a;                                                                                                                                                \
        b ^= (a << 23);                                                                                                                                        \
        c -= a;                                                                                                                                                \
        c -= b;                                                                                                                                                \
        c ^= (b >> 5);                                                                                                                                         \
        a -= b;                                                                                                                                                \
        a -= c;                                                                                                                                                \
        a ^= (c >> 35);                                                                                                                                        \
        b -= c;                                                                                                                                                \
        b -= a;                                                                                                                                                \
        b ^= (a << 49);                                                                                                                                        \
        c -= a;                                                                                                                                                \
        c -= b;                                                                                                                                                \
        c ^= (b >> 11);                                                                                                                                        \
        a -= b;                                                                                                                                                \
        a -= c;                                                                                                                                                \
        a ^= (c >> 12);                                                                                                                                        \
        b -= c;                                                                                                                                                \
        b -= a;                                                                                                                                                \
        b ^= (a << 18);                                                                                                                                        \
        c -= a;                                                                                                                                                \
        c -= b;                                                                                                                                                \
        c ^= (b >> 22);                                                                                                                                        \
    }

/**
 * @brief hash a variable-length key into a 64-bit value
 *
 * The best hash table sizes are powers of 2.  There is no need to do
 * mod a prime (mod is sooo slow!).  If you need less than 64 bits,
 * use a bitmask.  For example, if you need only 10 bits, do
 *   h = (h & hashmask(10));
 * In which case, the hash table should have hashsize(10) elements.
 *
 * Use for hash table lookup, or anything where one collision in 2^^64
 * is acceptable.  Do NOT use for cryptographic purposes.
 *
 * @param k       The key (the unaligned variable-length array of bytes).
 * @param length  The length of the key, counting by bytes.
 * @param intval  Initial value, can be any 8-byte value.
 *
 * @return    A 64-bit value. Every bit of the key affects every bit of
 *            the return value.  No funnels.  Every 1-bit and 2-bit delta
 *            achieves avalanche. About 41+5len instructions.
 */
static inline uint64_t c_jhash64(const uint8_t *k, uint64_t length, uint64_t intval)
{
    uint64_t a, b, c, len;

    /* Set up the internal state */
    len = length;
    a = b = intval; /* the previous hash value */
    c = 0x9e3779b97f4a7c13LL; /* the golden ratio; an arbitrary value */

    /* handle most of the key */
    while (len >= 24) {
        a += (k[0] + ((uint64_t)k[1] << 8) + ((uint64_t)k[2] << 16) + ((uint64_t)k[3] << 24) + ((uint64_t)k[4] << 32) + ((uint64_t)k[5] << 40)
            + ((uint64_t)k[6] << 48) + ((uint64_t)k[7] << 56));
        b += (k[8] + ((uint64_t)k[9] << 8) + ((uint64_t)k[10] << 16) + ((uint64_t)k[11] << 24) + ((uint64_t)k[12] << 32) + ((uint64_t)k[13] << 40)
            + ((uint64_t)k[14] << 48) + ((uint64_t)k[15] << 56));
        c += (k[16] + ((uint64_t)k[17] << 8) + ((uint64_t)k[18] << 16) + ((uint64_t)k[19] << 24) + ((uint64_t)k[20] << 32) + ((uint64_t)k[21] << 40)
            + ((uint64_t)k[22] << 48) + ((uint64_t)k[23] << 56));
        _c_mix64(a, b, c);
        k += 24;
        len -= 24;
    }

    /* handle the last 23 bytes */
    c += length;
    switch (len) {
    case 23:
        c += ((uint64_t)k[22] << 56);
        Q_FALLTHROUGH();
    case 22:
        c += ((uint64_t)k[21] << 48);
        Q_FALLTHROUGH();
    case 21:
        c += ((uint64_t)k[20] << 40);
        Q_FALLTHROUGH();
    case 20:
        c += ((uint64_t)k[19] << 32);
        Q_FALLTHROUGH();
    case 19:
        c += ((uint64_t)k[18] << 24);
        Q_FALLTHROUGH();
    case 18:
        c += ((uint64_t)k[17] << 16);
        Q_FALLTHROUGH();
    case 17:
        c += ((uint64_t)k[16] << 8);
        Q_FALLTHROUGH();
    /* the first byte of c is reserved for the length */
    case 16:
        b += ((uint64_t)k[15] << 56);
        Q_FALLTHROUGH();
    case 15:
        b += ((uint64_t)k[14] << 48);
        Q_FALLTHROUGH();
    case 14:
        b += ((uint64_t)k[13] << 40);
        Q_FALLTHROUGH();
    case 13:
        b += ((uint64_t)k[12] << 32);
        Q_FALLTHROUGH();
    case 12:
        b += ((uint64_t)k[11] << 24);
        Q_FALLTHROUGH();
    case 11:
        b += ((uint64_t)k[10] << 16);
        Q_FALLTHROUGH();
    case 10:
        b += ((uint64_t)k[9] << 8);
        Q_FALLTHROUGH();
    case 9:
        b += ((uint64_t)k[8]);
        Q_FALLTHROUGH();
    case 8:
        a += ((uint64_t)k[7] << 56);
        Q_FALLTHROUGH();
    case 7:
        a += ((uint64_t)k[6] << 48);
        Q_FALLTHROUGH();
    case 6:
        a += ((uint64_t)k[5] << 40);
        Q_FALLTHROUGH();
    case 5:
        a += ((uint64_t)k[4] << 32);
        Q_FALLTHROUGH();
    case 4:
        a += ((uint64_t)k[3] << 24);
        Q_FALLTHROUGH();
    case 3:
        a += ((uint64_t)k[2] << 16);
        Q_FALLTHROUGH();
    case 2:
        a += ((uint64_t)k[1] << 8);
        Q_FALLTHROUGH();
    case 1:
        a += ((uint64_t)k[0]);
        /* case 0: nothing left to add */
    }
    _c_mix64(a, b, c);

    return c;
}

/**
 * }@
 */
#endif /* _C_JHASH_H */
