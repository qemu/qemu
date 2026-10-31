/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-op-gvec-common.h"
#include "hlx_gvec_tables.h"
#include "exec/helper-gen.h"

/* Saturate signed halfword results to uint16_t. */

static const TCGOpcode hlx_vecop_ushsat_add[] = {
    INDEX_op_add_vec,
    INDEX_op_ssadd_vec,
    INDEX_op_smax_vec,
    INDEX_op_cmp_vec,
    0
};

static const TCGOpcode hlx_vecop_ushsat_sub[] = {
    INDEX_op_sub_vec,
    INDEX_op_sssub_vec,
    INDEX_op_smax_vec,
    INDEX_op_cmp_vec,
    0
};

static const TCGOpcode hlx_vecop_abs_uh_sat[] = {
    INDEX_op_abs_vec,
    INDEX_op_umin_vec,
    0
};

static void hlx_fniv_ushsat_add(unsigned vece, TCGv_vec r,
                                 TCGv_vec a, TCGv_vec b)
{
    TCGv_vec wrap    = tcg_temp_new_vec_matching(r);
    TCGv_vec ssat    = tcg_temp_new_vec_matching(r);
    TCGv_vec pos_ovf = tcg_temp_new_vec_matching(r);
    TCGv_vec pa      = tcg_temp_new_vec_matching(r);
    TCGv_vec pb      = tcg_temp_new_vec_matching(r);
    TCGv_vec zero    = tcg_constant_vec_matching(r, vece, 0);

    tcg_gen_add_vec(vece, wrap, a, b);

    tcg_gen_ssadd_vec(vece, ssat, a, b);
    tcg_gen_smax_vec(vece, ssat, ssat, zero);

    tcg_gen_cmp_vec(TCG_COND_GE, vece, pa, a, zero);
    tcg_gen_cmp_vec(TCG_COND_GE, vece, pb, b, zero);
    tcg_gen_cmp_vec(TCG_COND_LT, vece, pos_ovf, wrap, zero);
    tcg_gen_and_vec(vece, pos_ovf, pos_ovf, pa);
    tcg_gen_and_vec(vece, pos_ovf, pos_ovf, pb);

    tcg_gen_bitsel_vec(vece, r, pos_ovf, wrap, ssat);
}

static void hlx_fniv_ushsat_sub(unsigned vece, TCGv_vec r,
                                 TCGv_vec a, TCGv_vec b)
{
    TCGv_vec wrap    = tcg_temp_new_vec_matching(r);
    TCGv_vec ssat    = tcg_temp_new_vec_matching(r);
    TCGv_vec pos_ovf = tcg_temp_new_vec_matching(r);
    TCGv_vec pa      = tcg_temp_new_vec_matching(r);
    TCGv_vec nb      = tcg_temp_new_vec_matching(r);
    TCGv_vec zero    = tcg_constant_vec_matching(r, vece, 0);

    tcg_gen_sub_vec(vece, wrap, a, b);

    tcg_gen_sssub_vec(vece, ssat, a, b);
    tcg_gen_smax_vec(vece, ssat, ssat, zero);

    /* Positive subtraction overflow needs a >= 0 and b < 0. */
    tcg_gen_cmp_vec(TCG_COND_GE, vece, pa, a, zero);
    tcg_gen_cmp_vec(TCG_COND_LT, vece, nb, b, zero);
    tcg_gen_cmp_vec(TCG_COND_LT, vece, pos_ovf, wrap, zero);
    tcg_gen_and_vec(vece, pos_ovf, pos_ovf, pa);
    tcg_gen_and_vec(vece, pos_ovf, pos_ovf, nb);

    tcg_gen_bitsel_vec(vece, r, pos_ovf, wrap, ssat);
}

static void hlx_fniv_abs_uh_sat(unsigned vece, TCGv_vec r, TCGv_vec a)
{
    /* Clamp abs(INT16_MIN) to INT16_MAX. */
    TCGv_vec imax = tcg_constant_vec_matching(r, vece, 0x7FFF);
    tcg_gen_abs_vec(vece, r, a);
    tcg_gen_umin_vec(vece, r, r, imax);
}

