/*
 *  ARM translation: AArch32 VFP instructions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Copyright (c) 2007 OpenedHand, Ltd.
 *  Copyright (c) 2019 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This file is intended to be included from translate.c; it uses
 * some macros and definitions provided by that file.
 * It might be possible to convert it to a standalone .c file eventually.
 */

/* Include the generated VFP decoder */
#include "decode-vfp.inc.c"
#include "decode-vfp-uncond.inc.c"

/*
 * Check that VFP access is enabled. If it is, do the necessary
 * M-profile lazy-FP handling and then return true.
 * If not, emit code to generate an appropriate exception and
 * return false.
 * The ignore_vfp_enabled argument specifies that we should ignore
 * whether VFP is enabled via FPEXC[EN]: this should be true for FMXR/FMRX
 * accesses to FPSID, FPEXC, MVFR0, MVFR1, MVFR2, and false for all other insns.
 */
static bool full_vfp_access_check(DisasContext *s, bool ignore_vfp_enabled)
{
    if (s->fp_excp_el) {
        if (arm_dc_feature(s, ARM_FEATURE_M)) {
            gen_exception_insn(s, 4, EXCP_NOCP, syn_uncategorized(),
                               s->fp_excp_el);
        } else {
            gen_exception_insn(s, 4, EXCP_UDEF,
                               syn_fp_access_trap(1, 0xe, false),
                               s->fp_excp_el);
        }
        return false;
    }

    if (!s->vfp_enabled && !ignore_vfp_enabled) {
        assert(!arm_dc_feature(s, ARM_FEATURE_M));
        gen_exception_insn(s, 4, EXCP_UDEF, syn_uncategorized(),
                           default_exception_el(s));
        return false;
    }

    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        /* Handle M-profile lazy FP state mechanics */

        /* Trigger lazy-state preservation if necessary */
        if (s->v7m_lspact) {
            /*
             * Lazy state saving affects external memory and also the NVIC,
             * so we must mark it as an IO operation for icount.
             */
            if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
                gen_io_start();
            }
            gen_helper_v7m_preserve_fp_state(cpu_env);
            if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
                gen_io_end();
            }
            /*
             * If the preserve_fp_state helper doesn't throw an exception
             * then it will clear LSPACT; we don't need to repeat this for
             * any further FP insns in this TB.
             */
            s->v7m_lspact = false;
        }

        /* Update ownership of FP context: set FPCCR.S to match current state */
        if (s->v8m_fpccr_s_wrong) {
            TCGv_i32 tmp;

            tmp = load_cpu_field(v7m.fpccr[M_REG_S]);
            if (s->v8m_secure) {
                tcg_gen_ori_i32(tmp, tmp, R_V7M_FPCCR_S_MASK);
            } else {
                tcg_gen_andi_i32(tmp, tmp, ~R_V7M_FPCCR_S_MASK);
            }
            store_cpu_field(tmp, v7m.fpccr[M_REG_S]);
            /* Don't need to do this for any further FP insns in this TB */
            s->v8m_fpccr_s_wrong = false;
        }

        if (s->v7m_new_fp_ctxt_needed) {
            /*
             * Create new FP context by updating CONTROL.FPCA, CONTROL.SFPA
             * and the FPSCR.
             */
            TCGv_i32 control, fpscr;
            uint32_t bits = R_V7M_CONTROL_FPCA_MASK;

            fpscr = load_cpu_field(v7m.fpdscr[s->v8m_secure]);
            gen_helper_vfp_set_fpscr(cpu_env, fpscr);
            tcg_temp_free_i32(fpscr);
            /*
             * We don't need to arrange to end the TB, because the only
             * parts of FPSCR which we cache in the TB flags are the VECLEN
             * and VECSTRIDE, and those don't exist for M-profile.
             */

            if (s->v8m_secure) {
                bits |= R_V7M_CONTROL_SFPA_MASK;
            }
            control = load_cpu_field(v7m.control[M_REG_S]);
            tcg_gen_ori_i32(control, control, bits);
            store_cpu_field(control, v7m.control[M_REG_S]);
            /* Don't need to do this for any further FP insns in this TB */
            s->v7m_new_fp_ctxt_needed = false;
        }
    }

    return true;
}

