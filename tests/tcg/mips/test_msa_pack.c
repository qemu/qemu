/*
 * MIPS MSA tests: Pack instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_pckev_b
#include "user/ase/msa/pack/test_msa_pckev_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckev_d
#include "user/ase/msa/pack/test_msa_pckev_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckev_h
#include "user/ase/msa/pack/test_msa_pckev_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckev_w
#include "user/ase/msa/pack/test_msa_pckev_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckod_b
#include "user/ase/msa/pack/test_msa_pckod_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckod_d
#include "user/ase/msa/pack/test_msa_pckod_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckod_h
#include "user/ase/msa/pack/test_msa_pckod_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_pckod_w
#include "user/ase/msa/pack/test_msa_pckod_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_vshf_b
#include "user/ase/msa/pack/test_msa_vshf_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_vshf_d
#include "user/ase/msa/pack/test_msa_vshf_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_vshf_h
#include "user/ase/msa/pack/test_msa_vshf_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_vshf_w
#include "user/ase/msa/pack/test_msa_vshf_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "pckev.b", test_msa_pckev_b },
    { "pckev.d", test_msa_pckev_d },
    { "pckev.h", test_msa_pckev_h },
    { "pckev.w", test_msa_pckev_w },
    { "pckod.b", test_msa_pckod_b },
    { "pckod.d", test_msa_pckod_d },
    { "pckod.h", test_msa_pckod_h },
    { "pckod.w", test_msa_pckod_w },
    { "vshf.b", test_msa_vshf_b },
    { "vshf.d", test_msa_vshf_d },
    { "vshf.h", test_msa_vshf_h },
    { "vshf.w", test_msa_vshf_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Pack", msa_tests[i].name);
    }

    return tap_done();
}
