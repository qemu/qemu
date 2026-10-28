/*
 * MIPS DSP ASE tests: Multiply instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_muleq_s_w_phl
#include "test_dsp_r1_muleq_s_w_phl.c"
#undef main

#define main test_dsp_r1_muleq_s_w_phr
#include "test_dsp_r1_muleq_s_w_phr.c"
#undef main

#define main test_dsp_r1_muleu_s_ph_qbl
#include "test_dsp_r1_muleu_s_ph_qbl.c"
#undef main

#define main test_dsp_r1_muleu_s_ph_qbr
#include "test_dsp_r1_muleu_s_ph_qbr.c"
#undef main

#define main test_dsp_r1_mulq_rs_ph
#include "test_dsp_r1_mulq_rs_ph.c"
#undef main

#define main test_dsp_r2_mul_ph
#include "test_dsp_r2_mul_ph.c"
#undef main

#define main test_dsp_r2_mul_s_ph
#include "test_dsp_r2_mul_s_ph.c"
#undef main

#define main test_dsp_r2_mulq_rs_w
#include "test_dsp_r2_mulq_rs_w.c"
#undef main

#define main test_dsp_r2_mulq_s_ph
#include "test_dsp_r2_mulq_s_ph.c"
#undef main

#define main test_dsp_r2_mulq_s_w
#include "test_dsp_r2_mulq_s_w.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_muleq_s_w_phl);
    RUN_TEST(test_dsp_r1_muleq_s_w_phr);
    RUN_TEST(test_dsp_r1_muleu_s_ph_qbl);
    RUN_TEST(test_dsp_r1_muleu_s_ph_qbr);
    RUN_TEST(test_dsp_r1_mulq_rs_ph);
    RUN_TEST(test_dsp_r2_mul_ph);
    RUN_TEST(test_dsp_r2_mul_s_ph);
    RUN_TEST(test_dsp_r2_mulq_rs_w);
    RUN_TEST(test_dsp_r2_mulq_s_ph);
    RUN_TEST(test_dsp_r2_mulq_s_w);

    return tap_done();
}
