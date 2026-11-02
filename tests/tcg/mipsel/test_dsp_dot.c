/*
 * MIPS DSP ASE tests: Dot Product and Multiply-Accumulate instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_dpaq_s_w_ph
#include "test_dsp_r1_dpaq_s_w_ph.c"
#undef main

#define main test_dsp_r1_dpaq_sa_l_w
#include "test_dsp_r1_dpaq_sa_l_w.c"
#undef main

#define main test_dsp_r1_dpau_h_qbl
#include "test_dsp_r1_dpau_h_qbl.c"
#undef main

#define main test_dsp_r1_dpau_h_qbr
#include "test_dsp_r1_dpau_h_qbr.c"
#undef main

#define main test_dsp_r1_dpsq_s_w_ph
#include "test_dsp_r1_dpsq_s_w_ph.c"
#undef main

#define main test_dsp_r1_dpsq_sa_l_w
#include "test_dsp_r1_dpsq_sa_l_w.c"
#undef main

#define main test_dsp_r1_dpsu_h_qbl
#include "test_dsp_r1_dpsu_h_qbl.c"
#undef main

#define main test_dsp_r1_dpsu_h_qbr
#include "test_dsp_r1_dpsu_h_qbr.c"
#undef main

#define main test_dsp_r1_maq_s_w_phl
#include "test_dsp_r1_maq_s_w_phl.c"
#undef main

#define main test_dsp_r1_maq_s_w_phr
#include "test_dsp_r1_maq_s_w_phr.c"
#undef main

#define main test_dsp_r1_maq_sa_w_phl
#include "test_dsp_r1_maq_sa_w_phl.c"
#undef main

#define main test_dsp_r1_maq_sa_w_phr
#include "test_dsp_r1_maq_sa_w_phr.c"
#undef main

#define main test_dsp_r1_madd
#include "test_dsp_r1_madd.c"
#undef main

#define main test_dsp_r1_maddu
#include "test_dsp_r1_maddu.c"
#undef main

#define main test_dsp_r1_msub
#include "test_dsp_r1_msub.c"
#undef main

#define main test_dsp_r1_msubu
#include "test_dsp_r1_msubu.c"
#undef main

#define main test_dsp_r1_mult
#include "test_dsp_r1_mult.c"
#undef main

#define main test_dsp_r1_multu
#include "test_dsp_r1_multu.c"
#undef main

#define main test_dsp_r2_dpa_w_ph
#include "test_dsp_r2_dpa_w_ph.c"
#undef main

#define main test_dsp_r2_dpaqx_s_w_ph
#include "test_dsp_r2_dpaqx_s_w_ph.c"
#undef main

#define main test_dsp_r2_dpaqx_sa_w_ph
#include "test_dsp_r2_dpaqx_sa_w_ph.c"
#undef main

#define main test_dsp_r2_dpax_w_ph
#include "test_dsp_r2_dpax_w_ph.c"
#undef main

#define main test_dsp_r2_dps_w_ph
#include "test_dsp_r2_dps_w_ph.c"
#undef main

#define main test_dsp_r2_dpsqx_s_w_ph
#include "test_dsp_r2_dpsqx_s_w_ph.c"
#undef main

#define main test_dsp_r2_dpsqx_sa_w_ph
#include "test_dsp_r2_dpsqx_sa_w_ph.c"
#undef main

#define main test_dsp_r2_dpsx_w_ph
#include "test_dsp_r2_dpsx_w_ph.c"
#undef main

#define main test_dsp_r2_mulsa_w_ph
#include "test_dsp_r2_mulsa_w_ph.c"
#undef main

#define main test_dsp_r2_mulsaq_s_w_ph
#include "test_dsp_r2_mulsaq_s_w_ph.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_dpaq_s_w_ph);
    RUN_TEST(test_dsp_r1_dpaq_sa_l_w);
    RUN_TEST(test_dsp_r1_dpau_h_qbl);
    RUN_TEST(test_dsp_r1_dpau_h_qbr);
    RUN_TEST(test_dsp_r1_dpsq_s_w_ph);
    RUN_TEST(test_dsp_r1_dpsq_sa_l_w);
    RUN_TEST(test_dsp_r1_dpsu_h_qbl);
    RUN_TEST(test_dsp_r1_dpsu_h_qbr);
    RUN_TEST(test_dsp_r1_maq_s_w_phl);
    RUN_TEST(test_dsp_r1_maq_s_w_phr);
    RUN_TEST(test_dsp_r1_maq_sa_w_phl);
    RUN_TEST(test_dsp_r1_maq_sa_w_phr);
    RUN_TEST(test_dsp_r1_madd);
    RUN_TEST(test_dsp_r1_maddu);
    RUN_TEST(test_dsp_r1_msub);
    RUN_TEST(test_dsp_r1_msubu);
    RUN_TEST(test_dsp_r1_mult);
    RUN_TEST(test_dsp_r1_multu);
    RUN_TEST(test_dsp_r2_dpa_w_ph);
    RUN_TEST(test_dsp_r2_dpaqx_s_w_ph);
    RUN_TEST(test_dsp_r2_dpaqx_sa_w_ph);
    RUN_TEST(test_dsp_r2_dpax_w_ph);
    RUN_TEST(test_dsp_r2_dps_w_ph);
    RUN_TEST(test_dsp_r2_dpsqx_s_w_ph);
    RUN_TEST(test_dsp_r2_dpsqx_sa_w_ph);
    RUN_TEST(test_dsp_r2_dpsx_w_ph);
    RUN_TEST(test_dsp_r2_mulsa_w_ph);
    RUN_TEST(test_dsp_r2_mulsaq_s_w_ph);

    return tap_done();
}
