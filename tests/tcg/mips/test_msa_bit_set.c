/*
 * MIPS MSA tests: Bit Set instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_bclr_b
#include "user/ase/msa/bit-set/test_msa_bclr_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bclr_d
#include "user/ase/msa/bit-set/test_msa_bclr_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bclr_h
#include "user/ase/msa/bit-set/test_msa_bclr_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bclr_w
#include "user/ase/msa/bit-set/test_msa_bclr_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bneg_b
#include "user/ase/msa/bit-set/test_msa_bneg_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bneg_d
#include "user/ase/msa/bit-set/test_msa_bneg_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bneg_h
#include "user/ase/msa/bit-set/test_msa_bneg_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bneg_w
#include "user/ase/msa/bit-set/test_msa_bneg_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bset_b
#include "user/ase/msa/bit-set/test_msa_bset_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bset_d
#include "user/ase/msa/bit-set/test_msa_bset_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bset_h
#include "user/ase/msa/bit-set/test_msa_bset_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bset_w
#include "user/ase/msa/bit-set/test_msa_bset_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "bclr.b", test_msa_bclr_b },
    { "bclr.d", test_msa_bclr_d },
    { "bclr.h", test_msa_bclr_h },
    { "bclr.w", test_msa_bclr_w },
    { "bneg.b", test_msa_bneg_b },
    { "bneg.d", test_msa_bneg_d },
    { "bneg.h", test_msa_bneg_h },
    { "bneg.w", test_msa_bneg_w },
    { "bset.b", test_msa_bset_b },
    { "bset.d", test_msa_bset_d },
    { "bset.h", test_msa_bset_h },
    { "bset.w", test_msa_bset_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Bit Set", msa_tests[i].name);
    }

    return tap_done();
}
