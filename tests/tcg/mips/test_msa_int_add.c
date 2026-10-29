/*
 * MIPS MSA tests: Int Add instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_add_a_b
#include "user/ase/msa/int-add/test_msa_add_a_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_add_a_d
#include "user/ase/msa/int-add/test_msa_add_a_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_add_a_h
#include "user/ase/msa/int-add/test_msa_add_a_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_add_a_w
#include "user/ase/msa/int-add/test_msa_add_a_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_a_b
#include "user/ase/msa/int-add/test_msa_adds_a_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_a_d
#include "user/ase/msa/int-add/test_msa_adds_a_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_a_h
#include "user/ase/msa/int-add/test_msa_adds_a_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_a_w
#include "user/ase/msa/int-add/test_msa_adds_a_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_s_b
#include "user/ase/msa/int-add/test_msa_adds_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_s_d
#include "user/ase/msa/int-add/test_msa_adds_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_s_h
#include "user/ase/msa/int-add/test_msa_adds_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_s_w
#include "user/ase/msa/int-add/test_msa_adds_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_u_b
#include "user/ase/msa/int-add/test_msa_adds_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_u_d
#include "user/ase/msa/int-add/test_msa_adds_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_u_h
#include "user/ase/msa/int-add/test_msa_adds_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_adds_u_w
#include "user/ase/msa/int-add/test_msa_adds_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_addv_b
#include "user/ase/msa/int-add/test_msa_addv_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_addv_d
#include "user/ase/msa/int-add/test_msa_addv_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_addv_h
#include "user/ase/msa/int-add/test_msa_addv_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_addv_w
#include "user/ase/msa/int-add/test_msa_addv_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_hadd_s_d
#include "user/ase/msa/int-add/test_msa_hadd_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_hadd_s_h
#include "user/ase/msa/int-add/test_msa_hadd_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_hadd_s_w
#include "user/ase/msa/int-add/test_msa_hadd_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_hadd_u_d
#include "user/ase/msa/int-add/test_msa_hadd_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_hadd_u_h
#include "user/ase/msa/int-add/test_msa_hadd_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_hadd_u_w
#include "user/ase/msa/int-add/test_msa_hadd_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "add.a.b", test_msa_add_a_b },
    { "add.a.d", test_msa_add_a_d },
    { "add.a.h", test_msa_add_a_h },
    { "add.a.w", test_msa_add_a_w },
    { "adds.a.b", test_msa_adds_a_b },
    { "adds.a.d", test_msa_adds_a_d },
    { "adds.a.h", test_msa_adds_a_h },
    { "adds.a.w", test_msa_adds_a_w },
    { "adds.s.b", test_msa_adds_s_b },
    { "adds.s.d", test_msa_adds_s_d },
    { "adds.s.h", test_msa_adds_s_h },
    { "adds.s.w", test_msa_adds_s_w },
    { "adds.u.b", test_msa_adds_u_b },
    { "adds.u.d", test_msa_adds_u_d },
    { "adds.u.h", test_msa_adds_u_h },
    { "adds.u.w", test_msa_adds_u_w },
    { "addv.b", test_msa_addv_b },
    { "addv.d", test_msa_addv_d },
    { "addv.h", test_msa_addv_h },
    { "addv.w", test_msa_addv_w },
    { "hadd.s.d", test_msa_hadd_s_d },
    { "hadd.s.h", test_msa_hadd_s_h },
    { "hadd.s.w", test_msa_hadd_s_w },
    { "hadd.u.d", test_msa_hadd_u_d },
    { "hadd.u.h", test_msa_hadd_u_h },
    { "hadd.u.w", test_msa_hadd_u_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Int Add", msa_tests[i].name);
    }

    return tap_done();
}