const GVecGen3 hlx_ushsat_add_op = {
    .fniv    = hlx_fniv_ushsat_add,
    .opt_opc = hlx_vecop_ushsat_add,
    .fno     = gen_helper_hlx_gvec_ushsat_add16,
    .vece    = MO_16,
};

const GVecGen3 hlx_ushsat_sub_op = {
    .fniv    = hlx_fniv_ushsat_sub,
    .opt_opc = hlx_vecop_ushsat_sub,
    .fno     = gen_helper_hlx_gvec_ushsat_sub16,
    .vece    = MO_16,
};

const GVecGen2 hlx_abs_uh_sat_op = {
    .fniv    = hlx_fniv_abs_uh_sat,
    .opt_opc = hlx_vecop_abs_uh_sat,
    .fno     = gen_helper_hlx_gvec_abs_uh_sat16,
    .vece    = MO_16,
};

/* Halfword averages. The shift is arithmetic or logical by signedness. */
static void hlx_fniv_avg_sh(unsigned vece, TCGv_vec r,
                             TCGv_vec a, TCGv_vec b)
{
    TCGv_vec ta = tcg_temp_new_vec_matching(r);
    TCGv_vec tb = tcg_temp_new_vec_matching(r);
    tcg_gen_and_vec(vece, ta, a, b);
    tcg_gen_xor_vec(vece, tb, a, b);
    tcg_gen_sari_vec(vece, tb, tb, 1);
    tcg_gen_add_vec(vece, r, ta, tb);
}

static void hlx_fniv_avg_sh_rnd(unsigned vece, TCGv_vec r,
                                 TCGv_vec a, TCGv_vec b)
{
    TCGv_vec ta = tcg_temp_new_vec_matching(r);
    TCGv_vec tb = tcg_temp_new_vec_matching(r);
    tcg_gen_or_vec(vece, ta, a, b);
    tcg_gen_xor_vec(vece, tb, a, b);
    tcg_gen_sari_vec(vece, tb, tb, 1);
    tcg_gen_sub_vec(vece, r, ta, tb);
}

static void hlx_fniv_avg_uh(unsigned vece, TCGv_vec r,
                             TCGv_vec a, TCGv_vec b)
{
    TCGv_vec ta = tcg_temp_new_vec_matching(r);
    TCGv_vec tb = tcg_temp_new_vec_matching(r);
    tcg_gen_and_vec(vece, ta, a, b);
    tcg_gen_xor_vec(vece, tb, a, b);
    tcg_gen_shri_vec(vece, tb, tb, 1);
    tcg_gen_add_vec(vece, r, ta, tb);
}

static void hlx_fniv_avg_uh_rnd(unsigned vece, TCGv_vec r,
                                  TCGv_vec a, TCGv_vec b)
{
    TCGv_vec ta = tcg_temp_new_vec_matching(r);
    TCGv_vec tb = tcg_temp_new_vec_matching(r);
    tcg_gen_or_vec(vece, ta, a, b);
    tcg_gen_xor_vec(vece, tb, a, b);
    tcg_gen_shri_vec(vece, tb, tb, 1);
    tcg_gen_sub_vec(vece, r, ta, tb);
}

static const TCGOpcode hlx_vecop_avg_sh[] = {
    INDEX_op_and_vec,
    INDEX_op_xor_vec,
    INDEX_op_sari_vec,
    INDEX_op_add_vec,
    0
};

static const TCGOpcode hlx_vecop_avg_sh_rnd[] = {
    INDEX_op_or_vec,
    INDEX_op_xor_vec,
    INDEX_op_sari_vec,
    INDEX_op_sub_vec,
    0
};

static const TCGOpcode hlx_vecop_avg_uh[] = {
    INDEX_op_and_vec,
    INDEX_op_xor_vec,
    INDEX_op_shri_vec,
    INDEX_op_add_vec,
    0
};

