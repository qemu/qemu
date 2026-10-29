/*
 * MIPS MSA tests: Fixed Multiply instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_madd_q_h
#include "user/ase/msa/fixed-multiply/test_msa_madd_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_madd_q_w
#include "user/ase/msa/fixed-multiply/test_msa_madd_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddr_q_h
#include "user/ase/msa/fixed-multiply/test_msa_maddr_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddr_q_w
#include "user/ase/msa/fixed-multiply/test_msa_maddr_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msub_q_h
#include "user/ase/msa/fixed-multiply/test_msa_msub_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msub_q_w
#include "user/ase/msa/fixed-multiply/test_msa_msub_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubr_q_h
#include "user/ase/msa/fixed-multiply/test_msa_msubr_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubr_q_w
#include "user/ase/msa/fixed-multiply/test_msa_msubr_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mul_q_h
#include "user/ase/msa/fixed-multiply/test_msa_mul_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mul_q_w
#include "user/ase/msa/fixed-multiply/test_msa_mul_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulr_q_h
#include "user/ase/msa/fixed-multiply/test_msa_mulr_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulr_q_w
#include "user/ase/msa/fixed-multiply/test_msa_mulr_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "madd.q.h", test_msa_madd_q_h },
    { "madd.q.w", test_msa_madd_q_w },
    { "maddr.q.h", test_msa_maddr_q_h },
    { "maddr.q.w", test_msa_maddr_q_w },
    { "msub.q.h", test_msa_msub_q_h },
    { "msub.q.w", test_msa_msub_q_w },
    { "msubr.q.h", test_msa_msubr_q_h },
    { "msubr.q.w", test_msa_msubr_q_w },
    { "mul.q.h", test_msa_mul_q_h },
    { "mul.q.w", test_msa_mul_q_w },
    { "mulr.q.h", test_msa_mulr_q_h },
    { "mulr.q.w", test_msa_mulr_q_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Fixed Multiply", msa_tests[i].name);
    }

    return tap_done();
}
