/*
 * MIPS MSA tests: Bit Count instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_nloc_b
#include "user/ase/msa/bit-count/test_msa_nloc_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nloc_d
#include "user/ase/msa/bit-count/test_msa_nloc_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nloc_h
#include "user/ase/msa/bit-count/test_msa_nloc_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nloc_w
#include "user/ase/msa/bit-count/test_msa_nloc_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nlzc_b
#include "user/ase/msa/bit-count/test_msa_nlzc_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nlzc_d
#include "user/ase/msa/bit-count/test_msa_nlzc_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nlzc_h
#include "user/ase/msa/bit-count/test_msa_nlzc_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nlzc_w
#include "user/ase/msa/bit-count/test_msa_nlzc_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pcnt_b
#include "user/ase/msa/bit-count/test_msa_pcnt_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pcnt_d
#include "user/ase/msa/bit-count/test_msa_pcnt_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pcnt_h
#include "user/ase/msa/bit-count/test_msa_pcnt_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pcnt_w
#include "user/ase/msa/bit-count/test_msa_pcnt_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "nloc.b", test_msa_nloc_b },
    { "nloc.d", test_msa_nloc_d },
    { "nloc.h", test_msa_nloc_h },
    { "nloc.w", test_msa_nloc_w },
    { "nlzc.b", test_msa_nlzc_b },
    { "nlzc.d", test_msa_nlzc_d },
    { "nlzc.h", test_msa_nlzc_h },
    { "nlzc.w", test_msa_nlzc_w },
    { "pcnt.b", test_msa_pcnt_b },
    { "pcnt.d", test_msa_pcnt_d },
    { "pcnt.h", test_msa_pcnt_h },
    { "pcnt.w", test_msa_pcnt_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Bit Count", msa_tests[i].name);
    }

    return tap_done();
}
