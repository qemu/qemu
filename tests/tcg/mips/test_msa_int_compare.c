/*
 * MIPS MSA tests: Int Compare instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../include/tap.h"

#define main test_msa_ceq_b
#include "user/ase/msa/int-compare/test_msa_ceq_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ceq_d
#include "user/ase/msa/int-compare/test_msa_ceq_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ceq_h
#include "user/ase/msa/int-compare/test_msa_ceq_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ceq_w
#include "user/ase/msa/int-compare/test_msa_ceq_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_s_b
#include "user/ase/msa/int-compare/test_msa_cle_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_s_d
#include "user/ase/msa/int-compare/test_msa_cle_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_s_h
#include "user/ase/msa/int-compare/test_msa_cle_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_s_w
#include "user/ase/msa/int-compare/test_msa_cle_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_u_b
#include "user/ase/msa/int-compare/test_msa_cle_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_u_d
#include "user/ase/msa/int-compare/test_msa_cle_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_u_h
#include "user/ase/msa/int-compare/test_msa_cle_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_cle_u_w
#include "user/ase/msa/int-compare/test_msa_cle_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_s_b
#include "user/ase/msa/int-compare/test_msa_clt_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_s_d
#include "user/ase/msa/int-compare/test_msa_clt_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_s_h
#include "user/ase/msa/int-compare/test_msa_clt_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_s_w
#include "user/ase/msa/int-compare/test_msa_clt_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_u_b
#include "user/ase/msa/int-compare/test_msa_clt_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_u_d
#include "user/ase/msa/int-compare/test_msa_clt_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_u_h
#include "user/ase/msa/int-compare/test_msa_clt_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_clt_u_w
#include "user/ase/msa/int-compare/test_msa_clt_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "ceq.b", test_msa_ceq_b },
    { "ceq.d", test_msa_ceq_d },
    { "ceq.h", test_msa_ceq_h },
    { "ceq.w", test_msa_ceq_w },
    { "cle.s.b", test_msa_cle_s_b },
    { "cle.s.d", test_msa_cle_s_d },
    { "cle.s.h", test_msa_cle_s_h },
    { "cle.s.w", test_msa_cle_s_w },
    { "cle.u.b", test_msa_cle_u_b },
    { "cle.u.d", test_msa_cle_u_d },
    { "cle.u.h", test_msa_cle_u_h },
    { "cle.u.w", test_msa_cle_u_w },
    { "clt.s.b", test_msa_clt_s_b },
    { "clt.s.d", test_msa_clt_s_d },
    { "clt.s.h", test_msa_clt_s_h },
    { "clt.s.w", test_msa_clt_s_w },
    { "clt.u.b", test_msa_clt_u_b },
    { "clt.u.d", test_msa_clt_u_d },
    { "clt.u.h", test_msa_clt_u_h },
    { "clt.u.w", test_msa_clt_u_w },
};

int main(void)
{
    unsigned i;

    TAP_START();
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        tap_report(!msa_tests[i].fn(), "MSA %s %s",
                   "Int Compare", msa_tests[i].name);
    }

    return tap_done();
}
