/*
 * MIPS MSA tests: Logic and Move instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_and_v
#include "user/ase/msa/logic/test_msa_and_v.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_nor_v
#include "user/ase/msa/logic/test_msa_nor_v.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_or_v
#include "user/ase/msa/logic/test_msa_or_v.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_xor_v
#include "user/ase/msa/logic/test_msa_xor_v.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_move_v
#include "user/ase/msa/move/test_msa_move_v.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "and.v", test_msa_and_v },
    { "nor.v", test_msa_nor_v },
    { "or.v", test_msa_or_v },
    { "xor.v", test_msa_xor_v },
    { "move.v", test_msa_move_v },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Logic and Move", msa_tests[i].name);
    }

    return tap_done();
}
