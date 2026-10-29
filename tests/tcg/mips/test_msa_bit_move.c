/*
 * MIPS MSA tests: Bit Move instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_binsl_b
#include "user/ase/msa/bit-move/test_msa_binsl_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsl_d
#include "user/ase/msa/bit-move/test_msa_binsl_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsl_h
#include "user/ase/msa/bit-move/test_msa_binsl_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsl_w
#include "user/ase/msa/bit-move/test_msa_binsl_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsr_b
#include "user/ase/msa/bit-move/test_msa_binsr_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsr_d
#include "user/ase/msa/bit-move/test_msa_binsr_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsr_h
#include "user/ase/msa/bit-move/test_msa_binsr_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_binsr_w
#include "user/ase/msa/bit-move/test_msa_binsr_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bmnz_v
#include "user/ase/msa/bit-move/test_msa_bmnz_v.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bmz_v
#include "user/ase/msa/bit-move/test_msa_bmz_v.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_bsel_v
#include "user/ase/msa/bit-move/test_msa_bsel_v.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "binsl.b", test_msa_binsl_b },
    { "binsl.d", test_msa_binsl_d },
    { "binsl.h", test_msa_binsl_h },
    { "binsl.w", test_msa_binsl_w },
    { "binsr.b", test_msa_binsr_b },
    { "binsr.d", test_msa_binsr_d },
    { "binsr.h", test_msa_binsr_h },
    { "binsr.w", test_msa_binsr_w },
    { "bmnz.v", test_msa_bmnz_v },
    { "bmz.v", test_msa_bmz_v },
    { "bsel.v", test_msa_bsel_v },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Bit Move", msa_tests[i].name);
    }

    return tap_done();
}
