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
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "translate.h"
#include "translate-a32.h"

/* Include the generated VFP decoder */
#include "decode-vfp.c.inc"
#include "decode-vfp-uncond.c.inc"

static inline void vfp_load_reg64(TCGv_i64 var, int reg)
{
    tcg_gen_ld_i64(var, cpu_env, vfp_reg_offset(true, reg));
}

static inline void vfp_store_reg64(TCGv_i64 var, int reg)
{
    tcg_gen_st_i64(var, cpu_env, vfp_reg_offset(true, reg));
}

static inline void vfp_load_reg32(TCGv_i32 var, int reg)
{
    tcg_gen_ld_i32(var, cpu_env, vfp_reg_offset(false, reg));
}

static inline void vfp_store_reg32(TCGv_i32 var, int reg)
{
    tcg_gen_st_i32(var, cpu_env, vfp_reg_offset(false, reg));
}

/*
 * The imm8 encodes the sign bit, enough bits to represent an exponent in
 * the range 01....1xx to 10....0xx, and the most significant 4 bits of
 * the mantissa; see VFPExpandImm() in the v8 ARM ARM.
 */
uint64_t vfp_expand_imm(int size, uint8_t imm8)
{
    uint64_t imm;

    switch (size) {
    case MO_64:
        imm = (extract32(imm8, 7, 1) ? 0x8000 : 0) |
            (extract32(imm8, 6, 1) ? 0x3fc0 : 0x4000) |
            extract32(imm8, 0, 6);
        imm <<= 48;
        break;
    case MO_32:
        imm = (extract32(imm8, 7, 1) ? 0x8000 : 0) |
            (extract32(imm8, 6, 1) ? 0x3e00 : 0x4000) |
            (extract32(imm8, 0, 6) << 3);
        imm <<= 16;
        break;
    case MO_16:
        imm = (extract32(imm8, 7, 1) ? 0x8000 : 0) |
            (extract32(imm8, 6, 1) ? 0x3000 : 0x4000) |
            (extract32(imm8, 0, 6) << 6);
        break;
    default:
        g_assert_not_reached();
    }
    return imm;
}

/*
 * Return the offset of a 16-bit half of the specified VFP single-precision
 * register. If top is true, returns the top 16 bits; otherwise the bottom
 * 16 bits.
 */
static inline long vfp_f16_offset(unsigned reg, bool top)
{
    long offs = vfp_reg_offset(false, reg);
#ifdef HOST_WORDS_BIGENDIAN
    if (!top) {
        offs += 2;
    }
#else
    if (top) {
        offs += 2;
    }
#endif
    return offs;
}

/*
 * Generate code for M-profile lazy FP state preservation if needed;
 * this corresponds to the pseudocode PreserveFPState() function.
 */
static void gen_preserve_fp_state(DisasContext *s, bool skip_context_update)
{
    if (s->v7m_lspact) {
        /*
         * Lazy state saving affects external memory and also the NVIC,
         * so we must mark it as an IO operation for icount (and cause
         * this to be the last insn in the TB).
         */
        if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
            s->base.is_jmp = DISAS_UPDATE_EXIT;
            gen_io_start();
        }
        gen_helper_v7m_preserve_fp_state(cpu_env);
        /*
         * If the preserve_fp_state helper doesn't throw an exception
         * then it will clear LSPACT; we don't need to repeat this for
         * any further FP insns in this TB.
         */
        s->v7m_lspact = false;
        /*
         * The helper might have zeroed VPR, so we do not know the
         * correct value for the MVE_NO_PRED TB flag any more.
         * If we're about to create a new fp context then that
         * will precisely determine the MVE_NO_PRED value (see
         * gen_update_fp_context()). Otherwise, we must:
         *  - set s->mve_no_pred to false, so this instruction
         *    is generated to use helper functions
         *  - end the TB now, without chaining to the next TB
         */
        if (skip_context_update || !s->v7m_new_fp_ctxt_needed) {
            s->mve_no_pred = false;
            s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        }
    }
}

/*
 * Generate code for M-profile FP context handling: update the
 * ownership of the FP context, and create a new context if
 * necessary. This corresponds to the parts of the pseudocode
 * ExecuteFPCheck() after the inital PreserveFPState() call.
 */
static void gen_update_fp_context(DisasContext *s)
{
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
         * Create new FP context by updating CONTROL.FPCA, CONTROL.SFPA,
         * the FPSCR, and VPR.
         */
        TCGv_i32 control, fpscr;
        uint32_t bits = R_V7M_CONTROL_FPCA_MASK;

        fpscr = load_cpu_field(v7m.fpdscr[s->v8m_secure]);
        gen_helper_vfp_set_fpscr(cpu_env, fpscr);
        tcg_temp_free_i32(fpscr);
        if (dc_isar_feature(aa32_mve, s)) {
            TCGv_i32 z32 = tcg_const_i32(0);
            store_cpu_field(z32, v7m.vpr);
        }
        /*
         * We just updated the FPSCR and VPR. Some of this state is cached
         * in the MVE_NO_PRED TB flag. We want to avoid having to end the
         * TB here, which means we need the new value of the MVE_NO_PRED
         * flag to be exactly known here and the same for all executions.
         * Luckily FPDSCR.LTPSIZE is always constant 4 and the VPR is
         * always set to 0, so the new MVE_NO_PRED flag is always 1
         * if and only if we have MVE.
         *
         * (The other FPSCR state cached in TB flags is VECLEN and VECSTRIDE,
         * but those do not exist for M-profile, so are not relevant here.)
         */
        s->mve_no_pred = dc_isar_feature(aa32_mve, s);

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

/*
 * Check that VFP access is enabled, A-profile specific version.
 *
 * If VFP is enabled, return true. If not, emit code to generate an
 * appropriate exception and return false.
 * The ignore_vfp_enabled argument specifies that we should ignore
 * whether VFP is enabled via FPEXC.EN: this should be true for FMXR/FMRX
 * accesses to FPSID, FPEXC, MVFR0, MVFR1, MVFR2, and false for all other insns.
 */
static bool vfp_access_check_a(DisasContext *s, bool ignore_vfp_enabled)
{
    if (s->fp_excp_el) {
        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_fp_access_trap(1, 0xe, false), s->fp_excp_el);
        return false;
    }

    if (!s->vfp_enabled && !ignore_vfp_enabled) {
        assert(!arm_dc_feature(s, ARM_FEATURE_M));
        unallocated_encoding(s);
        return false;
    }
    return true;
}

/*
 * Check that VFP access is enabled, M-profile specific version.
 *
 * If VFP is enabled, do the necessary M-profile lazy-FP handling and then
 * return true. If not, emit code to generate an appropriate exception and
 * return false.
 * skip_context_update is true to skip the "update FP context" part of this.
 */
bool vfp_access_check_m(DisasContext *s, bool skip_context_update)
{
    if (s->fp_excp_el) {
        /*
         * M-profile mostly catches the "FPU disabled" case early, in
         * disas_m_nocp(), but a few insns (eg LCTP, WLSTP, DLSTP)
         * which do coprocessor-checks are outside the large ranges of
         * the encoding space handled by the patterns in m-nocp.decode,
         * and for them we may need to raise NOCP here.
         */
        gen_exception_insn(s, s->pc_curr, EXCP_NOCP,
                           syn_uncategorized(), s->fp_excp_el);
        return false;
    }

    /* Handle M-profile lazy FP state mechanics */

    /* Trigger lazy-state preservation if necessary */
    gen_preserve_fp_state(s, skip_context_update);

    if (!skip_context_update) {
        /* Update ownership of FP context and create new FP context if needed */
        gen_update_fp_context(s);
    }

    return true;
}

/*
 * The most usual kind of VFP access check, for everything except
 * FMXR/FMRX to the always-available special registers.
 */
bool vfp_access_check(DisasContext *s)
{
    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        return vfp_access_check_m(s, false);
    } else {
        return vfp_access_check_a(s, false);
    }
}

