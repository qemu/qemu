/*
 * MIPS DSP ASE tests: Shift instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_shll_ph
#include "test_dsp_r1_shll_ph.c"
#undef main

#define main test_dsp_r1_shll_qb
#include "test_dsp_r1_shll_qb.c"
#undef main

#define main test_dsp_r1_shll_s_ph
#include "test_dsp_r1_shll_s_ph.c"
#undef main

#define main test_dsp_r1_shll_s_w
#include "test_dsp_r1_shll_s_w.c"
#undef main

#define main test_dsp_r1_shllv_ph
#include "test_dsp_r1_shllv_ph.c"
#undef main

#define main test_dsp_r1_shllv_qb
#include "test_dsp_r1_shllv_qb.c"
#undef main

#define main test_dsp_r1_shllv_s_ph
#include "test_dsp_r1_shllv_s_ph.c"
#undef main

#define main test_dsp_r1_shllv_s_w
#include "test_dsp_r1_shllv_s_w.c"
#undef main

#define main test_dsp_r1_shra_ph
#include "test_dsp_r1_shra_ph.c"
#undef main

#define main test_dsp_r1_shra_r_ph
#include "test_dsp_r1_shra_r_ph.c"
#undef main

#define main test_dsp_r1_shra_r_w
#include "test_dsp_r1_shra_r_w.c"
#undef main

#define main test_dsp_r1_shrav_ph
#include "test_dsp_r1_shrav_ph.c"
#undef main

#define main test_dsp_r1_shrav_r_ph
#include "test_dsp_r1_shrav_r_ph.c"
#undef main

#define main test_dsp_r1_shrav_r_w
#include "test_dsp_r1_shrav_r_w.c"
#undef main

#define main test_dsp_r1_shrl_qb
#include "test_dsp_r1_shrl_qb.c"
#undef main

#define main test_dsp_r1_shrlv_qb
#include "test_dsp_r1_shrlv_qb.c"
#undef main

#define main test_dsp_r2_shra_qb
#include "test_dsp_r2_shra_qb.c"
#undef main

#define main test_dsp_r2_shra_r_qb
#include "test_dsp_r2_shra_r_qb.c"
#undef main

#define main test_dsp_r2_shrav_qb
#include "test_dsp_r2_shrav_qb.c"
#undef main

#define main test_dsp_r2_shrav_r_qb
#include "test_dsp_r2_shrav_r_qb.c"
#undef main

#define main test_dsp_r2_shrl_ph
#include "test_dsp_r2_shrl_ph.c"
#undef main

#define main test_dsp_r2_shrlv_ph
#include "test_dsp_r2_shrlv_ph.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_shll_ph);
    RUN_TEST(test_dsp_r1_shll_qb);
    RUN_TEST(test_dsp_r1_shll_s_ph);
    RUN_TEST(test_dsp_r1_shll_s_w);
    RUN_TEST(test_dsp_r1_shllv_ph);
    RUN_TEST(test_dsp_r1_shllv_qb);
    RUN_TEST(test_dsp_r1_shllv_s_ph);
    RUN_TEST(test_dsp_r1_shllv_s_w);
    RUN_TEST(test_dsp_r1_shra_ph);
    RUN_TEST(test_dsp_r1_shra_r_ph);
    RUN_TEST(test_dsp_r1_shra_r_w);
    RUN_TEST(test_dsp_r1_shrav_ph);
    RUN_TEST(test_dsp_r1_shrav_r_ph);
    RUN_TEST(test_dsp_r1_shrav_r_w);
    RUN_TEST(test_dsp_r1_shrl_qb);
    RUN_TEST(test_dsp_r1_shrlv_qb);
    RUN_TEST(test_dsp_r2_shra_qb);
    RUN_TEST(test_dsp_r2_shra_r_qb);
    RUN_TEST(test_dsp_r2_shrav_qb);
    RUN_TEST(test_dsp_r2_shrav_r_qb);
    RUN_TEST(test_dsp_r2_shrl_ph);
    RUN_TEST(test_dsp_r2_shrlv_ph);

    return tap_done();
}
