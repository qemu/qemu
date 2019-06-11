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

        neon_load_reg64(frn, rn);
        neon_load_reg64(frm, rm);
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
        neon_store_reg64(dest, rd);
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
        neon_load_reg32(frn, rn);
        neon_load_reg32(frm, rm);
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
        neon_store_reg32(dest, rd);
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

        neon_load_reg64(frn, rn);
        neon_load_reg64(frm, rm);
        if (vmin) {
            gen_helper_vfp_minnumd(dest, frn, frm, fpst);
        } else {
            gen_helper_vfp_maxnumd(dest, frn, frm, fpst);
        }
        neon_store_reg64(dest, rd);
        tcg_temp_free_i64(frn);
        tcg_temp_free_i64(frm);
        tcg_temp_free_i64(dest);
    } else {
        TCGv_i32 frn, frm, dest;

        frn = tcg_temp_new_i32();
        frm = tcg_temp_new_i32();
        dest = tcg_temp_new_i32();

        neon_load_reg32(frn, rn);
        neon_load_reg32(frm, rm);
        if (vmin) {
            gen_helper_vfp_minnums(dest, frn, frm, fpst);
        } else {
            gen_helper_vfp_maxnums(dest, frn, frm, fpst);
        }
        neon_store_reg32(dest, rd);
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
        neon_load_reg64(tcg_op, rm);
        gen_helper_rintd(tcg_res, tcg_op, fpst);
        neon_store_reg64(tcg_res, rd);
        tcg_temp_free_i64(tcg_op);
        tcg_temp_free_i64(tcg_res);
    } else {
        TCGv_i32 tcg_op;
        TCGv_i32 tcg_res;
        tcg_op = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        neon_load_reg32(tcg_op, rm);
        gen_helper_rints(tcg_res, tcg_op, fpst);
        neon_store_reg32(tcg_res, rd);
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
        neon_load_reg64(tcg_double, rm);
        if (is_signed) {
            gen_helper_vfp_tosld(tcg_res, tcg_double, tcg_shift, fpst);
        } else {
            gen_helper_vfp_tould(tcg_res, tcg_double, tcg_shift, fpst);
        }
        tcg_gen_extrl_i64_i32(tcg_tmp, tcg_res);
        neon_store_reg32(tcg_tmp, rd);
        tcg_temp_free_i32(tcg_tmp);
        tcg_temp_free_i64(tcg_res);
        tcg_temp_free_i64(tcg_double);
    } else {
        TCGv_i32 tcg_single, tcg_res;
        tcg_single = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        neon_load_reg32(tcg_single, rm);
        if (is_signed) {
            gen_helper_vfp_tosls(tcg_res, tcg_single, tcg_shift, fpst);
        } else {
            gen_helper_vfp_touls(tcg_res, tcg_single, tcg_shift, fpst);
        }
        neon_store_reg32(tcg_res, rd);
        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_single);
    }

    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    tcg_temp_free_i32(tcg_rmode);

    tcg_temp_free_i32(tcg_shift);

    tcg_temp_free_ptr(fpst);

    return true;
}

static bool trans_VMOV_to_gp(DisasContext *s, arg_VMOV_to_gp *a)
{
    /* VMOV scalar to general purpose register */
    TCGv_i32 tmp;
    int pass;
    uint32_t offset;

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_fp_d32, s) && (a->vn & 0x10)) {
        return false;
    }

    offset = a->index << a->size;
    pass = extract32(offset, 2, 1);
    offset = extract32(offset, 0, 2) * 8;

    if (a->size != 2 && !arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = neon_load_reg(a->vn, pass);
    switch (a->size) {
    case 0:
        if (offset) {
            tcg_gen_shri_i32(tmp, tmp, offset);
        }
        if (a->u) {
            gen_uxtb(tmp);
        } else {
            gen_sxtb(tmp);
        }
        break;
    case 1:
        if (a->u) {
            if (offset) {
                tcg_gen_shri_i32(tmp, tmp, 16);
            } else {
                gen_uxth(tmp);
            }
        } else {
            if (offset) {
                tcg_gen_sari_i32(tmp, tmp, 16);
            } else {
                gen_sxth(tmp);
            }
        }
        break;
    case 2:
        break;
    }
    store_reg(s, a->rt, tmp);

    return true;
}

