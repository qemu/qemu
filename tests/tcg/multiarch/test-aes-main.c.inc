/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool test_SB_SR(uint8_t *o, const uint8_t *i);
static bool test_MC(uint8_t *o, const uint8_t *i);
static bool test_SB_SR_MC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k);

static bool test_ISB_ISR(uint8_t *o, const uint8_t *i);
static bool test_IMC(uint8_t *o, const uint8_t *i);
static bool test_ISB_ISR_AK_IMC(uint8_t *o, const uint8_t *i, const uint8_t *k);
static bool test_ISB_ISR_IMC_AK(uint8_t *o, const uint8_t *i, const uint8_t *k);

/*
 * From https://doi.org/10.6028/NIST.FIPS.197-upd1,
 * Appendix B -- Cipher Example
 *
 * Note that the formatting of the 4x4 matrices in the document is
 * column-major, whereas C is row-major.  Therefore to get the bytes
 * in the same order as the text, the matrices are transposed.
 *
 * Note that we are not going to test SubBytes or ShiftRows separately,
 * so the "After SubBytes" column is omitted, using only the combined
 * result "After ShiftRows" column.
 */

/* Ease the inline assembly by aligning everything. */
typedef struct {
    uint8_t b[16] __attribute__((aligned(16)));
} State;

typedef struct {
    State start, after_sr, after_mc, round_key;
} Round;

static const Round rounds[] = {
    /* Round 1 */
    { { { 0x19, 0x3d, 0xe3, 0xbe,       /* start */
          0xa0, 0xf4, 0xe2, 0x2b,
          0x9a, 0xc6, 0x8d, 0x2a,
          0xe9, 0xf8, 0x48, 0x08, } },

      { { 0xd4, 0xbf, 0x5d, 0x30,       /* after shiftrows */
          0xe0, 0xb4, 0x52, 0xae,
          0xb8, 0x41, 0x11, 0xf1,
          0x1e, 0x27, 0x98, 0xe5, } },

      { { 0x04, 0x66, 0x81, 0xe5,       /* after mixcolumns */
          0xe0, 0xcb, 0x19, 0x9a,
          0x48, 0xf8, 0xd3, 0x7a,
          0x28, 0x06, 0x26, 0x4c, } },

      { { 0xa0, 0xfa, 0xfe, 0x17,       /* round key */
          0x88, 0x54, 0x2c, 0xb1,
          0x23, 0xa3, 0x39, 0x39,
          0x2a, 0x6c, 0x76, 0x05, } } },

    /* Round 2 */
    { { { 0xa4, 0x9c, 0x7f, 0xf2,       /* start */
          0x68, 0x9f, 0x35, 0x2b,
          0x6b, 0x5b, 0xea, 0x43,
          0x02, 0x6a, 0x50, 0x49, } },

      { { 0x49, 0xdb, 0x87, 0x3b,       /* after shiftrows */
          0x45, 0x39, 0x53, 0x89,
          0x7f, 0x02, 0xd2, 0xf1,
          0x77, 0xde, 0x96, 0x1a, } },

      { { 0x58, 0x4d, 0xca, 0xf1,       /* after mixcolumns */
          0x1b, 0x4b, 0x5a, 0xac,
          0xdb, 0xe7, 0xca, 0xa8,
          0x1b, 0x6b, 0xb0, 0xe5, } },

      { { 0xf2, 0xc2, 0x95, 0xf2,       /* round key */
          0x7a, 0x96, 0xb9, 0x43,
          0x59, 0x35, 0x80, 0x7a,
          0x73, 0x59, 0xf6, 0x7f, } } },

    /* Round 3 */
    { { { 0xaa, 0x8f, 0x5f, 0x03,       /* start */
          0x61, 0xdd, 0xe3, 0xef,
          0x82, 0xd2, 0x4a, 0xd2,
          0x68, 0x32, 0x46, 0x9a, } },

      { { 0xac, 0xc1, 0xd6, 0xb8,       /* after shiftrows */
          0xef, 0xb5, 0x5a, 0x7b,
          0x13, 0x23, 0xcf, 0xdf,
          0x45, 0x73, 0x11, 0xb5, } },

      { { 0x75, 0xec, 0x09, 0x93,       /* after mixcolumns */
          0x20, 0x0b, 0x63, 0x33,
          0x53, 0xc0, 0xcf, 0x7c,
          0xbb, 0x25, 0xd0, 0xdc, } },

      { { 0x3d, 0x80, 0x47, 0x7d,       /* round key */
          0x47, 0x16, 0xfe, 0x3e,
          0x1e, 0x23, 0x7e, 0x44,
          0x6d, 0x7a, 0x88, 0x3b, } } },
};

static void verify_log(const char *prefix, const State *s)
{
    printf("%s:", prefix);
    for (int i = 0; i < sizeof(State); ++i) {
        printf(" %02x", s->b[i]);
    }
    printf("\n");
}

static void verify(const State *ref, const State *tst, const char *which)
{
    if (!memcmp(ref, tst, sizeof(State))) {
        return;
    }

    printf("Mismatch on %s\n", which);
    verify_log("ref", ref);
    verify_log("tst", tst);
    exit(EXIT_FAILURE);
}

int main()
{
    int i, n = sizeof(rounds) / sizeof(Round);
    State t;

    for (i = 0; i < n; ++i) {
        if (test_SB_SR(t.b, rounds[i].start.b)) {
            verify(&rounds[i].after_sr, &t, "SB+SR");
        }
    }

    for (i = 0; i < n; ++i) {
        if (test_MC(t.b, rounds[i].after_sr.b)) {
            verify(&rounds[i].after_mc, &t, "MC");
        }
    }

    /* The kernel of Cipher(). */
    for (i = 0; i < n - 1; ++i) {
        if (test_SB_SR_MC_AK(t.b, rounds[i].start.b, rounds[i].round_key.b)) {
            verify(&rounds[i + 1].start, &t, "SB+SR+MC+AK");
        }
    }

    for (i = 0; i < n; ++i) {
        if (test_ISB_ISR(t.b, rounds[i].after_sr.b)) {
            verify(&rounds[i].start, &t, "ISB+ISR");
        }
    }

    for (i = 0; i < n; ++i) {
        if (test_IMC(t.b, rounds[i].after_mc.b)) {
            verify(&rounds[i].after_sr, &t, "IMC");
        }
    }

    /* The kernel of InvCipher(). */
    for (i = n - 1; i > 0; --i) {
        if (test_ISB_ISR_AK_IMC(t.b, rounds[i].after_sr.b,
                                rounds[i - 1].round_key.b)) {
            verify(&rounds[i - 1].after_sr, &t, "ISB+ISR+AK+IMC");
        }
    }

    /*
     * The kernel of EqInvCipher().
     * We must compute a different round key: apply InvMixColumns to
     * the standard round key, per KeyExpansion vs KeyExpansionEIC.
     */
    for (i = 1; i < n; ++i) {
        if (test_IMC(t.b, rounds[i - 1].round_key.b) &&
            test_ISB_ISR_IMC_AK(t.b, rounds[i].after_sr.b, t.b)) {
            verify(&rounds[i - 1].after_sr, &t, "ISB+ISR+IMC+AK");
        }
    }

    return EXIT_SUCCESS;
}