static bool trans_VSEL(DisasContext *s, arg_VSEL *a)
{
    uint32_t rd, rn, rm;
    int sz = a->sz;

    if (!dc_isar_feature(aa32_vsel, s)) {
        return false;
    }

    if (sz == 3 && !dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (sz == 1 && !dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (sz == 3 && !dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vm | a->vn | a->vd) & 0x10)) {
        return false;
    }

    rd = a->vd;
    rn = a->vn;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    if (sz == 3) {
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

        vfp_load_reg64(frn, rn);
        vfp_load_reg64(frm, rm);
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
        vfp_store_reg64(dest, rd);
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
        vfp_load_reg32(frn, rn);
        vfp_load_reg32(frm, rm);
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
        /* For fp16 the top half is always zeroes */
        if (sz == 1) {
            tcg_gen_andi_i32(dest, dest, 0xffff);
        }
        vfp_store_reg32(dest, rd);
        tcg_temp_free_i32(frn);
        tcg_temp_free_i32(frm);
        tcg_temp_free_i32(dest);

        tcg_temp_free_i32(zero);
    }

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
    int sz = a->sz;
    TCGv_ptr fpst;
    TCGv_i32 tcg_rmode;
    int rounding = fp_decode_rm[a->rm];

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    if (sz == 3 && !dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (sz == 1 && !dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (sz == 3 && !dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vm | a->vd) & 0x10)) {
        return false;
    }

    rd = a->vd;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    if (sz == 1) {
        fpst = fpstatus_ptr(FPST_FPCR_F16);
    } else {
        fpst = fpstatus_ptr(FPST_FPCR);
    }

    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rounding));
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);

    if (sz == 3) {
        TCGv_i64 tcg_op;
        TCGv_i64 tcg_res;
        tcg_op = tcg_temp_new_i64();
        tcg_res = tcg_temp_new_i64();
        vfp_load_reg64(tcg_op, rm);
        gen_helper_rintd(tcg_res, tcg_op, fpst);
        vfp_store_reg64(tcg_res, rd);
        tcg_temp_free_i64(tcg_op);
        tcg_temp_free_i64(tcg_res);
    } else {
        TCGv_i32 tcg_op;
        TCGv_i32 tcg_res;
        tcg_op = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        vfp_load_reg32(tcg_op, rm);
        if (sz == 1) {
            gen_helper_rinth(tcg_res, tcg_op, fpst);
        } else {
            gen_helper_rints(tcg_res, tcg_op, fpst);
        }
        vfp_store_reg32(tcg_res, rd);
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
    int sz = a->sz;
    TCGv_ptr fpst;
    TCGv_i32 tcg_rmode, tcg_shift;
    int rounding = fp_decode_rm[a->rm];
    bool is_signed = a->op;

    if (!dc_isar_feature(aa32_vcvt_dr, s)) {
        return false;
    }

    if (sz == 3 && !dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (sz == 1 && !dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (sz == 3 && !dc_isar_feature(aa32_simd_r32, s) && (a->vm & 0x10)) {
        return false;
    }

    rd = a->vd;
    rm = a->vm;

    if (!vfp_access_check(s)) {
        return true;
    }

    if (sz == 1) {
        fpst = fpstatus_ptr(FPST_FPCR_F16);
    } else {
        fpst = fpstatus_ptr(FPST_FPCR);
    }

    tcg_shift = tcg_const_i32(0);

    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rounding));
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);

    if (sz == 3) {
        TCGv_i64 tcg_double, tcg_res;
        TCGv_i32 tcg_tmp;
        tcg_double = tcg_temp_new_i64();
        tcg_res = tcg_temp_new_i64();
        tcg_tmp = tcg_temp_new_i32();
        vfp_load_reg64(tcg_double, rm);
        if (is_signed) {
            gen_helper_vfp_tosld(tcg_res, tcg_double, tcg_shift, fpst);
        } else {
            gen_helper_vfp_tould(tcg_res, tcg_double, tcg_shift, fpst);
        }
        tcg_gen_extrl_i64_i32(tcg_tmp, tcg_res);
        vfp_store_reg32(tcg_tmp, rd);
        tcg_temp_free_i32(tcg_tmp);
        tcg_temp_free_i64(tcg_res);
        tcg_temp_free_i64(tcg_double);
    } else {
        TCGv_i32 tcg_single, tcg_res;
        tcg_single = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        vfp_load_reg32(tcg_single, rm);
        if (sz == 1) {
            if (is_signed) {
                gen_helper_vfp_toslh(tcg_res, tcg_single, tcg_shift, fpst);
            } else {
                gen_helper_vfp_toulh(tcg_res, tcg_single, tcg_shift, fpst);
            }
        } else {
            if (is_signed) {
                gen_helper_vfp_tosls(tcg_res, tcg_single, tcg_shift, fpst);
            } else {
                gen_helper_vfp_touls(tcg_res, tcg_single, tcg_shift, fpst);
            }
        }
        vfp_store_reg32(tcg_res, rd);
        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_single);
    }

    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    tcg_temp_free_i32(tcg_rmode);

    tcg_temp_free_i32(tcg_shift);

    tcg_temp_free_ptr(fpst);

    return true;
}

bool mve_skip_vmov(DisasContext *s, int vn, int index, int size)
{
    /*
     * In a CPU with MVE, the VMOV (vector lane to general-purpose register)
     * and VMOV (general-purpose register to vector lane) insns are not
     * predicated, but they are subject to beatwise execution if they are
     * not in an IT block.
     *
     * Since our implementation always executes all 4 beats in one tick,
     * this means only that if PSR.ECI says we should not be executing
     * the beat corresponding to the lane of the vector register being
     * accessed then we should skip performing the move, and that we need
     * to do the usual check for bad ECI state and advance of ECI state.
     *
     * Note that if PSR.ECI is non-zero then we cannot be in an IT block.
     *
     * Return true if this VMOV scalar <-> gpreg should be skipped because
     * the MVE PSR.ECI state says we skip the beat where the store happens.
     */

    /* Calculate the byte offset into Qn which we're going to access */
    int ofs = (index << size) + ((vn & 1) * 8);

    if (!dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    switch (s->eci) {
    case ECI_NONE:
        return false;
    case ECI_A0:
        return ofs < 4;
    case ECI_A0A1:
        return ofs < 8;
    case ECI_A0A1A2:
    case ECI_A0A1A2B0:
        return ofs < 12;
    default:
        g_assert_not_reached();
    }
}

static bool trans_VMOV_to_gp(DisasContext *s, arg_VMOV_to_gp *a)
{
    /* VMOV scalar to general purpose register */
    TCGv_i32 tmp;

    /*
     * SIZE == MO_32 is a VFP instruction; otherwise NEON. MVE has
     * all sizes, whether the CPU has fp or not.
     */
    if (!dc_isar_feature(aa32_mve, s)) {
        if (a->size == MO_32
            ? !dc_isar_feature(aa32_fpsp_v2, s)
            : !arm_dc_feature(s, ARM_FEATURE_NEON)) {
            return false;
        }
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vn & 0x10)) {
        return false;
    }

    if (dc_isar_feature(aa32_mve, s)) {
        if (!mve_eci_check(s)) {
            return true;
        }
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (!mve_skip_vmov(s, a->vn, a->index, a->size)) {
        tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, a->vn, a->index,
                            a->size | (a->u ? 0 : MO_SIGN));
        store_reg(s, a->rt, tmp);
    }

    if (dc_isar_feature(aa32_mve, s)) {
        mve_update_and_store_eci(s);
    }
    return true;
}

static bool trans_VMOV_from_gp(DisasContext *s, arg_VMOV_from_gp *a)
{
    /* VMOV general purpose register to scalar */
    TCGv_i32 tmp;

    /*
     * SIZE == MO_32 is a VFP instruction; otherwise NEON. MVE has
     * all sizes, whether the CPU has fp or not.
     */
    if (!dc_isar_feature(aa32_mve, s)) {
        if (a->size == MO_32
            ? !dc_isar_feature(aa32_fpsp_v2, s)
            : !arm_dc_feature(s, ARM_FEATURE_NEON)) {
            return false;
        }
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vn & 0x10)) {
        return false;
    }

    if (dc_isar_feature(aa32_mve, s)) {
        if (!mve_eci_check(s)) {
            return true;
        }
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (!mve_skip_vmov(s, a->vn, a->index, a->size)) {
        tmp = load_reg(s, a->rt);
        write_neon_element32(tmp, a->vn, a->index, a->size);
        tcg_temp_free_i32(tmp);
    }

    if (dc_isar_feature(aa32_mve, s)) {
        mve_update_and_store_eci(s);
    }
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
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vn & 0x10)) {
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
    tcg_gen_gvec_dup_i32(size, neon_full_reg_offset(a->vn),
                         vec_size, vec_size, tmp);
    tcg_temp_free_i32(tmp);

    return true;
}