/*
 * The most usual kind of VFP access check, for everything except
 * FMXR/FMRX to the always-available special registers.
 */
static bool vfp_access_check(DisasContext *s)
{
    return full_vfp_access_check(s, false);
}

static bool trans_VSEL(DisasContext *s, arg_VSEL *a)
{
    uint32_t rd, rn, rm;
    bool dp = a->dp;

    if (!dc_isar_feature(aa32_vsel, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (dp && !dc_isar_feature(aa32_fp_d32, s) &&
        ((a->vm | a->vn | a->vd) & 0x10)) {
        return false;
    }
    rd = a->vd;
    rn = a->vn;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    if (dp) {
        TCGv_i64 frn, frm, dest;
        TCGv_i64 tmp, zero, zf, nf, vf;

        zero = tcg_const_i64(0);

        frn = tcg_temp_new_i64();
        frm = tcg_temp_new_i64();
        dest = tcg_temp_new_i64();

        zf = tcg_temp_new_i64();
        nf = tcg_temp_new_i64();
        vf = tcg_temp_new_i64();

        tcg_gen_extu_i32_i64(zf, cpu_ZF);
        tcg_gen_ext_i32_i64(nf, cpu_NF);
        tcg_gen_ext_i32_i64(vf, cpu_VF);

        tcg_gen_ld_f64(frn, cpu_env, vfp_reg_offset(dp, rn));
        tcg_gen_ld_f64(frm, cpu_env, vfp_reg_offset(dp, rm));
        switch (a->cc) {
        case 0: /* eq: Z */
            tcg_gen_movcond_i64(TCG_COND_EQ, dest, zf, zero,
                                frn, frm);
            break;
        case 1: /* vs: V */
            tcg_gen_movcond_i64(TCG_COND_LT, dest, vf, zero,
                                frn, frm);
            break;
        case 2: /* ge: N == V -> N ^ V == 0 */
            tmp = tcg_temp_new_i64();
            tcg_gen_xor_i64(tmp, vf, nf);
            tcg_gen_movcond_i64(TCG_COND_GE, dest, tmp, zero,
                                frn, frm);
            tcg_temp_free_i64(tmp);
            break;
        case 3: /* gt: !Z && N == V */
            tcg_gen_movcond_i64(TCG_COND_NE, dest, zf, zero,
                                frn, frm);
            tmp = tcg_temp_new_i64();
            tcg_gen_xor_i64(tmp, vf, nf);
            tcg_gen_movcond_i64(TCG_COND_GE, dest, tmp, zero,
                                dest, frm);
            tcg_temp_free_i64(tmp);
            break;
        }
        tcg_gen_st_f64(dest, cpu_env, vfp_reg_offset(dp, rd));
        tcg_temp_free_i64(frn);
        tcg_temp_free_i64(frm);
        tcg_temp_free_i64(dest);

        tcg_temp_free_i64(zf);
        tcg_temp_free_i64(nf);
        tcg_temp_free_i64(vf);

        tcg_temp_free_i64(zero);
    } else {
        TCGv_i32 frn, frm, dest;
        TCGv_i32 tmp, zero;

        zero = tcg_const_i32(0);

        frn = tcg_temp_new_i32();
        frm = tcg_temp_new_i32();
        dest = tcg_temp_new_i32();
        tcg_gen_ld_f32(frn, cpu_env, vfp_reg_offset(dp, rn));
        tcg_gen_ld_f32(frm, cpu_env, vfp_reg_offset(dp, rm));
        switch (a->cc) {
        case 0: /* eq: Z */
            tcg_gen_movcond_i32(TCG_COND_EQ, dest, cpu_ZF, zero,
                                frn, frm);
            break;
        case 1: /* vs: V */
            tcg_gen_movcond_i32(TCG_COND_LT, dest, cpu_VF, zero,
                                frn, frm);
            break;
        case 2: /* ge: N == V -> N ^ V == 0 */
            tmp = tcg_temp_new_i32();
            tcg_gen_xor_i32(tmp, cpu_VF, cpu_NF);
            tcg_gen_movcond_i32(TCG_COND_GE, dest, tmp, zero,
                                frn, frm);
            tcg_temp_free_i32(tmp);
            break;
        case 3: /* gt: !Z && N == V */
            tcg_gen_movcond_i32(TCG_COND_NE, dest, cpu_ZF, zero,
                                frn, frm);
            tmp = tcg_temp_new_i32();
            tcg_gen_xor_i32(tmp, cpu_VF, cpu_NF);
            tcg_gen_movcond_i32(TCG_COND_GE, dest, tmp, zero,
                                dest, frm);
            tcg_temp_free_i32(tmp);
            break;
        }
        tcg_gen_st_f32(dest, cpu_env, vfp_reg_offset(dp, rd));
        tcg_temp_free_i32(frn);
        tcg_temp_free_i32(frm);
        tcg_temp_free_i32(dest);

        tcg_temp_free_i32(zero);
    }

    return true;
}

static bool trans_VMINMAXNM(DisasContext *s, arg_VMINMAXNM *a)
{
    uint32_t rd, rn, rm;
    bool dp = a->dp;
    bool vmin = a->op;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (dp && !dc_isar_feature(aa32_fp_d32, s) &&
        ((a->vm | a->vn | a->vd) & 0x10)) {
        return false;
    }
    rd = a->vd;
    rn = a->vn;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = get_fpstatus_ptr(0);

    if (dp) {
        TCGv_i64 frn, frm, dest;

        frn = tcg_temp_new_i64();
        frm = tcg_temp_new_i64();
        dest = tcg_temp_new_i64();

        tcg_gen_ld_f64(frn, cpu_env, vfp_reg_offset(dp, rn));
        tcg_gen_ld_f64(frm, cpu_env, vfp_reg_offset(dp, rm));
        if (vmin) {
            gen_helper_vfp_minnumd(dest, frn, frm, fpst);
        } else {
            gen_helper_vfp_maxnumd(dest, frn, frm, fpst);
        }
        tcg_gen_st_f64(dest, cpu_env, vfp_reg_offset(dp, rd));
        tcg_temp_free_i64(frn);
        tcg_temp_free_i64(frm);
        tcg_temp_free_i64(dest);
    } else {
        TCGv_i32 frn, frm, dest;

        frn = tcg_temp_new_i32();
        frm = tcg_temp_new_i32();
        dest = tcg_temp_new_i32();

        tcg_gen_ld_f32(frn, cpu_env, vfp_reg_offset(dp, rn));
        tcg_gen_ld_f32(frm, cpu_env, vfp_reg_offset(dp, rm));
        if (vmin) {
            gen_helper_vfp_minnums(dest, frn, frm, fpst);
        } else {
            gen_helper_vfp_maxnums(dest, frn, frm, fpst);
        }
        tcg_gen_st_f32(dest, cpu_env, vfp_reg_offset(dp, rd));
        tcg_temp_free_i32(frn);
        tcg_temp_free_i32(frm);
        tcg_temp_free_i32(dest);
    }

    tcg_temp_free_ptr(fpst);
    return true;
}

/*
 * Table for converting the most common AArch32 encoding of
 * rounding mode to arm_fprounding order (which matches the
 * common AArch64 order); see ARM ARM pseudocode FPDecodeRM().
 */
static const uint8_t fp_decode_rm[] = {
    FPROUNDING_TIEAWAY,
    FPROUNDING_TIEEVEN,
    FPROUNDING_POSINF,
    FPROUNDING_NEGINF,
};

static bool trans_VRINT(DisasContext *s, arg_VRINT *a)
{
    uint32_t rd, rm;
    bool dp = a->dp;
    TCGv_ptr fpst;
    TCGv_i32 tcg_rmode;
    int rounding = fp_decode_rm[a->rm];

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (dp && !dc_isar_feature(aa32_fp_d32, s) &&
        ((a->vm | a->vd) & 0x10)) {
        return false;
    }
    rd = a->vd;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = get_fpstatus_ptr(0);

    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rounding));
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);

    if (dp) {
        TCGv_i64 tcg_op;
        TCGv_i64 tcg_res;
        tcg_op = tcg_temp_new_i64();
        tcg_res = tcg_temp_new_i64();
        tcg_gen_ld_f64(tcg_op, cpu_env, vfp_reg_offset(dp, rm));
        gen_helper_rintd(tcg_res, tcg_op, fpst);
        tcg_gen_st_f64(tcg_res, cpu_env, vfp_reg_offset(dp, rd));
        tcg_temp_free_i64(tcg_op);
        tcg_temp_free_i64(tcg_res);
    } else {
        TCGv_i32 tcg_op;
        TCGv_i32 tcg_res;
        tcg_op = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        tcg_gen_ld_f32(tcg_op, cpu_env, vfp_reg_offset(dp, rm));
        gen_helper_rints(tcg_res, tcg_op, fpst);
        tcg_gen_st_f32(tcg_res, cpu_env, vfp_reg_offset(dp, rd));
        tcg_temp_free_i32(tcg_op);
        tcg_temp_free_i32(tcg_res);
    }

    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    tcg_temp_free_i32(tcg_rmode);

    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT(DisasContext *s, arg_VCVT *a)
{
    uint32_t rd, rm;
    bool dp = a->dp;
    TCGv_ptr fpst;
    TCGv_i32 tcg_rmode, tcg_shift;
    int rounding = fp_decode_rm[a->rm];
    bool is_signed = a->op;

    if (!dc_isar_feature(aa32_vcvt_dr, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (dp && !dc_isar_feature(aa32_fp_d32, s) && (a->vm & 0x10)) {
        return false;
    }
    rd = a->vd;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = get_fpstatus_ptr(0);

    tcg_shift = tcg_const_i32(0);

    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rounding));
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);

    if (dp) {
        TCGv_i64 tcg_double, tcg_res;
        TCGv_i32 tcg_tmp;
        tcg_double = tcg_temp_new_i64();
        tcg_res = tcg_temp_new_i64();
        tcg_tmp = tcg_temp_new_i32();
        tcg_gen_ld_f64(tcg_double, cpu_env, vfp_reg_offset(1, rm));
        if (is_signed) {
            gen_helper_vfp_tosld(tcg_res, tcg_double, tcg_shift, fpst);
        } else {
            gen_helper_vfp_tould(tcg_res, tcg_double, tcg_shift, fpst);
        }
        tcg_gen_extrl_i64_i32(tcg_tmp, tcg_res);
        tcg_gen_st_f32(tcg_tmp, cpu_env, vfp_reg_offset(0, rd));
        tcg_temp_free_i32(tcg_tmp);
        tcg_temp_free_i64(tcg_res);
        tcg_temp_free_i64(tcg_double);
    } else {
        TCGv_i32 tcg_single, tcg_res;
        tcg_single = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        tcg_gen_ld_f32(tcg_single, cpu_env, vfp_reg_offset(0, rm));
        if (is_signed) {
            gen_helper_vfp_tosls(tcg_res, tcg_single, tcg_shift, fpst);
        } else {
            gen_helper_vfp_touls(tcg_res, tcg_single, tcg_shift, fpst);
        }
        tcg_gen_st_f32(tcg_res, cpu_env, vfp_reg_offset(0, rd));
        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_single);
    }

    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    tcg_temp_free_i32(tcg_rmode);

    tcg_temp_free_i32(tcg_shift);

    tcg_temp_free_ptr(fpst);

    return true;
}
