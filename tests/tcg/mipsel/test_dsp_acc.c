/*
 * MIPS DSP ASE tests: Accumulator access and Extract instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_extp
#include "test_dsp_r1_extp.c"
#undef main

#define main test_dsp_r1_extpdp
#include "test_dsp_r1_extpdp.c"
#undef main

#define main test_dsp_r1_extpdpv
#include "test_dsp_r1_extpdpv.c"
#undef main

#define main test_dsp_r1_extpv
#include "test_dsp_r1_extpv.c"
#undef main

#define main test_dsp_r1_extr_r_w
#include "test_dsp_r1_extr_r_w.c"
#undef main

#define main test_dsp_r1_extr_rs_w
#include "test_dsp_r1_extr_rs_w.c"
#undef main

#define main test_dsp_r1_extr_s_h
#include "test_dsp_r1_extr_s_h.c"
#undef main

#define main test_dsp_r1_extr_w
#include "test_dsp_r1_extr_w.c"
#undef main

#define main test_dsp_r1_extrv_r_w
#include "test_dsp_r1_extrv_r_w.c"
#undef main

#define main test_dsp_r1_extrv_rs_w
#include "test_dsp_r1_extrv_rs_w.c"
#undef main

#define main test_dsp_r1_extrv_s_h
#include "test_dsp_r1_extrv_s_h.c"
#undef main

#define main test_dsp_r1_extrv_w
#include "test_dsp_r1_extrv_w.c"
#undef main

#define main test_dsp_r1_mfhi
#include "test_dsp_r1_mfhi.c"
#undef main

#define main test_dsp_r1_mflo
#include "test_dsp_r1_mflo.c"
#undef main

#define main test_dsp_r1_mthi
#include "test_dsp_r1_mthi.c"
#undef main

#define main test_dsp_r1_mthlip
#include "test_dsp_r1_mthlip.c"
#undef main

#define main test_dsp_r1_mtlo
#include "test_dsp_r1_mtlo.c"
#undef main

#define main test_dsp_r1_shilo
#include "test_dsp_r1_shilo.c"
#undef main

#define main test_dsp_r1_shilov
#include "test_dsp_r1_shilov.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_extp);
    RUN_TEST(test_dsp_r1_extpdp);
    RUN_TEST(test_dsp_r1_extpdpv);
    RUN_TEST(test_dsp_r1_extpv);
    RUN_TEST(test_dsp_r1_extr_r_w);
    RUN_TEST(test_dsp_r1_extr_rs_w);
    RUN_TEST(test_dsp_r1_extr_s_h);
    RUN_TEST(test_dsp_r1_extr_w);
    RUN_TEST(test_dsp_r1_extrv_r_w);
    RUN_TEST(test_dsp_r1_extrv_rs_w);
    RUN_TEST(test_dsp_r1_extrv_s_h);
    RUN_TEST(test_dsp_r1_extrv_w);
    RUN_TEST(test_dsp_r1_mfhi);
    RUN_TEST(test_dsp_r1_mflo);
    RUN_TEST(test_dsp_r1_mthi);
    RUN_TEST(test_dsp_r1_mthlip);
    RUN_TEST(test_dsp_r1_mtlo);
    RUN_TEST(test_dsp_r1_shilo);
    RUN_TEST(test_dsp_r1_shilov);

    return tap_done();
}
