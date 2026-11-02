/*
 * MIPS MSA tests: Interleave instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_ilvev_b
#include "user/ase/msa/interleave/test_msa_ilvev_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvev_d
#include "user/ase/msa/interleave/test_msa_ilvev_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvev_h
#include "user/ase/msa/interleave/test_msa_ilvev_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvev_w
#include "user/ase/msa/interleave/test_msa_ilvev_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvl_b
#include "user/ase/msa/interleave/test_msa_ilvl_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvl_d
#include "user/ase/msa/interleave/test_msa_ilvl_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvl_h
#include "user/ase/msa/interleave/test_msa_ilvl_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvl_w
#include "user/ase/msa/interleave/test_msa_ilvl_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvod_b
#include "user/ase/msa/interleave/test_msa_ilvod_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvod_d
#include "user/ase/msa/interleave/test_msa_ilvod_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvod_h
#include "user/ase/msa/interleave/test_msa_ilvod_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvod_w
#include "user/ase/msa/interleave/test_msa_ilvod_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvr_b
#include "user/ase/msa/interleave/test_msa_ilvr_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvr_d
#include "user/ase/msa/interleave/test_msa_ilvr_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvr_h
#include "user/ase/msa/interleave/test_msa_ilvr_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ilvr_w
#include "user/ase/msa/interleave/test_msa_ilvr_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "ilvev.b", test_msa_ilvev_b },
    { "ilvev.d", test_msa_ilvev_d },
    { "ilvev.h", test_msa_ilvev_h },
    { "ilvev.w", test_msa_ilvev_w },
    { "ilvl.b", test_msa_ilvl_b },
    { "ilvl.d", test_msa_ilvl_d },
    { "ilvl.h", test_msa_ilvl_h },
    { "ilvl.w", test_msa_ilvl_w },
    { "ilvod.b", test_msa_ilvod_b },
    { "ilvod.d", test_msa_ilvod_d },
    { "ilvod.h", test_msa_ilvod_h },
    { "ilvod.w", test_msa_ilvod_w },
    { "ilvr.b", test_msa_ilvr_b },
    { "ilvr.d", test_msa_ilvr_d },
    { "ilvr.h", test_msa_ilvr_h },
    { "ilvr.w", test_msa_ilvr_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Interleave", msa_tests[i].name);
    }

    return tap_done();
}
