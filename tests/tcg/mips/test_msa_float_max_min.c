/*
 * MIPS MSA tests: Float Min / Max instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_fmax_a_d
#include "user/ase/msa/float-max-min/test_msa_fmax_a_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmax_a_w
#include "user/ase/msa/float-max-min/test_msa_fmax_a_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmax_d
#include "user/ase/msa/float-max-min/test_msa_fmax_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmax_w
#include "user/ase/msa/float-max-min/test_msa_fmax_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmin_a_d
#include "user/ase/msa/float-max-min/test_msa_fmin_a_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmin_a_w
#include "user/ase/msa/float-max-min/test_msa_fmin_a_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmin_d
#include "user/ase/msa/float-max-min/test_msa_fmin_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_fmin_w
#include "user/ase/msa/float-max-min/test_msa_fmin_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "fmax.a.d", test_msa_fmax_a_d },
    { "fmax.a.w", test_msa_fmax_a_w },
    { "fmax.d", test_msa_fmax_d },
    { "fmax.w", test_msa_fmax_w },
    { "fmin.a.d", test_msa_fmin_a_d },
    { "fmin.a.w", test_msa_fmin_a_w },
    { "fmin.d", test_msa_fmin_d },
    { "fmin.w", test_msa_fmin_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Float Max Min", msa_tests[i].name);
    }

    return tap_done();
}