static const TCGOpcode hlx_vecop_avg_uh_rnd[] = {
    INDEX_op_or_vec,
    INDEX_op_xor_vec,
    INDEX_op_shri_vec,
    INDEX_op_sub_vec,
    0
};

const GVecGen3 hlx_avg_sh_op = {
    .fniv    = hlx_fniv_avg_sh,
    .opt_opc = hlx_vecop_avg_sh,
    .fno     = gen_helper_hlx_gvec_avg_sh16,
    .vece    = MO_16,
};

const GVecGen3 hlx_avg_sh_rnd_op = {
    .fniv    = hlx_fniv_avg_sh_rnd,
    .opt_opc = hlx_vecop_avg_sh_rnd,
    .fno     = gen_helper_hlx_gvec_avg_sh16_rnd,
    .vece    = MO_16,
};

const GVecGen3 hlx_avg_uh_op = {
    .fniv    = hlx_fniv_avg_uh,
    .opt_opc = hlx_vecop_avg_uh,
    .fno     = gen_helper_hlx_gvec_avg_uh16,
    .vece    = MO_16,
};

const GVecGen3 hlx_avg_uh_rnd_op = {
    .fniv    = hlx_fniv_avg_uh_rnd,
    .opt_opc = hlx_vecop_avg_uh_rnd,
    .fno     = gen_helper_hlx_gvec_avg_uh16_rnd,
    .vece    = MO_16,
};

/* Halfword absolute difference as uint16_t. */
static void hlx_fniv_absdiff_sh(unsigned vece, TCGv_vec r,
                                 TCGv_vec a, TCGv_vec b)
{
    TCGv_vec mx = tcg_temp_new_vec_matching(r);
    TCGv_vec mn = tcg_temp_new_vec_matching(r);
    tcg_gen_smax_vec(vece, mx, a, b);
    tcg_gen_smin_vec(vece, mn, a, b);
    tcg_gen_sub_vec(vece, r, mx, mn);
}

static void hlx_fniv_absdiff_uh(unsigned vece, TCGv_vec r,
                                 TCGv_vec a, TCGv_vec b)
{
    TCGv_vec mx = tcg_temp_new_vec_matching(r);
    TCGv_vec mn = tcg_temp_new_vec_matching(r);
    tcg_gen_umax_vec(vece, mx, a, b);
    tcg_gen_umin_vec(vece, mn, a, b);
    tcg_gen_sub_vec(vece, r, mx, mn);
}

static const TCGOpcode hlx_vecop_absdiff_sh[] = {
    INDEX_op_smax_vec,
    INDEX_op_smin_vec,
    INDEX_op_sub_vec,
    0
};

static const TCGOpcode hlx_vecop_absdiff_uh[] = {
    INDEX_op_umax_vec,
    INDEX_op_umin_vec,
    INDEX_op_sub_vec,
    0
};

const GVecGen3 hlx_absdiff_sh_op = {
    .fniv    = hlx_fniv_absdiff_sh,
    .opt_opc = hlx_vecop_absdiff_sh,
    .fno     = gen_helper_hlx_gvec_absdiff_sh16,
    .vece    = MO_16,
};

const GVecGen3 hlx_absdiff_uh_op = {
    .fniv    = hlx_fniv_absdiff_uh,
    .opt_opc = hlx_vecop_absdiff_uh,
    .fno     = gen_helper_hlx_gvec_absdiff_uh16,
    .vece    = MO_16,
};

/* Fill LX_conv_w_h's high register with sign bits. */

static void hlx_fniv_sari15_h(unsigned vece, TCGv_vec r, TCGv_vec a)
{
    tcg_gen_sari_vec(vece, r, a, 15);
}

static const TCGOpcode hlx_vecop_sari15_h[] = {
    INDEX_op_sari_vec,
    0
};

const GVecGen2 hlx_sari15_h_op = {
    .fniv    = hlx_fniv_sari15_h,
    .opt_opc = hlx_vecop_sari15_h,
    .fno     = gen_helper_hlx_gvec_sari15_h16,
    .vece    = MO_16,
};
