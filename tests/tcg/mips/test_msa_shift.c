/*
 * MIPS MSA tests: Shift instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_sll_b
#include "user/ase/msa/shift/test_msa_sll_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sll_d
#include "user/ase/msa/shift/test_msa_sll_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sll_h
#include "user/ase/msa/shift/test_msa_sll_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sll_w
#include "user/ase/msa/shift/test_msa_sll_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sra_b
#include "user/ase/msa/shift/test_msa_sra_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sra_d
#include "user/ase/msa/shift/test_msa_sra_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sra_h
#include "user/ase/msa/shift/test_msa_sra_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_sra_w
#include "user/ase/msa/shift/test_msa_sra_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srar_b
#include "user/ase/msa/shift/test_msa_srar_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srar_d
#include "user/ase/msa/shift/test_msa_srar_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srar_h
#include "user/ase/msa/shift/test_msa_srar_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srar_w
#include "user/ase/msa/shift/test_msa_srar_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srl_b
#include "user/ase/msa/shift/test_msa_srl_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srl_d
#include "user/ase/msa/shift/test_msa_srl_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srl_h
#include "user/ase/msa/shift/test_msa_srl_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srl_w
#include "user/ase/msa/shift/test_msa_srl_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srlr_b
#include "user/ase/msa/shift/test_msa_srlr_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srlr_d
#include "user/ase/msa/shift/test_msa_srlr_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srlr_h
#include "user/ase/msa/shift/test_msa_srlr_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_srlr_w
#include "user/ase/msa/shift/test_msa_srlr_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "sll.b", test_msa_sll_b },
    { "sll.d", test_msa_sll_d },
    { "sll.h", test_msa_sll_h },
    { "sll.w", test_msa_sll_w },
    { "sra.b", test_msa_sra_b },
    { "sra.d", test_msa_sra_d },
    { "sra.h", test_msa_sra_h },
    { "sra.w", test_msa_sra_w },
    { "srar.b", test_msa_srar_b },
    { "srar.d", test_msa_srar_d },
    { "srar.h", test_msa_srar_h },
    { "srar.w", test_msa_srar_w },
    { "srl.b", test_msa_srl_b },
    { "srl.d", test_msa_srl_d },
    { "srl.h", test_msa_srl_h },
    { "srl.w", test_msa_srl_w },
    { "srlr.b", test_msa_srlr_b },
    { "srlr.d", test_msa_srlr_d },
    { "srlr.h", test_msa_srlr_h },
    { "srlr.w", test_msa_srlr_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Shift", msa_tests[i].name);
    }

    return tap_done();
}
