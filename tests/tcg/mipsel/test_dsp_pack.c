/*
 * MIPS DSP ASE tests: Precision, Pack, Replicate and bit Insert instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dsp_runner.h"

#define main test_dsp_r1_bitrev
#include "test_dsp_r1_bitrev.c"
#undef main

#define main test_dsp_r1_insv
#include "test_dsp_r1_insv.c"
#undef main

#define main test_dsp_r1_packrl_ph
#include "test_dsp_r1_packrl_ph.c"
#undef main

#define main test_dsp_r1_preceq_w_phl
#include "test_dsp_r1_preceq_w_phl.c"
#undef main

#define main test_dsp_r1_preceq_w_phr
#include "test_dsp_r1_preceq_w_phr.c"
#undef main

#define main test_dsp_r1_precequ_ph_qbl
#include "test_dsp_r1_precequ_ph_qbl.c"
#undef main

#define main test_dsp_r1_precequ_ph_qbla
#include "test_dsp_r1_precequ_ph_qbla.c"
#undef main

#define main test_dsp_r1_precequ_ph_qbr
#include "test_dsp_r1_precequ_ph_qbr.c"
#undef main

#define main test_dsp_r1_precequ_ph_qbra
#include "test_dsp_r1_precequ_ph_qbra.c"
#undef main

#define main test_dsp_r1_preceu_ph_qbl
#include "test_dsp_r1_preceu_ph_qbl.c"
#undef main

#define main test_dsp_r1_preceu_ph_qbla
#include "test_dsp_r1_preceu_ph_qbla.c"
#undef main

#define main test_dsp_r1_preceu_ph_qbr
#include "test_dsp_r1_preceu_ph_qbr.c"
#undef main

#define main test_dsp_r1_preceu_ph_qbra
#include "test_dsp_r1_preceu_ph_qbra.c"
#undef main

#define main test_dsp_r1_precrq_ph_w
#include "test_dsp_r1_precrq_ph_w.c"
#undef main

#define main test_dsp_r1_precrq_qb_ph
#include "test_dsp_r1_precrq_qb_ph.c"
#undef main

#define main test_dsp_r1_precrq_rs_ph_w
#include "test_dsp_r1_precrq_rs_ph_w.c"
#undef main

#define main test_dsp_r1_precrqu_s_qb_ph
#include "test_dsp_r1_precrqu_s_qb_ph.c"
#undef main

#define main test_dsp_r1_repl_ph
#include "test_dsp_r1_repl_ph.c"
#undef main

#define main test_dsp_r1_repl_qb
#include "test_dsp_r1_repl_qb.c"
#undef main

#define main test_dsp_r1_replv_ph
#include "test_dsp_r1_replv_ph.c"
#undef main

#define main test_dsp_r1_replv_qb
#include "test_dsp_r1_replv_qb.c"
#undef main

#define main test_dsp_r2_append
#include "test_dsp_r2_append.c"
#undef main

#define main test_dsp_r2_balign
#include "test_dsp_r2_balign.c"
#undef main

#define main test_dsp_r2_precr_qb_ph
#include "test_dsp_r2_precr_qb_ph.c"
#undef main

#define main test_dsp_r2_precr_sra_ph_w
#include "test_dsp_r2_precr_sra_ph_w.c"
#undef main

#define main test_dsp_r2_precr_sra_r_ph_w
#include "test_dsp_r2_precr_sra_r_ph_w.c"
#undef main

#define main test_dsp_r2_prepend
#include "test_dsp_r2_prepend.c"
#undef main

int main(void)
{
    TAP_START();
    RUN_TEST(test_dsp_r1_bitrev);
    RUN_TEST(test_dsp_r1_insv);
    RUN_TEST(test_dsp_r1_packrl_ph);
    RUN_TEST(test_dsp_r1_preceq_w_phl);
    RUN_TEST(test_dsp_r1_preceq_w_phr);
    RUN_TEST(test_dsp_r1_precequ_ph_qbl);
    RUN_TEST(test_dsp_r1_precequ_ph_qbla);
    RUN_TEST(test_dsp_r1_precequ_ph_qbr);
    RUN_TEST(test_dsp_r1_precequ_ph_qbra);
    RUN_TEST(test_dsp_r1_preceu_ph_qbl);
    RUN_TEST(test_dsp_r1_preceu_ph_qbla);
    RUN_TEST(test_dsp_r1_preceu_ph_qbr);
    RUN_TEST(test_dsp_r1_preceu_ph_qbra);
    RUN_TEST(test_dsp_r1_precrq_ph_w);
    RUN_TEST(test_dsp_r1_precrq_qb_ph);
    RUN_TEST(test_dsp_r1_precrq_rs_ph_w);
    RUN_TEST(test_dsp_r1_precrqu_s_qb_ph);
    RUN_TEST(test_dsp_r1_repl_ph);
    RUN_TEST(test_dsp_r1_repl_qb);
    RUN_TEST(test_dsp_r1_replv_ph);
    RUN_TEST(test_dsp_r1_replv_qb);
    RUN_TEST(test_dsp_r2_append);
    RUN_TEST(test_dsp_r2_balign);
    RUN_TEST(test_dsp_r2_precr_qb_ph);
    RUN_TEST(test_dsp_r2_precr_sra_ph_w);
    RUN_TEST(test_dsp_r2_precr_sra_r_ph_w);
    RUN_TEST(test_dsp_r2_prepend);

    return tap_done();
}