static bool trans_VMOV_from_gp(DisasContext *s, arg_VMOV_from_gp *a)
{
    /* VMOV general purpose register to scalar */
    TCGv_i32 tmp, tmp2;
    int pass;
    uint32_t offset;

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_fp_d32, s) && (a->vn & 0x10)) {
        return false;
    }

    offset = a->index << a->size;
    pass = extract32(offset, 2, 1);
    offset = extract32(offset, 0, 2) * 8;

    if (a->size != 2 && !arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = load_reg(s, a->rt);
    switch (a->size) {
    case 0:
        tmp2 = neon_load_reg(a->vn, pass);
        tcg_gen_deposit_i32(tmp, tmp2, tmp, offset, 8);
        tcg_temp_free_i32(tmp2);
        break;
    case 1:
        tmp2 = neon_load_reg(a->vn, pass);
        tcg_gen_deposit_i32(tmp, tmp2, tmp, offset, 16);
        tcg_temp_free_i32(tmp2);
        break;
    case 2:
        break;
    }
    neon_store_reg(a->vn, pass, tmp);

    return true;
}

static bool trans_VDUP(DisasContext *s, arg_VDUP *a)
{
    /* VDUP (general purpose register) */
    TCGv_i32 tmp;
    int size, vec_size;

    if (!arm_dc_feature(s, ARM_FEATURE_NEON)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_fp_d32, s) && (a->vn & 0x10)) {
        return false;
    }

    if (a->b && a->e) {
        return false;
    }

    if (a->q && (a->vn & 1)) {
        return false;
    }

    vec_size = a->q ? 16 : 8;
    if (a->b) {
        size = 0;
    } else if (a->e) {
        size = 1;
    } else {
        size = 2;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = load_reg(s, a->rt);
    tcg_gen_gvec_dup_i32(size, neon_reg_offset(a->vn, 0),
                         vec_size, vec_size, tmp);
    tcg_temp_free_i32(tmp);

    return true;
}

static bool trans_VMSR_VMRS(DisasContext *s, arg_VMSR_VMRS *a)
{
    TCGv_i32 tmp;
    bool ignore_vfp_enabled = false;

    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        /*
         * The only M-profile VFP vmrs/vmsr sysreg is FPSCR.
         * Writes to R15 are UNPREDICTABLE; we choose to undef.
         */
        if (a->rt == 15 || a->reg != ARM_VFP_FPSCR) {
            return false;
        }
    }

    switch (a->reg) {
    case ARM_VFP_FPSID:
        /*
         * VFPv2 allows access to FPSID from userspace; VFPv3 restricts
         * all ID registers to privileged access only.
         */
        if (IS_USER(s) && arm_dc_feature(s, ARM_FEATURE_VFP3)) {
            return false;
        }
        ignore_vfp_enabled = true;
        break;
    case ARM_VFP_MVFR0:
    case ARM_VFP_MVFR1:
        if (IS_USER(s) || !arm_dc_feature(s, ARM_FEATURE_MVFR)) {
            return false;
        }
        ignore_vfp_enabled = true;
        break;
    case ARM_VFP_MVFR2:
        if (IS_USER(s) || !arm_dc_feature(s, ARM_FEATURE_V8)) {
            return false;
        }
        ignore_vfp_enabled = true;
        break;
    case ARM_VFP_FPSCR:
        break;
    case ARM_VFP_FPEXC:
        if (IS_USER(s)) {
            return false;
        }
        ignore_vfp_enabled = true;
        break;
    case ARM_VFP_FPINST:
    case ARM_VFP_FPINST2:
        /* Not present in VFPv3 */
        if (IS_USER(s) || arm_dc_feature(s, ARM_FEATURE_VFP3)) {
            return false;
        }
        break;
    default:
        return false;
    }

    if (!full_vfp_access_check(s, ignore_vfp_enabled)) {
        return true;
    }

    if (a->l) {
        /* VMRS, move VFP special register to gp register */
        switch (a->reg) {
        case ARM_VFP_FPSID:
        case ARM_VFP_FPEXC:
        case ARM_VFP_FPINST:
        case ARM_VFP_FPINST2:
        case ARM_VFP_MVFR0:
        case ARM_VFP_MVFR1:
        case ARM_VFP_MVFR2:
            tmp = load_cpu_field(vfp.xregs[a->reg]);
            break;
        case ARM_VFP_FPSCR:
            if (a->rt == 15) {
                tmp = load_cpu_field(vfp.xregs[ARM_VFP_FPSCR]);
                tcg_gen_andi_i32(tmp, tmp, 0xf0000000);
            } else {
                tmp = tcg_temp_new_i32();
                gen_helper_vfp_get_fpscr(tmp, cpu_env);
            }
            break;
        default:
            g_assert_not_reached();
        }

        if (a->rt == 15) {
            /* Set the 4 flag bits in the CPSR.  */
            gen_set_nzcv(tmp);
            tcg_temp_free_i32(tmp);
        } else {
            store_reg(s, a->rt, tmp);
        }
    } else {
        /* VMSR, move gp register to VFP special register */
        switch (a->reg) {
        case ARM_VFP_FPSID:
        case ARM_VFP_MVFR0:
        case ARM_VFP_MVFR1:
        case ARM_VFP_MVFR2:
            /* Writes are ignored.  */
            break;
        case ARM_VFP_FPSCR:
            tmp = load_reg(s, a->rt);
            gen_helper_vfp_set_fpscr(cpu_env, tmp);
            tcg_temp_free_i32(tmp);
            gen_lookup_tb(s);
            break;
        case ARM_VFP_FPEXC:
            /*
             * TODO: VFP subarchitecture support.
             * For now, keep the EN bit only
             */
            tmp = load_reg(s, a->rt);
            tcg_gen_andi_i32(tmp, tmp, 1 << 30);
            store_cpu_field(tmp, vfp.xregs[a->reg]);
            gen_lookup_tb(s);
            break;
        case ARM_VFP_FPINST:
        case ARM_VFP_FPINST2:
            tmp = load_reg(s, a->rt);
            store_cpu_field(tmp, vfp.xregs[a->reg]);
            break;
        default:
            g_assert_not_reached();
        }
    }

    return true;
}

