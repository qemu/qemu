/*
 * MIPS DSP ASE tests: Indexed Load and DSPControl access instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_bposge32
#include "test_dsp_r1_bposge32.c"
#undef main

#define main test_dsp_r1_lbux
#include "test_dsp_r1_lbux.c"
#undef main

#define main test_dsp_r1_lhx
#include "test_dsp_r1_lhx.c"
#undef main

#define main test_dsp_r1_lwx
#include "test_dsp_r1_lwx.c"
#undef main

#define main test_dsp_r1_rddsp
#include "test_dsp_r1_rddsp.c"
#undef main

#define main test_dsp_r1_wrdsp
#include "test_dsp_r1_wrdsp.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_bposge32);
    RUN_TEST(test_dsp_r1_lbux);
    RUN_TEST(test_dsp_r1_lhx);
    RUN_TEST(test_dsp_r1_lwx);
    RUN_TEST(test_dsp_r1_rddsp);
    RUN_TEST(test_dsp_r1_wrdsp);

    return tap_done();
}
