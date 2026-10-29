/*
 * MIPS MSA tests: Int Multiply instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_maddv_b
#include "user/ase/msa/int-multiply/test_msa_maddv_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddv_d
#include "user/ase/msa/int-multiply/test_msa_maddv_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddv_h
#include "user/ase/msa/int-multiply/test_msa_maddv_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddv_w
#include "user/ase/msa/int-multiply/test_msa_maddv_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubv_b
#include "user/ase/msa/int-multiply/test_msa_msubv_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubv_d
#include "user/ase/msa/int-multiply/test_msa_msubv_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubv_h
#include "user/ase/msa/int-multiply/test_msa_msubv_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubv_w
#include "user/ase/msa/int-multiply/test_msa_msubv_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulv_b
#include "user/ase/msa/int-multiply/test_msa_mulv_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulv_d
#include "user/ase/msa/int-multiply/test_msa_mulv_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulv_h
#include "user/ase/msa/int-multiply/test_msa_mulv_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulv_w
#include "user/ase/msa/int-multiply/test_msa_mulv_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "maddv.b", test_msa_maddv_b },
    { "maddv.d", test_msa_maddv_d },
    { "maddv.h", test_msa_maddv_h },
    { "maddv.w", test_msa_maddv_w },
    { "msubv.b", test_msa_msubv_b },
    { "msubv.d", test_msa_msubv_d },
    { "msubv.h", test_msa_msubv_h },
    { "msubv.w", test_msa_msubv_w },
    { "mulv.b", test_msa_mulv_b },
    { "mulv.d", test_msa_mulv_d },
    { "mulv.h", test_msa_mulv_h },
    { "mulv.w", test_msa_mulv_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Int Multiply", msa_tests[i].name);
    }

    return tap_done();
}