static bool trans_VMOV_single(DisasContext *s, arg_VMOV_single *a)
{
    TCGv_i32 tmp;

    if (!vfp_access_check(s)) {
        return true;
    }

    if (a->l) {
        /* VFP to general purpose register */
        tmp = tcg_temp_new_i32();
        neon_load_reg32(tmp, a->vn);
        if (a->rt == 15) {
            /* Set the 4 flag bits in the CPSR.  */
            gen_set_nzcv(tmp);
            tcg_temp_free_i32(tmp);
        } else {
            store_reg(s, a->rt, tmp);
        }
    } else {
        /* general purpose register to VFP */
        tmp = load_reg(s, a->rt);
        neon_store_reg32(tmp, a->vn);
        tcg_temp_free_i32(tmp);
    }

    return true;
}

static bool trans_VMOV_64_sp(DisasContext *s, arg_VMOV_64_sp *a)
{
    TCGv_i32 tmp;

    /*
     * VMOV between two general-purpose registers and two single precision
     * floating point registers
     */
    if (!vfp_access_check(s)) {
        return true;
    }

    if (a->op) {
        /* fpreg to gpreg */
        tmp = tcg_temp_new_i32();
        neon_load_reg32(tmp, a->vm);
        store_reg(s, a->rt, tmp);
        tmp = tcg_temp_new_i32();
        neon_load_reg32(tmp, a->vm + 1);
        store_reg(s, a->rt2, tmp);
    } else {
        /* gpreg to fpreg */
        tmp = load_reg(s, a->rt);
        neon_store_reg32(tmp, a->vm);
        tmp = load_reg(s, a->rt2);
        neon_store_reg32(tmp, a->vm + 1);
    }

    return true;
}

static bool trans_VMOV_64_dp(DisasContext *s, arg_VMOV_64_sp *a)
{
    TCGv_i32 tmp;

    /*
     * VMOV between two general-purpose registers and one double precision
     * floating point register
     */

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_fp_d32, s) && (a->vm & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (a->op) {
        /* fpreg to gpreg */
        tmp = tcg_temp_new_i32();
        neon_load_reg32(tmp, a->vm * 2);
        store_reg(s, a->rt, tmp);
        tmp = tcg_temp_new_i32();
        neon_load_reg32(tmp, a->vm * 2 + 1);
        store_reg(s, a->rt2, tmp);
    } else {
        /* gpreg to fpreg */
        tmp = load_reg(s, a->rt);
        neon_store_reg32(tmp, a->vm * 2);
        tcg_temp_free_i32(tmp);
        tmp = load_reg(s, a->rt2);
        neon_store_reg32(tmp, a->vm * 2 + 1);
        tcg_temp_free_i32(tmp);
    }

    return true;
}

