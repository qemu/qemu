/*
 * MIPS DSP ASE tests: Compare and Pick instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_cmp_eq_ph
#include "test_dsp_r1_cmp_eq_ph.c"
#undef main

#define main test_dsp_r1_cmp_le_ph
#include "test_dsp_r1_cmp_le_ph.c"
#undef main

#define main test_dsp_r1_cmp_lt_ph
#include "test_dsp_r1_cmp_lt_ph.c"
#undef main

#define main test_dsp_r1_cmpgu_eq_qb
#include "test_dsp_r1_cmpgu_eq_qb.c"
#undef main

#define main test_dsp_r1_cmpgu_le_qb
#include "test_dsp_r1_cmpgu_le_qb.c"
#undef main

#define main test_dsp_r1_cmpgu_lt_qb
#include "test_dsp_r1_cmpgu_lt_qb.c"
#undef main

#define main test_dsp_r1_cmpu_eq_qb
#include "test_dsp_r1_cmpu_eq_qb.c"
#undef main

#define main test_dsp_r1_cmpu_le_qb
#include "test_dsp_r1_cmpu_le_qb.c"
#undef main

#define main test_dsp_r1_cmpu_lt_qb
#include "test_dsp_r1_cmpu_lt_qb.c"
#undef main

#define main test_dsp_r1_pick_ph
#include "test_dsp_r1_pick_ph.c"
#undef main

#define main test_dsp_r1_pick_qb
#include "test_dsp_r1_pick_qb.c"
#undef main

#define main test_dsp_r2_cmpgdu_eq_qb
#include "test_dsp_r2_cmpgdu_eq_qb.c"
#undef main

#define main test_dsp_r2_cmpgdu_le_qb
#include "test_dsp_r2_cmpgdu_le_qb.c"
#undef main

#define main test_dsp_r2_cmpgdu_lt_qb
#include "test_dsp_r2_cmpgdu_lt_qb.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_cmp_eq_ph);
    RUN_TEST(test_dsp_r1_cmp_le_ph);
    RUN_TEST(test_dsp_r1_cmp_lt_ph);
    RUN_TEST(test_dsp_r1_cmpgu_eq_qb);
    RUN_TEST(test_dsp_r1_cmpgu_le_qb);
    RUN_TEST(test_dsp_r1_cmpgu_lt_qb);
    RUN_TEST(test_dsp_r1_cmpu_eq_qb);
    RUN_TEST(test_dsp_r1_cmpu_le_qb);
    RUN_TEST(test_dsp_r1_cmpu_lt_qb);
    RUN_TEST(test_dsp_r1_pick_ph);
    RUN_TEST(test_dsp_r1_pick_qb);
    RUN_TEST(test_dsp_r2_cmpgdu_eq_qb);
    RUN_TEST(test_dsp_r2_cmpgdu_le_qb);
    RUN_TEST(test_dsp_r2_cmpgdu_lt_qb);

    return tap_done();
}
