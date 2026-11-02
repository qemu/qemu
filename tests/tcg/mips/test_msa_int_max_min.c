/*
 * MIPS MSA tests: Int Min / Max instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_max_a_b
#include "user/ase/msa/int-max-min/test_msa_max_a_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_a_d
#include "user/ase/msa/int-max-min/test_msa_max_a_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_a_h
#include "user/ase/msa/int-max-min/test_msa_max_a_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_a_w
#include "user/ase/msa/int-max-min/test_msa_max_a_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_s_b
#include "user/ase/msa/int-max-min/test_msa_max_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_s_d
#include "user/ase/msa/int-max-min/test_msa_max_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_s_h
#include "user/ase/msa/int-max-min/test_msa_max_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_s_w
#include "user/ase/msa/int-max-min/test_msa_max_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_u_b
#include "user/ase/msa/int-max-min/test_msa_max_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_u_d
#include "user/ase/msa/int-max-min/test_msa_max_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_u_h
#include "user/ase/msa/int-max-min/test_msa_max_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_max_u_w
#include "user/ase/msa/int-max-min/test_msa_max_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_a_b
#include "user/ase/msa/int-max-min/test_msa_min_a_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_a_d
#include "user/ase/msa/int-max-min/test_msa_min_a_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_a_h
#include "user/ase/msa/int-max-min/test_msa_min_a_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_a_w
#include "user/ase/msa/int-max-min/test_msa_min_a_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_s_b
#include "user/ase/msa/int-max-min/test_msa_min_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_s_d
#include "user/ase/msa/int-max-min/test_msa_min_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_s_h
#include "user/ase/msa/int-max-min/test_msa_min_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_s_w
#include "user/ase/msa/int-max-min/test_msa_min_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_u_b
#include "user/ase/msa/int-max-min/test_msa_min_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_u_d
#include "user/ase/msa/int-max-min/test_msa_min_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_u_h
#include "user/ase/msa/int-max-min/test_msa_min_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_min_u_w
#include "user/ase/msa/int-max-min/test_msa_min_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "max.a.b", test_msa_max_a_b },
    { "max.a.d", test_msa_max_a_d },
    { "max.a.h", test_msa_max_a_h },
    { "max.a.w", test_msa_max_a_w },
    { "max.s.b", test_msa_max_s_b },
    { "max.s.d", test_msa_max_s_d },
    { "max.s.h", test_msa_max_s_h },
    { "max.s.w", test_msa_max_s_w },
    { "max.u.b", test_msa_max_u_b },
    { "max.u.d", test_msa_max_u_d },
    { "max.u.h", test_msa_max_u_h },
    { "max.u.w", test_msa_max_u_w },
    { "min.a.b", test_msa_min_a_b },
    { "min.a.d", test_msa_min_a_d },
    { "min.a.h", test_msa_min_a_h },
    { "min.a.w", test_msa_min_a_w },
    { "min.s.b", test_msa_min_s_b },
    { "min.s.d", test_msa_min_s_d },
    { "min.s.h", test_msa_min_s_h },
    { "min.s.w", test_msa_min_s_w },
    { "min.u.b", test_msa_min_u_b },
    { "min.u.d", test_msa_min_u_d },
    { "min.u.h", test_msa_min_u_h },
    { "min.u.w", test_msa_min_u_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Int Max Min", msa_tests[i].name);
    }

    return tap_done();
}
