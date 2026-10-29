/*
 * MIPS DSP ASE tests: Add, Subtract and Absolute value instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_absq_s_ph
#include "test_dsp_r1_absq_s_ph.c"
#undef main

#define main test_dsp_r1_absq_s_w
#include "test_dsp_r1_absq_s_w.c"
#undef main

#define main test_dsp_r1_addq_ph
#include "test_dsp_r1_addq_ph.c"
#undef main

#define main test_dsp_r1_addq_s_ph
#include "test_dsp_r1_addq_s_ph.c"
#undef main

#define main test_dsp_r1_addq_s_w
#include "test_dsp_r1_addq_s_w.c"
#undef main

#define main test_dsp_r1_addsc
#include "test_dsp_r1_addsc.c"
#undef main

#define main test_dsp_r1_addu_qb
#include "test_dsp_r1_addu_qb.c"
#undef main

#define main test_dsp_r1_addu_s_qb
#include "test_dsp_r1_addu_s_qb.c"
#undef main

#define main test_dsp_r1_addwc
#include "test_dsp_r1_addwc.c"
#undef main

#define main test_dsp_r1_modsub
#include "test_dsp_r1_modsub.c"
#undef main

#define main test_dsp_r1_raddu_w_qb
#include "test_dsp_r1_raddu_w_qb.c"
#undef main

#define main test_dsp_r1_subq_ph
#include "test_dsp_r1_subq_ph.c"
#undef main

#define main test_dsp_r1_subq_s_ph
#include "test_dsp_r1_subq_s_ph.c"
#undef main

#define main test_dsp_r1_subq_s_w
#include "test_dsp_r1_subq_s_w.c"
#undef main

#define main test_dsp_r1_subu_qb
#include "test_dsp_r1_subu_qb.c"
#undef main

#define main test_dsp_r1_subu_s_qb
#include "test_dsp_r1_subu_s_qb.c"
#undef main

#define main test_dsp_r2_absq_s_qb
#include "test_dsp_r2_absq_s_qb.c"
#undef main

#define main test_dsp_r2_addqh_ph
#include "test_dsp_r2_addqh_ph.c"
#undef main

#define main test_dsp_r2_addqh_r_ph
#include "test_dsp_r2_addqh_r_ph.c"
#undef main

#define main test_dsp_r2_addqh_r_w
#include "test_dsp_r2_addqh_r_w.c"
#undef main

#define main test_dsp_r2_addqh_w
#include "test_dsp_r2_addqh_w.c"
#undef main

#define main test_dsp_r2_addu_ph
#include "test_dsp_r2_addu_ph.c"
#undef main

#define main test_dsp_r2_addu_s_ph
#include "test_dsp_r2_addu_s_ph.c"
#undef main

#define main test_dsp_r2_adduh_qb
#include "test_dsp_r2_adduh_qb.c"
#undef main

#define main test_dsp_r2_adduh_r_qb
#include "test_dsp_r2_adduh_r_qb.c"
#undef main

#define main test_dsp_r2_subqh_ph
#include "test_dsp_r2_subqh_ph.c"
#undef main

#define main test_dsp_r2_subqh_r_ph
#include "test_dsp_r2_subqh_r_ph.c"
#undef main

#define main test_dsp_r2_subqh_r_w
#include "test_dsp_r2_subqh_r_w.c"
#undef main

#define main test_dsp_r2_subqh_w
#include "test_dsp_r2_subqh_w.c"
#undef main

#define main test_dsp_r2_subu_ph
#include "test_dsp_r2_subu_ph.c"
#undef main

#define main test_dsp_r2_subu_s_ph
#include "test_dsp_r2_subu_s_ph.c"
#undef main

#define main test_dsp_r2_subuh_qb
#include "test_dsp_r2_subuh_qb.c"
#undef main

#define main test_dsp_r2_subuh_r_qb
#include "test_dsp_r2_subuh_r_qb.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_absq_s_ph);
    RUN_TEST(test_dsp_r1_absq_s_w);
    RUN_TEST(test_dsp_r1_addq_ph);
    RUN_TEST(test_dsp_r1_addq_s_ph);
    RUN_TEST(test_dsp_r1_addq_s_w);
    RUN_TEST(test_dsp_r1_addsc);
    RUN_TEST(test_dsp_r1_addu_qb);
    RUN_TEST(test_dsp_r1_addu_s_qb);
    RUN_TEST(test_dsp_r1_addwc);
    RUN_TEST(test_dsp_r1_modsub);
    RUN_TEST(test_dsp_r1_raddu_w_qb);
    RUN_TEST(test_dsp_r1_subq_ph);
    RUN_TEST(test_dsp_r1_subq_s_ph);
    RUN_TEST(test_dsp_r1_subq_s_w);
    RUN_TEST(test_dsp_r1_subu_qb);
    RUN_TEST(test_dsp_r1_subu_s_qb);
    RUN_TEST(test_dsp_r2_absq_s_qb);
    RUN_TEST(test_dsp_r2_addqh_ph);
    RUN_TEST(test_dsp_r2_addqh_r_ph);
    RUN_TEST(test_dsp_r2_addqh_r_w);
    RUN_TEST(test_dsp_r2_addqh_w);
    RUN_TEST(test_dsp_r2_addu_ph);
    RUN_TEST(test_dsp_r2_addu_s_ph);
    RUN_TEST(test_dsp_r2_adduh_qb);
    RUN_TEST(test_dsp_r2_adduh_r_qb);
    RUN_TEST(test_dsp_r2_subqh_ph);
    RUN_TEST(test_dsp_r2_subqh_r_ph);
    RUN_TEST(test_dsp_r2_subqh_r_w);
    RUN_TEST(test_dsp_r2_subqh_w);
    RUN_TEST(test_dsp_r2_subu_ph);
    RUN_TEST(test_dsp_r2_subu_s_ph);
    RUN_TEST(test_dsp_r2_subuh_qb);
    RUN_TEST(test_dsp_r2_subuh_r_qb);

    return tap_done();
}