static bool trans_VLDR_VSTR_sp(DisasContext *s, arg_VLDR_VSTR_sp *a)
{
    uint32_t offset;
    TCGv_i32 addr;

    if (!vfp_access_check(s)) {
        return true;
    }

    offset = a->imm << 2;
    if (!a->u) {
        offset = -offset;
    }

    if (s->thumb && a->rn == 15) {
        /* This is actually UNPREDICTABLE */
        addr = tcg_temp_new_i32();
        tcg_gen_movi_i32(addr, s->pc & ~2);
    } else {
        addr = load_reg(s, a->rn);
    }
    tcg_gen_addi_i32(addr, addr, offset);
    if (a->l) {
        gen_vfp_ld(s, false, addr);
        gen_mov_vreg_F0(false, a->vd);
    } else {
        gen_mov_F0_vreg(false, a->vd);
        gen_vfp_st(s, false, addr);
    }
    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_VLDR_VSTR_dp(DisasContext *s, arg_VLDR_VSTR_sp *a)
{
    uint32_t offset;
    TCGv_i32 addr;

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_fp_d32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    offset = a->imm << 2;
    if (!a->u) {
        offset = -offset;
    }

    if (s->thumb && a->rn == 15) {
        /* This is actually UNPREDICTABLE */
        addr = tcg_temp_new_i32();
        tcg_gen_movi_i32(addr, s->pc & ~2);
    } else {
        addr = load_reg(s, a->rn);
    }
    tcg_gen_addi_i32(addr, addr, offset);
    if (a->l) {
        gen_vfp_ld(s, true, addr);
        gen_mov_vreg_F0(true, a->vd);
    } else {
        gen_mov_F0_vreg(true, a->vd);
        gen_vfp_st(s, true, addr);
    }
    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_VLDM_VSTM_sp(DisasContext *s, arg_VLDM_VSTM_sp *a)
{
    uint32_t offset;
    TCGv_i32 addr;
    int i, n;

    n = a->imm;

    if (n == 0 || (a->vd + n) > 32) {
        /*
         * UNPREDICTABLE cases for bad immediates: we choose to
         * UNDEF to avoid generating huge numbers of TCG ops
         */
        return false;
    }
    if (a->rn == 15 && a->w) {
        /* writeback to PC is UNPREDICTABLE, we choose to UNDEF */
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (s->thumb && a->rn == 15) {
        /* This is actually UNPREDICTABLE */
        addr = tcg_temp_new_i32();
        tcg_gen_movi_i32(addr, s->pc & ~2);
    } else {
        addr = load_reg(s, a->rn);
    }
    if (a->p) {
        /* pre-decrement */
        tcg_gen_addi_i32(addr, addr, -(a->imm << 2));
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        /*
         * Here 'addr' is the lowest address we will store to,
         * and is either the old SP (if post-increment) or
         * the new SP (if pre-decrement). For post-increment
         * where the old value is below the limit and the new
         * value is above, it is UNKNOWN whether the limit check
         * triggers; we choose to trigger.
         */
        gen_helper_v8m_stackcheck(cpu_env, addr);
    }

    offset = 4;
    for (i = 0; i < n; i++) {
        if (a->l) {
            /* load */
            gen_vfp_ld(s, false, addr);
            gen_mov_vreg_F0(false, a->vd + i);
        } else {
            /* store */
            gen_mov_F0_vreg(false, a->vd + i);
            gen_vfp_st(s, false, addr);
        }
        tcg_gen_addi_i32(addr, addr, offset);
    }
    if (a->w) {
        /* writeback */
        if (a->p) {
            offset = -offset * n;
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    } else {
        tcg_temp_free_i32(addr);
    }

    return true;
}

static bool trans_VLDM_VSTM_dp(DisasContext *s, arg_VLDM_VSTM_dp *a)
{
    uint32_t offset;
    TCGv_i32 addr;
    int i, n;

    n = a->imm >> 1;

    if (n == 0 || (a->vd + n) > 32 || n > 16) {
        /*
         * UNPREDICTABLE cases for bad immediates: we choose to
         * UNDEF to avoid generating huge numbers of TCG ops
         */
        return false;
    }
    if (a->rn == 15 && a->w) {
        /* writeback to PC is UNPREDICTABLE, we choose to UNDEF */
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_fp_d32, s) && (a->vd + n) > 16) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (s->thumb && a->rn == 15) {
        /* This is actually UNPREDICTABLE */
        addr = tcg_temp_new_i32();
        tcg_gen_movi_i32(addr, s->pc & ~2);
    } else {
        addr = load_reg(s, a->rn);
    }
    if (a->p) {
        /* pre-decrement */
        tcg_gen_addi_i32(addr, addr, -(a->imm << 2));
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        /*
         * Here 'addr' is the lowest address we will store to,
         * and is either the old SP (if post-increment) or
         * the new SP (if pre-decrement). For post-increment
         * where the old value is below the limit and the new
         * value is above, it is UNKNOWN whether the limit check
         * triggers; we choose to trigger.
         */
        gen_helper_v8m_stackcheck(cpu_env, addr);
    }

    offset = 8;
    for (i = 0; i < n; i++) {
        if (a->l) {
            /* load */
            gen_vfp_ld(s, true, addr);
            gen_mov_vreg_F0(true, a->vd + i);
        } else {
            /* store */
            gen_mov_F0_vreg(true, a->vd + i);
            gen_vfp_st(s, true, addr);
        }
        tcg_gen_addi_i32(addr, addr, offset);
    }
    if (a->w) {
        /* writeback */
        if (a->p) {
            offset = -offset * n;
        } else if (a->imm & 1) {
            offset = 4;
        } else {
            offset = 0;
        }

        if (offset != 0) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    } else {
        tcg_temp_free_i32(addr);
    }

    return true;
}
