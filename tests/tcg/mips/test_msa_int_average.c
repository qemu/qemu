/*
 * MIPS MSA tests: Int Average instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_ave_s_b
#include "user/ase/msa/int-average/test_msa_ave_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_s_d
#include "user/ase/msa/int-average/test_msa_ave_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_s_h
#include "user/ase/msa/int-average/test_msa_ave_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_s_w
#include "user/ase/msa/int-average/test_msa_ave_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_b
#include "user/ase/msa/int-average/test_msa_ave_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_d
#include "user/ase/msa/int-average/test_msa_ave_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_h
#include "user/ase/msa/int-average/test_msa_ave_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_w
#include "user/ase/msa/int-average/test_msa_ave_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_b
#include "user/ase/msa/int-average/test_msa_aver_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_d
#include "user/ase/msa/int-average/test_msa_aver_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_h
#include "user/ase/msa/int-average/test_msa_aver_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_w
#include "user/ase/msa/int-average/test_msa_aver_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_b
#include "user/ase/msa/int-average/test_msa_aver_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_d
#include "user/ase/msa/int-average/test_msa_aver_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_h
#include "user/ase/msa/int-average/test_msa_aver_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_w
#include "user/ase/msa/int-average/test_msa_aver_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "ave.s.b", test_msa_ave_s_b },
    { "ave.s.d", test_msa_ave_s_d },
    { "ave.s.h", test_msa_ave_s_h },
    { "ave.s.w", test_msa_ave_s_w },
    { "ave.u.b", test_msa_ave_u_b },
    { "ave.u.d", test_msa_ave_u_d },
    { "ave.u.h", test_msa_ave_u_h },
    { "ave.u.w", test_msa_ave_u_w },
    { "aver.s.b", test_msa_aver_s_b },
    { "aver.s.d", test_msa_aver_s_d },
    { "aver.s.h", test_msa_aver_s_h },
    { "aver.s.w", test_msa_aver_s_w },
    { "aver.u.b", test_msa_aver_u_b },
    { "aver.u.d", test_msa_aver_u_d },
    { "aver.u.h", test_msa_aver_u_h },
    { "aver.u.w", test_msa_aver_u_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Int Average", msa_tests[i].name);
    }

    return tap_done();
}