static bool trans_VMSR_VMRS(DisasContext *s, arg_VMSR_VMRS *a)
{
    TCGv_i32 tmp;
    bool ignore_vfp_enabled = false;

    if (arm_dc_feature(s, ARM_FEATURE_M)) {
        /* M profile version was already handled in m-nocp.decode */
        return false;
    }

    if (!dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }

    switch (a->reg) {
    case ARM_VFP_FPSID:
        /*
         * VFPv2 allows access to FPSID from userspace; VFPv3 restricts
         * all ID registers to privileged access only.
         */
        if (IS_USER(s) && dc_isar_feature(aa32_fpsp_v3, s)) {
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
        if (IS_USER(s) || dc_isar_feature(aa32_fpsp_v3, s)) {
            return false;
        }
        break;
    default:
        return false;
    }

    /*
     * Call vfp_access_check_a() directly, because we need to tell
     * it to ignore FPEXC.EN for some register accesses.
     */
    if (!vfp_access_check_a(s, ignore_vfp_enabled)) {
        return true;
    }

    if (a->l) {
        /* VMRS, move VFP special register to gp register */
        switch (a->reg) {
        case ARM_VFP_MVFR0:
        case ARM_VFP_MVFR1:
        case ARM_VFP_MVFR2:
        case ARM_VFP_FPSID:
            if (s->current_el == 1) {
                TCGv_i32 tcg_reg, tcg_rt;

                gen_set_condexec(s);
                gen_set_pc_im(s, s->pc_curr);
                tcg_reg = tcg_const_i32(a->reg);
                tcg_rt = tcg_const_i32(a->rt);
                gen_helper_check_hcr_el2_trap(cpu_env, tcg_rt, tcg_reg);
                tcg_temp_free_i32(tcg_reg);
                tcg_temp_free_i32(tcg_rt);
            }
            /* fall through */
        case ARM_VFP_FPEXC:
        case ARM_VFP_FPINST:
        case ARM_VFP_FPINST2:
            tmp = load_cpu_field(vfp.xregs[a->reg]);
            break;
        case ARM_VFP_FPSCR:
            if (a->rt == 15) {
                tmp = load_cpu_field(vfp.xregs[ARM_VFP_FPSCR]);
                tcg_gen_andi_i32(tmp, tmp, FPCR_NZCV_MASK);
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


static bool trans_VMOV_half(DisasContext *s, arg_VMOV_single *a)
{
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (a->rt == 15) {
        /* UNPREDICTABLE; we choose to UNDEF */
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (a->l) {
        /* VFP to general purpose register */
        tmp = tcg_temp_new_i32();
        vfp_load_reg32(tmp, a->vn);
        tcg_gen_andi_i32(tmp, tmp, 0xffff);
        store_reg(s, a->rt, tmp);
    } else {
        /* general purpose register to VFP */
        tmp = load_reg(s, a->rt);
        tcg_gen_andi_i32(tmp, tmp, 0xffff);
        vfp_store_reg32(tmp, a->vn);
        tcg_temp_free_i32(tmp);
    }

    return true;
}

static bool trans_VMOV_single(DisasContext *s, arg_VMOV_single *a)
{
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (a->l) {
        /* VFP to general purpose register */
        tmp = tcg_temp_new_i32();
        vfp_load_reg32(tmp, a->vn);
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
        vfp_store_reg32(tmp, a->vn);
        tcg_temp_free_i32(tmp);
    }

    return true;
}

static bool trans_VMOV_64_sp(DisasContext *s, arg_VMOV_64_sp *a)
{
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

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
        vfp_load_reg32(tmp, a->vm);
        store_reg(s, a->rt, tmp);
        tmp = tcg_temp_new_i32();
        vfp_load_reg32(tmp, a->vm + 1);
        store_reg(s, a->rt2, tmp);
    } else {
        /* gpreg to fpreg */
        tmp = load_reg(s, a->rt);
        vfp_store_reg32(tmp, a->vm);
        tcg_temp_free_i32(tmp);
        tmp = load_reg(s, a->rt2);
        vfp_store_reg32(tmp, a->vm + 1);
        tcg_temp_free_i32(tmp);
    }

    return true;
}

static bool trans_VMOV_64_dp(DisasContext *s, arg_VMOV_64_dp *a)
{
    TCGv_i32 tmp;

    /*
     * VMOV between two general-purpose registers and one double precision
     * floating point register.  Note that this does not require support
     * for double precision arithmetic.
     */
    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vm & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (a->op) {
        /* fpreg to gpreg */
        tmp = tcg_temp_new_i32();
        vfp_load_reg32(tmp, a->vm * 2);
        store_reg(s, a->rt, tmp);
        tmp = tcg_temp_new_i32();
        vfp_load_reg32(tmp, a->vm * 2 + 1);
        store_reg(s, a->rt2, tmp);
    } else {
        /* gpreg to fpreg */
        tmp = load_reg(s, a->rt);
        vfp_store_reg32(tmp, a->vm * 2);
        tcg_temp_free_i32(tmp);
        tmp = load_reg(s, a->rt2);
        vfp_store_reg32(tmp, a->vm * 2 + 1);
        tcg_temp_free_i32(tmp);
    }

    return true;
}

static bool trans_VLDR_VSTR_hp(DisasContext *s, arg_VLDR_VSTR_sp *a)
{
    uint32_t offset;
    TCGv_i32 addr, tmp;

    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* imm8 field is offset/2 for fp16, unlike fp32 and fp64 */
    offset = a->imm << 1;
    if (!a->u) {
        offset = -offset;
    }

    /* For thumb, use of PC is UNPREDICTABLE.  */
    addr = add_reg_for_lit(s, a->rn, offset);
    tmp = tcg_temp_new_i32();
    if (a->l) {
        gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), MO_UW | MO_ALIGN);
        vfp_store_reg32(tmp, a->vd);
    } else {
        vfp_load_reg32(tmp, a->vd);
        gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), MO_UW | MO_ALIGN);
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_VLDR_VSTR_sp(DisasContext *s, arg_VLDR_VSTR_sp *a)
{
    uint32_t offset;
    TCGv_i32 addr, tmp;

    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    offset = a->imm << 2;
    if (!a->u) {
        offset = -offset;
    }

    /* For thumb, use of PC is UNPREDICTABLE.  */
    addr = add_reg_for_lit(s, a->rn, offset);
    tmp = tcg_temp_new_i32();
    if (a->l) {
        gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), MO_UL | MO_ALIGN);
        vfp_store_reg32(tmp, a->vd);
    } else {
        vfp_load_reg32(tmp, a->vd);
        gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), MO_UL | MO_ALIGN);
    }
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_VLDR_VSTR_dp(DisasContext *s, arg_VLDR_VSTR_dp *a)
{
    uint32_t offset;
    TCGv_i32 addr;
    TCGv_i64 tmp;

    /* Note that this does not require support for double arithmetic.  */
    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    offset = a->imm << 2;
    if (!a->u) {
        offset = -offset;
    }

    /* For thumb, use of PC is UNPREDICTABLE.  */
    addr = add_reg_for_lit(s, a->rn, offset);
    tmp = tcg_temp_new_i64();
    if (a->l) {
        gen_aa32_ld_i64(s, tmp, addr, get_mem_index(s), MO_Q | MO_ALIGN_4);
        vfp_store_reg64(tmp, a->vd);
    } else {
        vfp_load_reg64(tmp, a->vd);
        gen_aa32_st_i64(s, tmp, addr, get_mem_index(s), MO_Q | MO_ALIGN_4);
    }
    tcg_temp_free_i64(tmp);
    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_VLDM_VSTM_sp(DisasContext *s, arg_VLDM_VSTM_sp *a)
{
    uint32_t offset;
    TCGv_i32 addr, tmp;
    int i, n;

    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

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

    s->eci_handled = true;

    if (!vfp_access_check(s)) {
        return true;
    }

    /* For thumb, use of PC is UNPREDICTABLE.  */
    addr = add_reg_for_lit(s, a->rn, 0);
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
    tmp = tcg_temp_new_i32();
    for (i = 0; i < n; i++) {
        if (a->l) {
            /* load */
            gen_aa32_ld_i32(s, tmp, addr, get_mem_index(s), MO_UL | MO_ALIGN);
            vfp_store_reg32(tmp, a->vd + i);
        } else {
            /* store */
            vfp_load_reg32(tmp, a->vd + i);
            gen_aa32_st_i32(s, tmp, addr, get_mem_index(s), MO_UL | MO_ALIGN);
        }
        tcg_gen_addi_i32(addr, addr, offset);
    }
    tcg_temp_free_i32(tmp);
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

    clear_eci_state(s);
    return true;
}

static bool trans_VLDM_VSTM_dp(DisasContext *s, arg_VLDM_VSTM_dp *a)
{
    uint32_t offset;
    TCGv_i32 addr;
    TCGv_i64 tmp;
    int i, n;

    /* Note that this does not require support for double arithmetic.  */
    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return false;
    }

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
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd + n) > 16) {
        return false;
    }

    s->eci_handled = true;

    if (!vfp_access_check(s)) {
        return true;
    }

    /* For thumb, use of PC is UNPREDICTABLE.  */
    addr = add_reg_for_lit(s, a->rn, 0);
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
    tmp = tcg_temp_new_i64();
    for (i = 0; i < n; i++) {
        if (a->l) {
            /* load */
            gen_aa32_ld_i64(s, tmp, addr, get_mem_index(s), MO_Q | MO_ALIGN_4);
            vfp_store_reg64(tmp, a->vd + i);
        } else {
            /* store */
            vfp_load_reg64(tmp, a->vd + i);
            gen_aa32_st_i64(s, tmp, addr, get_mem_index(s), MO_Q | MO_ALIGN_4);
        }
        tcg_gen_addi_i32(addr, addr, offset);
    }
    tcg_temp_free_i64(tmp);
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

    clear_eci_state(s);
    return true;
}

/*
 * Types for callbacks for do_vfp_3op_sp() and do_vfp_3op_dp().
 * The callback should emit code to write a value to vd. If
 * do_vfp_3op_{sp,dp}() was passed reads_vd then the TCGv vd
 * will contain the old value of the relevant VFP register;
 * otherwise it must be written to only.
 */
typedef void VFPGen3OpSPFn(TCGv_i32 vd,
                           TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst);
typedef void VFPGen3OpDPFn(TCGv_i64 vd,
                           TCGv_i64 vn, TCGv_i64 vm, TCGv_ptr fpst);

/*
 * Types for callbacks for do_vfp_2op_sp() and do_vfp_2op_dp().
 * The callback should emit code to write a value to vd (which
 * should be written to only).
 */
typedef void VFPGen2OpSPFn(TCGv_i32 vd, TCGv_i32 vm);
typedef void VFPGen2OpDPFn(TCGv_i64 vd, TCGv_i64 vm);

/*
 * Return true if the specified S reg is in a scalar bank
 * (ie if it is s0..s7)
 */
static inline bool vfp_sreg_is_scalar(int reg)
{
    return (reg & 0x18) == 0;
}

/*
 * Return true if the specified D reg is in a scalar bank
 * (ie if it is d0..d3 or d16..d19)
 */
static inline bool vfp_dreg_is_scalar(int reg)
{
    return (reg & 0xc) == 0;
}

/*
 * Advance the S reg number forwards by delta within its bank
 * (ie increment the low 3 bits but leave the rest the same)
 */
static inline int vfp_advance_sreg(int reg, int delta)
{
    return ((reg + delta) & 0x7) | (reg & ~0x7);
}

/*
 * Advance the D reg number forwards by delta within its bank
 * (ie increment the low 2 bits but leave the rest the same)
 */
static inline int vfp_advance_dreg(int reg, int delta)
{
    return ((reg + delta) & 0x3) | (reg & ~0x3);
}

/*
 * Perform a 3-operand VFP data processing instruction. fn is the
 * callback to do the actual operation; this function deals with the
 * code to handle looping around for VFP vector processing.
 */
static bool do_vfp_3op_sp(DisasContext *s, VFPGen3OpSPFn *fn,
                          int vd, int vn, int vm, bool reads_vd)
{
    uint32_t delta_m = 0;
    uint32_t delta_d = 0;
    int veclen = s->vec_len;
    TCGv_i32 f0, f1, fd;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fpshvec, s) &&
        (veclen != 0 || s->vec_stride != 0)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (veclen > 0) {
        /* Figure out what type of vector operation this is.  */
        if (vfp_sreg_is_scalar(vd)) {
            /* scalar */
            veclen = 0;
        } else {
            delta_d = s->vec_stride + 1;

            if (vfp_sreg_is_scalar(vm)) {
                /* mixed scalar/vector */
                delta_m = 0;
            } else {
                /* vector */
                delta_m = delta_d;
            }
        }
    }

    f0 = tcg_temp_new_i32();
    f1 = tcg_temp_new_i32();
    fd = tcg_temp_new_i32();
    fpst = fpstatus_ptr(FPST_FPCR);

    vfp_load_reg32(f0, vn);
    vfp_load_reg32(f1, vm);

    for (;;) {
        if (reads_vd) {
            vfp_load_reg32(fd, vd);
        }
        fn(fd, f0, f1, fpst);
        vfp_store_reg32(fd, vd);

        if (veclen == 0) {
            break;
        }

        /* Set up the operands for the next iteration */
        veclen--;
        vd = vfp_advance_sreg(vd, delta_d);
        vn = vfp_advance_sreg(vn, delta_d);
        vfp_load_reg32(f0, vn);
        if (delta_m) {
            vm = vfp_advance_sreg(vm, delta_m);
            vfp_load_reg32(f1, vm);
        }
    }

    tcg_temp_free_i32(f0);
    tcg_temp_free_i32(f1);
    tcg_temp_free_i32(fd);
    tcg_temp_free_ptr(fpst);

    return true;
}

