/*
 * MIPS MSA tests: Int Divide and Modulo instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_div_s_b
#include "user/ase/msa/int-divide/test_msa_div_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_s_d
#include "user/ase/msa/int-divide/test_msa_div_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_s_h
#include "user/ase/msa/int-divide/test_msa_div_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_s_w
#include "user/ase/msa/int-divide/test_msa_div_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_b
#include "user/ase/msa/int-divide/test_msa_div_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_d
#include "user/ase/msa/int-divide/test_msa_div_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_h
#include "user/ase/msa/int-divide/test_msa_div_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_w
#include "user/ase/msa/int-divide/test_msa_div_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_b
#include "user/ase/msa/int-modulo/test_msa_mod_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_d
#include "user/ase/msa/int-modulo/test_msa_mod_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_h
#include "user/ase/msa/int-modulo/test_msa_mod_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_w
#include "user/ase/msa/int-modulo/test_msa_mod_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_b
#include "user/ase/msa/int-modulo/test_msa_mod_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_d
#include "user/ase/msa/int-modulo/test_msa_mod_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_h
#include "user/ase/msa/int-modulo/test_msa_mod_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_w
#include "user/ase/msa/int-modulo/test_msa_mod_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "div.s.b", test_msa_div_s_b },
    { "div.s.d", test_msa_div_s_d },
    { "div.s.h", test_msa_div_s_h },
    { "div.s.w", test_msa_div_s_w },
    { "div.u.b", test_msa_div_u_b },
    { "div.u.d", test_msa_div_u_d },
    { "div.u.h", test_msa_div_u_h },
    { "div.u.w", test_msa_div_u_w },
    { "mod.s.b", test_msa_mod_s_b },
    { "mod.s.d", test_msa_mod_s_d },
    { "mod.s.h", test_msa_mod_s_h },
    { "mod.s.w", test_msa_mod_s_w },
    { "mod.u.b", test_msa_mod_u_b },
    { "mod.u.d", test_msa_mod_u_d },
    { "mod.u.h", test_msa_mod_u_h },
    { "mod.u.w", test_msa_mod_u_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Int Divide and Modulo", msa_tests[i].name);
    }

    return tap_done();
}