static bool do_vfp_3op_hp(DisasContext *s, VFPGen3OpSPFn *fn,
                          int vd, int vn, int vm, bool reads_vd)
{
    /*
     * Do a half-precision operation. Functionally this is
     * the same as do_vfp_3op_sp(), except:
     *  - it uses the FPST_FPCR_F16
     *  - it doesn't need the VFP vector handling (fp16 is a
     *    v8 feature, and in v8 VFP vectors don't exist)
     *  - it does the aa32_fp16_arith feature test
     */
    TCGv_i32 f0, f1, fd;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    f0 = tcg_temp_new_i32();
    f1 = tcg_temp_new_i32();
    fd = tcg_temp_new_i32();
    fpst = fpstatus_ptr(FPST_FPCR_F16);

    vfp_load_reg32(f0, vn);
    vfp_load_reg32(f1, vm);

    if (reads_vd) {
        vfp_load_reg32(fd, vd);
    }
    fn(fd, f0, f1, fpst);
    vfp_store_reg32(fd, vd);

    tcg_temp_free_i32(f0);
    tcg_temp_free_i32(f1);
    tcg_temp_free_i32(fd);
    tcg_temp_free_ptr(fpst);

    return true;
}

static bool do_vfp_3op_dp(DisasContext *s, VFPGen3OpDPFn *fn,
                          int vd, int vn, int vm, bool reads_vd)
{
    uint32_t delta_m = 0;
    uint32_t delta_d = 0;
    int veclen = s->vec_len;
    TCGv_i64 f0, f1, fd;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && ((vd | vn | vm) & 0x10)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fpshvec, s) &&
        (veclen != 0 || s->vec_stride != 0)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (veclen > 0) {
        /* Figure out what type of vector operation this is.  */
        if (vfp_dreg_is_scalar(vd)) {
            /* scalar */
            veclen = 0;
        } else {
            delta_d = (s->vec_stride >> 1) + 1;

            if (vfp_dreg_is_scalar(vm)) {
                /* mixed scalar/vector */
                delta_m = 0;
            } else {
                /* vector */
                delta_m = delta_d;
            }
        }
    }

    f0 = tcg_temp_new_i64();
    f1 = tcg_temp_new_i64();
    fd = tcg_temp_new_i64();
    fpst = fpstatus_ptr(FPST_FPCR);

    vfp_load_reg64(f0, vn);
    vfp_load_reg64(f1, vm);

    for (;;) {
        if (reads_vd) {
            vfp_load_reg64(fd, vd);
        }
        fn(fd, f0, f1, fpst);
        vfp_store_reg64(fd, vd);

        if (veclen == 0) {
            break;
        }
        /* Set up the operands for the next iteration */
        veclen--;
        vd = vfp_advance_dreg(vd, delta_d);
        vn = vfp_advance_dreg(vn, delta_d);
        vfp_load_reg64(f0, vn);
        if (delta_m) {
            vm = vfp_advance_dreg(vm, delta_m);
            vfp_load_reg64(f1, vm);
        }
    }

    tcg_temp_free_i64(f0);
    tcg_temp_free_i64(f1);
    tcg_temp_free_i64(fd);
    tcg_temp_free_ptr(fpst);

    return true;
}

static bool do_vfp_2op_sp(DisasContext *s, VFPGen2OpSPFn *fn, int vd, int vm)
{
    uint32_t delta_m = 0;
    uint32_t delta_d = 0;
    int veclen = s->vec_len;
    TCGv_i32 f0, fd;

    /* Note that the caller must check the aa32_fpsp_v2 feature. */

    if (!dc_isar_feature(aa32_fpshvec, s) &&
        (veclen != 0 || s->vec_stride != 0)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (veclen > 0) {
        /* Figure out what type of vector operation this is.  */
        if (vfp_sreg_is_scalar(vd)) {
            /* scalar */
            veclen = 0;
        } else {
            delta_d = s->vec_stride + 1;

            if (vfp_sreg_is_scalar(vm)) {
                /* mixed scalar/vector */
                delta_m = 0;
            } else {
                /* vector */
                delta_m = delta_d;
            }
        }
    }

    f0 = tcg_temp_new_i32();
    fd = tcg_temp_new_i32();

    vfp_load_reg32(f0, vm);

    for (;;) {
        fn(fd, f0);
        vfp_store_reg32(fd, vd);

        if (veclen == 0) {
            break;
        }

        if (delta_m == 0) {
            /* single source one-many */
            while (veclen--) {
                vd = vfp_advance_sreg(vd, delta_d);
                vfp_store_reg32(fd, vd);
            }
            break;
        }

        /* Set up the operands for the next iteration */
        veclen--;
        vd = vfp_advance_sreg(vd, delta_d);
        vm = vfp_advance_sreg(vm, delta_m);
        vfp_load_reg32(f0, vm);
    }

    tcg_temp_free_i32(f0);
    tcg_temp_free_i32(fd);

    return true;
}

static bool do_vfp_2op_hp(DisasContext *s, VFPGen2OpSPFn *fn, int vd, int vm)
{
    /*
     * Do a half-precision operation. Functionally this is
     * the same as do_vfp_2op_sp(), except:
     *  - it doesn't need the VFP vector handling (fp16 is a
     *    v8 feature, and in v8 VFP vectors don't exist)
     *  - it does the aa32_fp16_arith feature test
     */
    TCGv_i32 f0;

    /* Note that the caller must check the aa32_fp16_arith feature */

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    f0 = tcg_temp_new_i32();
    vfp_load_reg32(f0, vm);
    fn(f0, f0);
    vfp_store_reg32(f0, vd);
    tcg_temp_free_i32(f0);

    return true;
}

static bool do_vfp_2op_dp(DisasContext *s, VFPGen2OpDPFn *fn, int vd, int vm)
{
    uint32_t delta_m = 0;
    uint32_t delta_d = 0;
    int veclen = s->vec_len;
    TCGv_i64 f0, fd;

    /* Note that the caller must check the aa32_fpdp_v2 feature. */

    /* UNDEF accesses to D16-D31 if they don't exist */
    if (!dc_isar_feature(aa32_simd_r32, s) && ((vd | vm) & 0x10)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fpshvec, s) &&
        (veclen != 0 || s->vec_stride != 0)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (veclen > 0) {
        /* Figure out what type of vector operation this is.  */
        if (vfp_dreg_is_scalar(vd)) {
            /* scalar */
            veclen = 0;
        } else {
            delta_d = (s->vec_stride >> 1) + 1;

            if (vfp_dreg_is_scalar(vm)) {
                /* mixed scalar/vector */
                delta_m = 0;
            } else {
                /* vector */
                delta_m = delta_d;
            }
        }
    }

    f0 = tcg_temp_new_i64();
    fd = tcg_temp_new_i64();

    vfp_load_reg64(f0, vm);

    for (;;) {
        fn(fd, f0);
        vfp_store_reg64(fd, vd);

        if (veclen == 0) {
            break;
        }

        if (delta_m == 0) {
            /* single source one-many */
            while (veclen--) {
                vd = vfp_advance_dreg(vd, delta_d);
                vfp_store_reg64(fd, vd);
            }
            break;
        }

        /* Set up the operands for the next iteration */
        veclen--;
        vd = vfp_advance_dreg(vd, delta_d);
        vd = vfp_advance_dreg(vm, delta_m);
        vfp_load_reg64(f0, vm);
    }

    tcg_temp_free_i64(f0);
    tcg_temp_free_i64(fd);

    return true;
}

static void gen_VMLA_hp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /* Note that order of inputs to the add matters for NaNs */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_mulh(tmp, vn, vm, fpst);
    gen_helper_vfp_addh(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VMLA_hp(DisasContext *s, arg_VMLA_sp *a)
{
    return do_vfp_3op_hp(s, gen_VMLA_hp, a->vd, a->vn, a->vm, true);
}

static void gen_VMLA_sp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /* Note that order of inputs to the add matters for NaNs */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_muls(tmp, vn, vm, fpst);
    gen_helper_vfp_adds(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VMLA_sp(DisasContext *s, arg_VMLA_sp *a)
{
    return do_vfp_3op_sp(s, gen_VMLA_sp, a->vd, a->vn, a->vm, true);
}

static void gen_VMLA_dp(TCGv_i64 vd, TCGv_i64 vn, TCGv_i64 vm, TCGv_ptr fpst)
{
    /* Note that order of inputs to the add matters for NaNs */
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_helper_vfp_muld(tmp, vn, vm, fpst);
    gen_helper_vfp_addd(vd, vd, tmp, fpst);
    tcg_temp_free_i64(tmp);
}

static bool trans_VMLA_dp(DisasContext *s, arg_VMLA_dp *a)
{
    return do_vfp_3op_dp(s, gen_VMLA_dp, a->vd, a->vn, a->vm, true);
}

static void gen_VMLS_hp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /*
     * VMLS: vd = vd + -(vn * vm)
     * Note that order of inputs to the add matters for NaNs.
     */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_mulh(tmp, vn, vm, fpst);
    gen_helper_vfp_negh(tmp, tmp);
    gen_helper_vfp_addh(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VMLS_hp(DisasContext *s, arg_VMLS_sp *a)
{
    return do_vfp_3op_hp(s, gen_VMLS_hp, a->vd, a->vn, a->vm, true);
}

static void gen_VMLS_sp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /*
     * VMLS: vd = vd + -(vn * vm)
     * Note that order of inputs to the add matters for NaNs.
     */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_muls(tmp, vn, vm, fpst);
    gen_helper_vfp_negs(tmp, tmp);
    gen_helper_vfp_adds(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VMLS_sp(DisasContext *s, arg_VMLS_sp *a)
{
    return do_vfp_3op_sp(s, gen_VMLS_sp, a->vd, a->vn, a->vm, true);
}

static void gen_VMLS_dp(TCGv_i64 vd, TCGv_i64 vn, TCGv_i64 vm, TCGv_ptr fpst)
{
    /*
     * VMLS: vd = vd + -(vn * vm)
     * Note that order of inputs to the add matters for NaNs.
     */
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_helper_vfp_muld(tmp, vn, vm, fpst);
    gen_helper_vfp_negd(tmp, tmp);
    gen_helper_vfp_addd(vd, vd, tmp, fpst);
    tcg_temp_free_i64(tmp);
}

static bool trans_VMLS_dp(DisasContext *s, arg_VMLS_dp *a)
{
    return do_vfp_3op_dp(s, gen_VMLS_dp, a->vd, a->vn, a->vm, true);
}

static void gen_VNMLS_hp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /*
     * VNMLS: -fd + (fn * fm)
     * Note that it isn't valid to replace (-A + B) with (B - A) or similar
     * plausible looking simplifications because this will give wrong results
     * for NaNs.
     */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_mulh(tmp, vn, vm, fpst);
    gen_helper_vfp_negh(vd, vd);
    gen_helper_vfp_addh(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VNMLS_hp(DisasContext *s, arg_VNMLS_sp *a)
{
    return do_vfp_3op_hp(s, gen_VNMLS_hp, a->vd, a->vn, a->vm, true);
}

static void gen_VNMLS_sp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /*
     * VNMLS: -fd + (fn * fm)
     * Note that it isn't valid to replace (-A + B) with (B - A) or similar
     * plausible looking simplifications because this will give wrong results
     * for NaNs.
     */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_muls(tmp, vn, vm, fpst);
    gen_helper_vfp_negs(vd, vd);
    gen_helper_vfp_adds(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VNMLS_sp(DisasContext *s, arg_VNMLS_sp *a)
{
    return do_vfp_3op_sp(s, gen_VNMLS_sp, a->vd, a->vn, a->vm, true);
}

static void gen_VNMLS_dp(TCGv_i64 vd, TCGv_i64 vn, TCGv_i64 vm, TCGv_ptr fpst)
{
    /*
     * VNMLS: -fd + (fn * fm)
     * Note that it isn't valid to replace (-A + B) with (B - A) or similar
     * plausible looking simplifications because this will give wrong results
     * for NaNs.
     */
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_helper_vfp_muld(tmp, vn, vm, fpst);
    gen_helper_vfp_negd(vd, vd);
    gen_helper_vfp_addd(vd, vd, tmp, fpst);
    tcg_temp_free_i64(tmp);
}

static bool trans_VNMLS_dp(DisasContext *s, arg_VNMLS_dp *a)
{
    return do_vfp_3op_dp(s, gen_VNMLS_dp, a->vd, a->vn, a->vm, true);
}

static void gen_VNMLA_hp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /* VNMLA: -fd + -(fn * fm) */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_mulh(tmp, vn, vm, fpst);
    gen_helper_vfp_negh(tmp, tmp);
    gen_helper_vfp_negh(vd, vd);
    gen_helper_vfp_addh(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VNMLA_hp(DisasContext *s, arg_VNMLA_sp *a)
{
    return do_vfp_3op_hp(s, gen_VNMLA_hp, a->vd, a->vn, a->vm, true);
}

static void gen_VNMLA_sp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /* VNMLA: -fd + -(fn * fm) */
    TCGv_i32 tmp = tcg_temp_new_i32();

    gen_helper_vfp_muls(tmp, vn, vm, fpst);
    gen_helper_vfp_negs(tmp, tmp);
    gen_helper_vfp_negs(vd, vd);
    gen_helper_vfp_adds(vd, vd, tmp, fpst);
    tcg_temp_free_i32(tmp);
}

static bool trans_VNMLA_sp(DisasContext *s, arg_VNMLA_sp *a)
{
    return do_vfp_3op_sp(s, gen_VNMLA_sp, a->vd, a->vn, a->vm, true);
}

static void gen_VNMLA_dp(TCGv_i64 vd, TCGv_i64 vn, TCGv_i64 vm, TCGv_ptr fpst)
{
    /* VNMLA: -fd + (fn * fm) */
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_helper_vfp_muld(tmp, vn, vm, fpst);
    gen_helper_vfp_negd(tmp, tmp);
    gen_helper_vfp_negd(vd, vd);
    gen_helper_vfp_addd(vd, vd, tmp, fpst);
    tcg_temp_free_i64(tmp);
}

static bool trans_VNMLA_dp(DisasContext *s, arg_VNMLA_dp *a)
{
    return do_vfp_3op_dp(s, gen_VNMLA_dp, a->vd, a->vn, a->vm, true);
}

static bool trans_VMUL_hp(DisasContext *s, arg_VMUL_sp *a)
{
    return do_vfp_3op_hp(s, gen_helper_vfp_mulh, a->vd, a->vn, a->vm, false);
}

static bool trans_VMUL_sp(DisasContext *s, arg_VMUL_sp *a)
{
    return do_vfp_3op_sp(s, gen_helper_vfp_muls, a->vd, a->vn, a->vm, false);
}

static bool trans_VMUL_dp(DisasContext *s, arg_VMUL_dp *a)
{
    return do_vfp_3op_dp(s, gen_helper_vfp_muld, a->vd, a->vn, a->vm, false);
}

static void gen_VNMUL_hp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /* VNMUL: -(fn * fm) */
    gen_helper_vfp_mulh(vd, vn, vm, fpst);
    gen_helper_vfp_negh(vd, vd);
}

static bool trans_VNMUL_hp(DisasContext *s, arg_VNMUL_sp *a)
{
    return do_vfp_3op_hp(s, gen_VNMUL_hp, a->vd, a->vn, a->vm, false);
}

static void gen_VNMUL_sp(TCGv_i32 vd, TCGv_i32 vn, TCGv_i32 vm, TCGv_ptr fpst)
{
    /* VNMUL: -(fn * fm) */
    gen_helper_vfp_muls(vd, vn, vm, fpst);
    gen_helper_vfp_negs(vd, vd);
}

static bool trans_VNMUL_sp(DisasContext *s, arg_VNMUL_sp *a)
{
    return do_vfp_3op_sp(s, gen_VNMUL_sp, a->vd, a->vn, a->vm, false);
}

static void gen_VNMUL_dp(TCGv_i64 vd, TCGv_i64 vn, TCGv_i64 vm, TCGv_ptr fpst)
{
    /* VNMUL: -(fn * fm) */
    gen_helper_vfp_muld(vd, vn, vm, fpst);
    gen_helper_vfp_negd(vd, vd);
}

static bool trans_VNMUL_dp(DisasContext *s, arg_VNMUL_dp *a)
{
    return do_vfp_3op_dp(s, gen_VNMUL_dp, a->vd, a->vn, a->vm, false);
}

static bool trans_VADD_hp(DisasContext *s, arg_VADD_sp *a)
{
    return do_vfp_3op_hp(s, gen_helper_vfp_addh, a->vd, a->vn, a->vm, false);
}

static bool trans_VADD_sp(DisasContext *s, arg_VADD_sp *a)
{
    return do_vfp_3op_sp(s, gen_helper_vfp_adds, a->vd, a->vn, a->vm, false);
}

static bool trans_VADD_dp(DisasContext *s, arg_VADD_dp *a)
{
    return do_vfp_3op_dp(s, gen_helper_vfp_addd, a->vd, a->vn, a->vm, false);
}

static bool trans_VSUB_hp(DisasContext *s, arg_VSUB_sp *a)
{
    return do_vfp_3op_hp(s, gen_helper_vfp_subh, a->vd, a->vn, a->vm, false);
}

static bool trans_VSUB_sp(DisasContext *s, arg_VSUB_sp *a)
{
    return do_vfp_3op_sp(s, gen_helper_vfp_subs, a->vd, a->vn, a->vm, false);
}

static bool trans_VSUB_dp(DisasContext *s, arg_VSUB_dp *a)
{
    return do_vfp_3op_dp(s, gen_helper_vfp_subd, a->vd, a->vn, a->vm, false);
}

static bool trans_VDIV_hp(DisasContext *s, arg_VDIV_sp *a)
{
    return do_vfp_3op_hp(s, gen_helper_vfp_divh, a->vd, a->vn, a->vm, false);
}

static bool trans_VDIV_sp(DisasContext *s, arg_VDIV_sp *a)
{
    return do_vfp_3op_sp(s, gen_helper_vfp_divs, a->vd, a->vn, a->vm, false);
}

static bool trans_VDIV_dp(DisasContext *s, arg_VDIV_dp *a)
{
    return do_vfp_3op_dp(s, gen_helper_vfp_divd, a->vd, a->vn, a->vm, false);
}

static bool trans_VMINNM_hp(DisasContext *s, arg_VMINNM_sp *a)
{
    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }
    return do_vfp_3op_hp(s, gen_helper_vfp_minnumh,
                         a->vd, a->vn, a->vm, false);
}

static bool trans_VMAXNM_hp(DisasContext *s, arg_VMAXNM_sp *a)
{
    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }
    return do_vfp_3op_hp(s, gen_helper_vfp_maxnumh,
                         a->vd, a->vn, a->vm, false);
}

static bool trans_VMINNM_sp(DisasContext *s, arg_VMINNM_sp *a)
{
    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }
    return do_vfp_3op_sp(s, gen_helper_vfp_minnums,
                         a->vd, a->vn, a->vm, false);
}

static bool trans_VMAXNM_sp(DisasContext *s, arg_VMAXNM_sp *a)
{
    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }
    return do_vfp_3op_sp(s, gen_helper_vfp_maxnums,
                         a->vd, a->vn, a->vm, false);
}

static bool trans_VMINNM_dp(DisasContext *s, arg_VMINNM_dp *a)
{
    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }
    return do_vfp_3op_dp(s, gen_helper_vfp_minnumd,
                         a->vd, a->vn, a->vm, false);
}

static bool trans_VMAXNM_dp(DisasContext *s, arg_VMAXNM_dp *a)
{
    if (!dc_isar_feature(aa32_vminmaxnm, s)) {
        return false;
    }
    return do_vfp_3op_dp(s, gen_helper_vfp_maxnumd,
                         a->vd, a->vn, a->vm, false);
}

static bool do_vfm_hp(DisasContext *s, arg_VFMA_sp *a, bool neg_n, bool neg_d)
{
    /*
     * VFNMA : fd = muladd(-fd,  fn, fm)
     * VFNMS : fd = muladd(-fd, -fn, fm)
     * VFMA  : fd = muladd( fd,  fn, fm)
     * VFMS  : fd = muladd( fd, -fn, fm)
     *
     * These are fused multiply-add, and must be done as one floating
     * point operation with no rounding between the multiplication and
     * addition steps.  NB that doing the negations here as separate
     * steps is correct : an input NaN should come out with its sign
     * bit flipped if it is a negated-input.
     */
    TCGv_ptr fpst;
    TCGv_i32 vn, vm, vd;

    /*
     * Present in VFPv4 only, and only with the FP16 extension.
     * Note that we can't rely on the SIMDFMAC check alone, because
     * in a Neon-no-VFP core that ID register field will be non-zero.
     */
    if (!dc_isar_feature(aa32_fp16_arith, s) ||
        !dc_isar_feature(aa32_simdfmac, s) ||
        !dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }

    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vn = tcg_temp_new_i32();
    vm = tcg_temp_new_i32();
    vd = tcg_temp_new_i32();

    vfp_load_reg32(vn, a->vn);
    vfp_load_reg32(vm, a->vm);
    if (neg_n) {
        /* VFNMS, VFMS */
        gen_helper_vfp_negh(vn, vn);
    }
    vfp_load_reg32(vd, a->vd);
    if (neg_d) {
        /* VFNMA, VFNMS */
        gen_helper_vfp_negh(vd, vd);
    }
    fpst = fpstatus_ptr(FPST_FPCR_F16);
    gen_helper_vfp_muladdh(vd, vn, vm, vd, fpst);
    vfp_store_reg32(vd, a->vd);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(vn);
    tcg_temp_free_i32(vm);
    tcg_temp_free_i32(vd);

    return true;
}

static bool do_vfm_sp(DisasContext *s, arg_VFMA_sp *a, bool neg_n, bool neg_d)
{
    /*
     * VFNMA : fd = muladd(-fd,  fn, fm)
     * VFNMS : fd = muladd(-fd, -fn, fm)
     * VFMA  : fd = muladd( fd,  fn, fm)
     * VFMS  : fd = muladd( fd, -fn, fm)
     *
     * These are fused multiply-add, and must be done as one floating
     * point operation with no rounding between the multiplication and
     * addition steps.  NB that doing the negations here as separate
     * steps is correct : an input NaN should come out with its sign
     * bit flipped if it is a negated-input.
     */
    TCGv_ptr fpst;
    TCGv_i32 vn, vm, vd;

    /*
     * Present in VFPv4 only.
     * Note that we can't rely on the SIMDFMAC check alone, because
     * in a Neon-no-VFP core that ID register field will be non-zero.
     */
    if (!dc_isar_feature(aa32_simdfmac, s) ||
        !dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }
    /*
     * In v7A, UNPREDICTABLE with non-zero vector length/stride; from
     * v8A, must UNDEF. We choose to UNDEF for both v7A and v8A.
     */
    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vn = tcg_temp_new_i32();
    vm = tcg_temp_new_i32();
    vd = tcg_temp_new_i32();

    vfp_load_reg32(vn, a->vn);
    vfp_load_reg32(vm, a->vm);
    if (neg_n) {
        /* VFNMS, VFMS */
        gen_helper_vfp_negs(vn, vn);
    }
    vfp_load_reg32(vd, a->vd);
    if (neg_d) {
        /* VFNMA, VFNMS */
        gen_helper_vfp_negs(vd, vd);
    }
    fpst = fpstatus_ptr(FPST_FPCR);
    gen_helper_vfp_muladds(vd, vn, vm, vd, fpst);
    vfp_store_reg32(vd, a->vd);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(vn);
    tcg_temp_free_i32(vm);
    tcg_temp_free_i32(vd);

    return true;
}

static bool do_vfm_dp(DisasContext *s, arg_VFMA_dp *a, bool neg_n, bool neg_d)
{
    /*
     * VFNMA : fd = muladd(-fd,  fn, fm)
     * VFNMS : fd = muladd(-fd, -fn, fm)
     * VFMA  : fd = muladd( fd,  fn, fm)
     * VFMS  : fd = muladd( fd, -fn, fm)
     *
     * These are fused multiply-add, and must be done as one floating
     * point operation with no rounding between the multiplication and
     * addition steps.  NB that doing the negations here as separate
     * steps is correct : an input NaN should come out with its sign
     * bit flipped if it is a negated-input.
     */
    TCGv_ptr fpst;
    TCGv_i64 vn, vm, vd;

    /*
     * Present in VFPv4 only.
     * Note that we can't rely on the SIMDFMAC check alone, because
     * in a Neon-no-VFP core that ID register field will be non-zero.
     */
    if (!dc_isar_feature(aa32_simdfmac, s) ||
        !dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }
    /*
     * In v7A, UNPREDICTABLE with non-zero vector length/stride; from
     * v8A, must UNDEF. We choose to UNDEF for both v7A and v8A.
     */
    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) &&
        ((a->vd | a->vn | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vn = tcg_temp_new_i64();
    vm = tcg_temp_new_i64();
    vd = tcg_temp_new_i64();

    vfp_load_reg64(vn, a->vn);
    vfp_load_reg64(vm, a->vm);
    if (neg_n) {
        /* VFNMS, VFMS */
        gen_helper_vfp_negd(vn, vn);
    }
    vfp_load_reg64(vd, a->vd);
    if (neg_d) {
        /* VFNMA, VFNMS */
        gen_helper_vfp_negd(vd, vd);
    }
    fpst = fpstatus_ptr(FPST_FPCR);
    gen_helper_vfp_muladdd(vd, vn, vm, vd, fpst);
    vfp_store_reg64(vd, a->vd);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i64(vn);
    tcg_temp_free_i64(vm);
    tcg_temp_free_i64(vd);

    return true;
}

#define MAKE_ONE_VFM_TRANS_FN(INSN, PREC, NEGN, NEGD)                   \
    static bool trans_##INSN##_##PREC(DisasContext *s,                  \
                                      arg_##INSN##_##PREC *a)           \
    {                                                                   \
        return do_vfm_##PREC(s, a, NEGN, NEGD);                         \
    }

#define MAKE_VFM_TRANS_FNS(PREC) \
    MAKE_ONE_VFM_TRANS_FN(VFMA, PREC, false, false) \
    MAKE_ONE_VFM_TRANS_FN(VFMS, PREC, true, false) \
    MAKE_ONE_VFM_TRANS_FN(VFNMA, PREC, false, true) \
    MAKE_ONE_VFM_TRANS_FN(VFNMS, PREC, true, true)

MAKE_VFM_TRANS_FNS(hp)
MAKE_VFM_TRANS_FNS(sp)
MAKE_VFM_TRANS_FNS(dp)

static bool trans_VMOV_imm_hp(DisasContext *s, arg_VMOV_imm_sp *a)
{
    TCGv_i32 fd;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fd = tcg_const_i32(vfp_expand_imm(MO_16, a->imm));
    vfp_store_reg32(fd, a->vd);
    tcg_temp_free_i32(fd);
    return true;
}

static bool trans_VMOV_imm_sp(DisasContext *s, arg_VMOV_imm_sp *a)
{
    uint32_t delta_d = 0;
    int veclen = s->vec_len;
    TCGv_i32 fd;
    uint32_t vd;

    vd = a->vd;

    if (!dc_isar_feature(aa32_fpsp_v3, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fpshvec, s) &&
        (veclen != 0 || s->vec_stride != 0)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (veclen > 0) {
        /* Figure out what type of vector operation this is.  */
        if (vfp_sreg_is_scalar(vd)) {
            /* scalar */
            veclen = 0;
        } else {
            delta_d = s->vec_stride + 1;
        }
    }

    fd = tcg_const_i32(vfp_expand_imm(MO_32, a->imm));

    for (;;) {
        vfp_store_reg32(fd, vd);

        if (veclen == 0) {
            break;
        }

        /* Set up the operands for the next iteration */
        veclen--;
        vd = vfp_advance_sreg(vd, delta_d);
    }

    tcg_temp_free_i32(fd);
    return true;
}

static bool trans_VMOV_imm_dp(DisasContext *s, arg_VMOV_imm_dp *a)
{
    uint32_t delta_d = 0;
    int veclen = s->vec_len;
    TCGv_i64 fd;
    uint32_t vd;

    vd = a->vd;

    if (!dc_isar_feature(aa32_fpdp_v3, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (vd & 0x10)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fpshvec, s) &&
        (veclen != 0 || s->vec_stride != 0)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    if (veclen > 0) {
        /* Figure out what type of vector operation this is.  */
        if (vfp_dreg_is_scalar(vd)) {
            /* scalar */
            veclen = 0;
        } else {
            delta_d = (s->vec_stride >> 1) + 1;
        }
    }

    fd = tcg_const_i64(vfp_expand_imm(MO_64, a->imm));

    for (;;) {
        vfp_store_reg64(fd, vd);

        if (veclen == 0) {
            break;
        }

        /* Set up the operands for the next iteration */
        veclen--;
        vd = vfp_advance_dreg(vd, delta_d);
    }

    tcg_temp_free_i64(fd);
    return true;
}

#define DO_VFP_2OP(INSN, PREC, FN, CHECK)                       \
    static bool trans_##INSN##_##PREC(DisasContext *s,          \
                                      arg_##INSN##_##PREC *a)   \
    {                                                           \
        if (!dc_isar_feature(CHECK, s)) {                       \
            return false;                                       \
        }                                                       \
        return do_vfp_2op_##PREC(s, FN, a->vd, a->vm);          \
    }

#define DO_VFP_VMOV(INSN, PREC, FN)                             \
    static bool trans_##INSN##_##PREC(DisasContext *s,          \
                                      arg_##INSN##_##PREC *a)   \
    {                                                           \
        if (!dc_isar_feature(aa32_fp##PREC##_v2, s) &&          \
            !dc_isar_feature(aa32_mve, s)) {                    \
            return false;                                       \
        }                                                       \
        return do_vfp_2op_##PREC(s, FN, a->vd, a->vm);          \
    }

DO_VFP_VMOV(VMOV_reg, sp, tcg_gen_mov_i32)
DO_VFP_VMOV(VMOV_reg, dp, tcg_gen_mov_i64)

DO_VFP_2OP(VABS, hp, gen_helper_vfp_absh, aa32_fp16_arith)
DO_VFP_2OP(VABS, sp, gen_helper_vfp_abss, aa32_fpsp_v2)
DO_VFP_2OP(VABS, dp, gen_helper_vfp_absd, aa32_fpdp_v2)

DO_VFP_2OP(VNEG, hp, gen_helper_vfp_negh, aa32_fp16_arith)
DO_VFP_2OP(VNEG, sp, gen_helper_vfp_negs, aa32_fpsp_v2)
DO_VFP_2OP(VNEG, dp, gen_helper_vfp_negd, aa32_fpdp_v2)

static void gen_VSQRT_hp(TCGv_i32 vd, TCGv_i32 vm)
{
    gen_helper_vfp_sqrth(vd, vm, cpu_env);
}

static void gen_VSQRT_sp(TCGv_i32 vd, TCGv_i32 vm)
{
    gen_helper_vfp_sqrts(vd, vm, cpu_env);
}

static void gen_VSQRT_dp(TCGv_i64 vd, TCGv_i64 vm)
{
    gen_helper_vfp_sqrtd(vd, vm, cpu_env);
}

DO_VFP_2OP(VSQRT, hp, gen_VSQRT_hp, aa32_fp16_arith)
DO_VFP_2OP(VSQRT, sp, gen_VSQRT_sp, aa32_fpsp_v2)
DO_VFP_2OP(VSQRT, dp, gen_VSQRT_dp, aa32_fpdp_v2)

static bool trans_VCMP_hp(DisasContext *s, arg_VCMP_sp *a)
{
    TCGv_i32 vd, vm;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    /* Vm/M bits must be zero for the Z variant */
    if (a->z && a->vm != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vd = tcg_temp_new_i32();
    vm = tcg_temp_new_i32();

    vfp_load_reg32(vd, a->vd);
    if (a->z) {
        tcg_gen_movi_i32(vm, 0);
    } else {
        vfp_load_reg32(vm, a->vm);
    }

    if (a->e) {
        gen_helper_vfp_cmpeh(vd, vm, cpu_env);
    } else {
        gen_helper_vfp_cmph(vd, vm, cpu_env);
    }

    tcg_temp_free_i32(vd);
    tcg_temp_free_i32(vm);

    return true;
}

static bool trans_VCMP_sp(DisasContext *s, arg_VCMP_sp *a)
{
    TCGv_i32 vd, vm;

    if (!dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }

    /* Vm/M bits must be zero for the Z variant */
    if (a->z && a->vm != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vd = tcg_temp_new_i32();
    vm = tcg_temp_new_i32();

    vfp_load_reg32(vd, a->vd);
    if (a->z) {
        tcg_gen_movi_i32(vm, 0);
    } else {
        vfp_load_reg32(vm, a->vm);
    }

    if (a->e) {
        gen_helper_vfp_cmpes(vd, vm, cpu_env);
    } else {
        gen_helper_vfp_cmps(vd, vm, cpu_env);
    }

    tcg_temp_free_i32(vd);
    tcg_temp_free_i32(vm);

    return true;
}

static bool trans_VCMP_dp(DisasContext *s, arg_VCMP_dp *a)
{
    TCGv_i64 vd, vm;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    /* Vm/M bits must be zero for the Z variant */
    if (a->z && a->vm != 0) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vd = tcg_temp_new_i64();
    vm = tcg_temp_new_i64();

    vfp_load_reg64(vd, a->vd);
    if (a->z) {
        tcg_gen_movi_i64(vm, 0);
    } else {
        vfp_load_reg64(vm, a->vm);
    }

    if (a->e) {
        gen_helper_vfp_cmped(vd, vm, cpu_env);
    } else {
        gen_helper_vfp_cmpd(vd, vm, cpu_env);
    }

    tcg_temp_free_i64(vd);
    tcg_temp_free_i64(vm);

    return true;
}

static bool trans_VCVT_f32_f16(DisasContext *s, arg_VCVT_f32_f16 *a)
{
    TCGv_ptr fpst;
    TCGv_i32 ahp_mode;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fp16_spconv, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    ahp_mode = get_ahp_flag();
    tmp = tcg_temp_new_i32();
    /* The T bit tells us if we want the low or high 16 bits of Vm */
    tcg_gen_ld16u_i32(tmp, cpu_env, vfp_f16_offset(a->vm, a->t));
    gen_helper_vfp_fcvt_f16_to_f32(tmp, tmp, fpst, ahp_mode);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_i32(ahp_mode);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VCVT_f64_f16(DisasContext *s, arg_VCVT_f64_f16 *a)
{
    TCGv_ptr fpst;
    TCGv_i32 ahp_mode;
    TCGv_i32 tmp;
    TCGv_i64 vd;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fp16_dpconv, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd  & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    ahp_mode = get_ahp_flag();
    tmp = tcg_temp_new_i32();
    /* The T bit tells us if we want the low or high 16 bits of Vm */
    tcg_gen_ld16u_i32(tmp, cpu_env, vfp_f16_offset(a->vm, a->t));
    vd = tcg_temp_new_i64();
    gen_helper_vfp_fcvt_f16_to_f64(vd, tmp, fpst, ahp_mode);
    vfp_store_reg64(vd, a->vd);
    tcg_temp_free_i32(ahp_mode);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    tcg_temp_free_i64(vd);
    return true;
}

static bool trans_VCVT_b16_f32(DisasContext *s, arg_VCVT_b16_f32 *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_bf16, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    tmp = tcg_temp_new_i32();

    vfp_load_reg32(tmp, a->vm);
    gen_helper_bfcvt(tmp, tmp, fpst);
    tcg_gen_st16_i32(tmp, cpu_env, vfp_f16_offset(a->vd, a->t));
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VCVT_f16_f32(DisasContext *s, arg_VCVT_f16_f32 *a)
{
    TCGv_ptr fpst;
    TCGv_i32 ahp_mode;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fp16_spconv, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    ahp_mode = get_ahp_flag();
    tmp = tcg_temp_new_i32();

    vfp_load_reg32(tmp, a->vm);
    gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp_mode);
    tcg_gen_st16_i32(tmp, cpu_env, vfp_f16_offset(a->vd, a->t));
    tcg_temp_free_i32(ahp_mode);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VCVT_f16_f64(DisasContext *s, arg_VCVT_f16_f64 *a)
{
    TCGv_ptr fpst;
    TCGv_i32 ahp_mode;
    TCGv_i32 tmp;
    TCGv_i64 vm;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_fp16_dpconv, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vm  & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    ahp_mode = get_ahp_flag();
    tmp = tcg_temp_new_i32();
    vm = tcg_temp_new_i64();

    vfp_load_reg64(vm, a->vm);
    gen_helper_vfp_fcvt_f64_to_f16(tmp, vm, fpst, ahp_mode);
    tcg_temp_free_i64(vm);
    tcg_gen_st16_i32(tmp, cpu_env, vfp_f16_offset(a->vd, a->t));
    tcg_temp_free_i32(ahp_mode);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTR_hp(DisasContext *s, arg_VRINTR_sp *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    vfp_load_reg32(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR_F16);
    gen_helper_rinth(tmp, tmp, fpst);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTR_sp(DisasContext *s, arg_VRINTR_sp *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    vfp_load_reg32(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    gen_helper_rints(tmp, tmp, fpst);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTR_dp(DisasContext *s, arg_VRINTR_dp *a)
{
    TCGv_ptr fpst;
    TCGv_i64 tmp;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i64();
    vfp_load_reg64(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    gen_helper_rintd(tmp, tmp, fpst);
    vfp_store_reg64(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i64(tmp);
    return true;
}

static bool trans_VRINTZ_hp(DisasContext *s, arg_VRINTZ_sp *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;
    TCGv_i32 tcg_rmode;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    vfp_load_reg32(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR_F16);
    tcg_rmode = tcg_const_i32(float_round_to_zero);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    gen_helper_rinth(tmp, tmp, fpst);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tcg_rmode);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTZ_sp(DisasContext *s, arg_VRINTZ_sp *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;
    TCGv_i32 tcg_rmode;

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    vfp_load_reg32(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    tcg_rmode = tcg_const_i32(float_round_to_zero);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    gen_helper_rints(tmp, tmp, fpst);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tcg_rmode);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTZ_dp(DisasContext *s, arg_VRINTZ_dp *a)
{
    TCGv_ptr fpst;
    TCGv_i64 tmp;
    TCGv_i32 tcg_rmode;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i64();
    vfp_load_reg64(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    tcg_rmode = tcg_const_i32(float_round_to_zero);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    gen_helper_rintd(tmp, tmp, fpst);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
    vfp_store_reg64(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i64(tmp);
    tcg_temp_free_i32(tcg_rmode);
    return true;
}

static bool trans_VRINTX_hp(DisasContext *s, arg_VRINTX_sp *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    vfp_load_reg32(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR_F16);
    gen_helper_rinth_exact(tmp, tmp, fpst);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTX_sp(DisasContext *s, arg_VRINTX_sp *a)
{
    TCGv_ptr fpst;
    TCGv_i32 tmp;

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i32();
    vfp_load_reg32(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    gen_helper_rints_exact(tmp, tmp, fpst);
    vfp_store_reg32(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tmp);
    return true;
}

static bool trans_VRINTX_dp(DisasContext *s, arg_VRINTX_dp *a)
{
    TCGv_ptr fpst;
    TCGv_i64 tmp;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_vrint, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && ((a->vd | a->vm) & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    tmp = tcg_temp_new_i64();
    vfp_load_reg64(tmp, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    gen_helper_rintd_exact(tmp, tmp, fpst);
    vfp_store_reg64(tmp, a->vd);
    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i64(tmp);
    return true;
}

static bool trans_VCVT_sp(DisasContext *s, arg_VCVT_sp *a)
{
    TCGv_i64 vd;
    TCGv_i32 vm;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vm = tcg_temp_new_i32();
    vd = tcg_temp_new_i64();
    vfp_load_reg32(vm, a->vm);
    gen_helper_vfp_fcvtds(vd, vm, cpu_env);
    vfp_store_reg64(vd, a->vd);
    tcg_temp_free_i32(vm);
    tcg_temp_free_i64(vd);
    return true;
}

static bool trans_VCVT_dp(DisasContext *s, arg_VCVT_dp *a)
{
    TCGv_i64 vm;
    TCGv_i32 vd;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vm & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vd = tcg_temp_new_i32();
    vm = tcg_temp_new_i64();
    vfp_load_reg64(vm, a->vm);
    gen_helper_vfp_fcvtsd(vd, vm, cpu_env);
    vfp_store_reg32(vd, a->vd);
    tcg_temp_free_i32(vd);
    tcg_temp_free_i64(vm);
    return true;
}

static bool trans_VCVT_int_hp(DisasContext *s, arg_VCVT_int_sp *a)
{
    TCGv_i32 vm;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vm = tcg_temp_new_i32();
    vfp_load_reg32(vm, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR_F16);
    if (a->s) {
        /* i32 -> f16 */
        gen_helper_vfp_sitoh(vm, vm, fpst);
    } else {
        /* u32 -> f16 */
        gen_helper_vfp_uitoh(vm, vm, fpst);
    }
    vfp_store_reg32(vm, a->vd);
    tcg_temp_free_i32(vm);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_int_sp(DisasContext *s, arg_VCVT_int_sp *a)
{
    TCGv_i32 vm;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vm = tcg_temp_new_i32();
    vfp_load_reg32(vm, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    if (a->s) {
        /* i32 -> f32 */
        gen_helper_vfp_sitos(vm, vm, fpst);
    } else {
        /* u32 -> f32 */
        gen_helper_vfp_uitos(vm, vm, fpst);
    }
    vfp_store_reg32(vm, a->vd);
    tcg_temp_free_i32(vm);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_int_dp(DisasContext *s, arg_VCVT_int_dp *a)
{
    TCGv_i32 vm;
    TCGv_i64 vd;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vm = tcg_temp_new_i32();
    vd = tcg_temp_new_i64();
    vfp_load_reg32(vm, a->vm);
    fpst = fpstatus_ptr(FPST_FPCR);
    if (a->s) {
        /* i32 -> f64 */
        gen_helper_vfp_sitod(vd, vm, fpst);
    } else {
        /* u32 -> f64 */
        gen_helper_vfp_uitod(vd, vm, fpst);
    }
    vfp_store_reg64(vd, a->vd);
    tcg_temp_free_i32(vm);
    tcg_temp_free_i64(vd);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VJCVT(DisasContext *s, arg_VJCVT *a)
{
    TCGv_i32 vd;
    TCGv_i64 vm;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    if (!dc_isar_feature(aa32_jscvt, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vm & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    vm = tcg_temp_new_i64();
    vd = tcg_temp_new_i32();
    vfp_load_reg64(vm, a->vm);
    gen_helper_vjcvt(vd, vm, cpu_env);
    vfp_store_reg32(vd, a->vd);
    tcg_temp_free_i64(vm);
    tcg_temp_free_i32(vd);
    return true;
}

static bool trans_VCVT_fix_hp(DisasContext *s, arg_VCVT_fix_sp *a)
{
    TCGv_i32 vd, shift;
    TCGv_ptr fpst;
    int frac_bits;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    frac_bits = (a->opc & 1) ? (32 - a->imm) : (16 - a->imm);

    vd = tcg_temp_new_i32();
    vfp_load_reg32(vd, a->vd);

    fpst = fpstatus_ptr(FPST_FPCR_F16);
    shift = tcg_const_i32(frac_bits);

    /* Switch on op:U:sx bits */
    switch (a->opc) {
    case 0:
        gen_helper_vfp_shtoh_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 1:
        gen_helper_vfp_sltoh_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 2:
        gen_helper_vfp_uhtoh_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 3:
        gen_helper_vfp_ultoh_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 4:
        gen_helper_vfp_toshh_round_to_zero(vd, vd, shift, fpst);
        break;
    case 5:
        gen_helper_vfp_toslh_round_to_zero(vd, vd, shift, fpst);
        break;
    case 6:
        gen_helper_vfp_touhh_round_to_zero(vd, vd, shift, fpst);
        break;
    case 7:
        gen_helper_vfp_toulh_round_to_zero(vd, vd, shift, fpst);
        break;
    default:
        g_assert_not_reached();
    }

    vfp_store_reg32(vd, a->vd);
    tcg_temp_free_i32(vd);
    tcg_temp_free_i32(shift);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_fix_sp(DisasContext *s, arg_VCVT_fix_sp *a)
{
    TCGv_i32 vd, shift;
    TCGv_ptr fpst;
    int frac_bits;

    if (!dc_isar_feature(aa32_fpsp_v3, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    frac_bits = (a->opc & 1) ? (32 - a->imm) : (16 - a->imm);

    vd = tcg_temp_new_i32();
    vfp_load_reg32(vd, a->vd);

    fpst = fpstatus_ptr(FPST_FPCR);
    shift = tcg_const_i32(frac_bits);

    /* Switch on op:U:sx bits */
    switch (a->opc) {
    case 0:
        gen_helper_vfp_shtos_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 1:
        gen_helper_vfp_sltos_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 2:
        gen_helper_vfp_uhtos_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 3:
        gen_helper_vfp_ultos_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 4:
        gen_helper_vfp_toshs_round_to_zero(vd, vd, shift, fpst);
        break;
    case 5:
        gen_helper_vfp_tosls_round_to_zero(vd, vd, shift, fpst);
        break;
    case 6:
        gen_helper_vfp_touhs_round_to_zero(vd, vd, shift, fpst);
        break;
    case 7:
        gen_helper_vfp_touls_round_to_zero(vd, vd, shift, fpst);
        break;
    default:
        g_assert_not_reached();
    }

    vfp_store_reg32(vd, a->vd);
    tcg_temp_free_i32(vd);
    tcg_temp_free_i32(shift);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_fix_dp(DisasContext *s, arg_VCVT_fix_dp *a)
{
    TCGv_i64 vd;
    TCGv_i32 shift;
    TCGv_ptr fpst;
    int frac_bits;

    if (!dc_isar_feature(aa32_fpdp_v3, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vd & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    frac_bits = (a->opc & 1) ? (32 - a->imm) : (16 - a->imm);

    vd = tcg_temp_new_i64();
    vfp_load_reg64(vd, a->vd);

    fpst = fpstatus_ptr(FPST_FPCR);
    shift = tcg_const_i32(frac_bits);

    /* Switch on op:U:sx bits */
    switch (a->opc) {
    case 0:
        gen_helper_vfp_shtod_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 1:
        gen_helper_vfp_sltod_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 2:
        gen_helper_vfp_uhtod_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 3:
        gen_helper_vfp_ultod_round_to_nearest(vd, vd, shift, fpst);
        break;
    case 4:
        gen_helper_vfp_toshd_round_to_zero(vd, vd, shift, fpst);
        break;
    case 5:
        gen_helper_vfp_tosld_round_to_zero(vd, vd, shift, fpst);
        break;
    case 6:
        gen_helper_vfp_touhd_round_to_zero(vd, vd, shift, fpst);
        break;
    case 7:
        gen_helper_vfp_tould_round_to_zero(vd, vd, shift, fpst);
        break;
    default:
        g_assert_not_reached();
    }

    vfp_store_reg64(vd, a->vd);
    tcg_temp_free_i64(vd);
    tcg_temp_free_i32(shift);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_hp_int(DisasContext *s, arg_VCVT_sp_int *a)
{
    TCGv_i32 vm;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR_F16);
    vm = tcg_temp_new_i32();
    vfp_load_reg32(vm, a->vm);

    if (a->s) {
        if (a->rz) {
            gen_helper_vfp_tosizh(vm, vm, fpst);
        } else {
            gen_helper_vfp_tosih(vm, vm, fpst);
        }
    } else {
        if (a->rz) {
            gen_helper_vfp_touizh(vm, vm, fpst);
        } else {
            gen_helper_vfp_touih(vm, vm, fpst);
        }
    }
    vfp_store_reg32(vm, a->vd);
    tcg_temp_free_i32(vm);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_sp_int(DisasContext *s, arg_VCVT_sp_int *a)
{
    TCGv_i32 vm;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fpsp_v2, s)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    vm = tcg_temp_new_i32();
    vfp_load_reg32(vm, a->vm);

    if (a->s) {
        if (a->rz) {
            gen_helper_vfp_tosizs(vm, vm, fpst);
        } else {
            gen_helper_vfp_tosis(vm, vm, fpst);
        }
    } else {
        if (a->rz) {
            gen_helper_vfp_touizs(vm, vm, fpst);
        } else {
            gen_helper_vfp_touis(vm, vm, fpst);
        }
    }
    vfp_store_reg32(vm, a->vd);
    tcg_temp_free_i32(vm);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VCVT_dp_int(DisasContext *s, arg_VCVT_dp_int *a)
{
    TCGv_i32 vd;
    TCGv_i64 vm;
    TCGv_ptr fpst;

    if (!dc_isar_feature(aa32_fpdp_v2, s)) {
        return false;
    }

    /* UNDEF accesses to D16-D31 if they don't exist. */
    if (!dc_isar_feature(aa32_simd_r32, s) && (a->vm & 0x10)) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    vm = tcg_temp_new_i64();
    vd = tcg_temp_new_i32();
    vfp_load_reg64(vm, a->vm);

    if (a->s) {
        if (a->rz) {
            gen_helper_vfp_tosizd(vd, vm, fpst);
        } else {
            gen_helper_vfp_tosid(vd, vm, fpst);
        }
    } else {
        if (a->rz) {
            gen_helper_vfp_touizd(vd, vm, fpst);
        } else {
            gen_helper_vfp_touid(vd, vm, fpst);
        }
    }
    vfp_store_reg32(vd, a->vd);
    tcg_temp_free_i32(vd);
    tcg_temp_free_i64(vm);
    tcg_temp_free_ptr(fpst);
    return true;
}

static bool trans_VINS(DisasContext *s, arg_VINS *a)
{
    TCGv_i32 rd, rm;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* Insert low half of Vm into high half of Vd */
    rm = tcg_temp_new_i32();
    rd = tcg_temp_new_i32();
    vfp_load_reg32(rm, a->vm);
    vfp_load_reg32(rd, a->vd);
    tcg_gen_deposit_i32(rd, rd, rm, 16, 16);
    vfp_store_reg32(rd, a->vd);
    tcg_temp_free_i32(rm);
    tcg_temp_free_i32(rd);
    return true;
}

static bool trans_VMOVX(DisasContext *s, arg_VINS *a)
{
    TCGv_i32 rm;

    if (!dc_isar_feature(aa32_fp16_arith, s)) {
        return false;
    }

    if (s->vec_len != 0 || s->vec_stride != 0) {
        return false;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* Set Vd to high half of Vm */
    rm = tcg_temp_new_i32();
    vfp_load_reg32(rm, a->vm);
    tcg_gen_shri_i32(rm, rm, 16);
    vfp_store_reg32(rm, a->vd);
    tcg_temp_free_i32(rm);
    return true;
}
