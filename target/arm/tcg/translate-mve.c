/*
 *  ARM translation: M-profile MVE instructions
 *
 *  Copyright (c) 2021 Linaro, Ltd.
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
#include "translate.h"
#include "translate-a32.h"

static inline int vidup_imm(DisasContext *s, int x)
{
    return 1 << x;
}

/* Include the generated decoder */
#include "decode-mve.c.inc"

typedef void MVEGenLdStFn(TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenLdStSGFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenLdStIlFn(TCGv_ptr, TCGv_i32, TCGv_i32);
typedef void MVEGenOneOpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void MVEGenTwoOpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void MVEGenTwoOpScalarFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenTwoOpShiftFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenLongDualAccOpFn(TCGv_i64, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i64);
typedef void MVEGenVADDVFn(TCGv_i32, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenOneOpImmFn(TCGv_ptr, TCGv_ptr, TCGv_i64);
typedef void MVEGenVIDUPFn(TCGv_i32, TCGv_ptr, TCGv_ptr, TCGv_i32, TCGv_i32);
typedef void MVEGenVIWDUPFn(TCGv_i32, TCGv_ptr, TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32);
typedef void MVEGenCmpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void MVEGenScalarCmpFn(TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenVABAVFn(TCGv_i32, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenDualAccOpFn(TCGv_i32, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenVCVTRmodeFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);

/* Return the offset of a Qn register (same semantics as aa32_vfp_qreg()) */
static inline long mve_qreg_offset(unsigned reg)
{
    return offsetof(CPUARMState, vfp.zregs[reg].d[0]);
}

static TCGv_ptr mve_qreg_ptr(unsigned reg)
{
    TCGv_ptr ret = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ret, cpu_env, mve_qreg_offset(reg));
    return ret;
}

static bool mve_no_predication(DisasContext *s)
{
    /*
     * Return true if we are executing the entire MVE instruction
     * with no predication or partial-execution, and so we can safely
     * use an inline TCG vector implementation.
     */
    return s->eci == 0 && s->mve_no_pred;
}

static bool mve_check_qreg_bank(DisasContext *s, int qmask)
{
    /*
     * Check whether Qregs are in range. For v8.1M only Q0..Q7
     * are supported, see VFPSmallRegisterBank().
     */
    return qmask < 8;
}

bool mve_eci_check(DisasContext *s)
{
    /*
     * This is a beatwise insn: check that ECI is valid (not a
     * reserved value) and note that we are handling it.
     * Return true if OK, false if we generated an exception.
     */
    s->eci_handled = true;
    switch (s->eci) {
    case ECI_NONE:
    case ECI_A0:
    case ECI_A0A1:
    case ECI_A0A1A2:
    case ECI_A0A1A2B0:
        return true;
    default:
        /* Reserved value: INVSTATE UsageFault */
        gen_exception_insn(s, 0, EXCP_INVSTATE, syn_uncategorized());
        return false;
    }
}

void mve_update_eci(DisasContext *s)
{
    /*
     * The helper function will always update the CPUState field,
     * so we only need to update the DisasContext field.
     */
    if (s->eci) {
        s->eci = (s->eci == ECI_A0A1A2B0) ? ECI_A0 : ECI_NONE;
    }
}

void mve_update_and_store_eci(DisasContext *s)
{
    /*
     * For insns which don't call a helper function that will call
     * mve_advance_vpt(), this version updates s->eci and also stores
     * it out to the CPUState field.
     */
    if (s->eci) {
        mve_update_eci(s);
        store_cpu_field(tcg_constant_i32(s->eci << 4), condexec_bits);
    }
}

static bool mve_skip_first_beat(DisasContext *s)
{
    /* Return true if PSR.ECI says we must skip the first beat of this insn */
    switch (s->eci) {
    case ECI_NONE:
        return false;
    case ECI_A0:
    case ECI_A0A1:
    case ECI_A0A1A2:
    case ECI_A0A1A2B0:
        return true;
    default:
        g_assert_not_reached();
    }
}

static bool do_ldst(DisasContext *s, arg_VLDR_VSTR *a, MVEGenLdStFn *fn,
                    unsigned msize)
{
    TCGv_i32 addr;
    uint32_t offset;
    TCGv_ptr qreg;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd) ||
        !fn) {
        return false;
    }

    /* CONSTRAINED UNPREDICTABLE: we choose to UNDEF */
    if (a->rn == 15 || (a->rn == 13 && a->w)) {
        return false;
    }

    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    offset = a->imm << msize;
    if (!a->a) {
        offset = -offset;
    }
    addr = load_reg(s, a->rn);
    if (a->p) {
        tcg_gen_addi_i32(addr, addr, offset);
    }

    qreg = mve_qreg_ptr(a->qd);
    fn(cpu_env, qreg, addr);

    /*
     * Writeback always happens after the last beat of the insn,
     * regardless of predication
     */
    if (a->w) {
        if (!a->p) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    }
    mve_update_eci(s);
    return true;
}

static bool trans_VLDR_VSTR(DisasContext *s, arg_VLDR_VSTR *a)
{
    static MVEGenLdStFn * const ldstfns[4][2] = {
        { gen_helper_mve_vstrb, gen_helper_mve_vldrb },
        { gen_helper_mve_vstrh, gen_helper_mve_vldrh },
        { gen_helper_mve_vstrw, gen_helper_mve_vldrw },
        { NULL, NULL }
    };
    return do_ldst(s, a, ldstfns[a->size][a->l], a->size);
}

#define DO_VLDST_WIDE_NARROW(OP, SLD, ULD, ST, MSIZE)           \
    static bool trans_##OP(DisasContext *s, arg_VLDR_VSTR *a)   \
    {                                                           \
        static MVEGenLdStFn * const ldstfns[2][2] = {           \
            { gen_helper_mve_##ST, gen_helper_mve_##SLD },      \
            { NULL, gen_helper_mve_##ULD },                     \
        };                                                      \
        return do_ldst(s, a, ldstfns[a->u][a->l], MSIZE);       \
    }

DO_VLDST_WIDE_NARROW(VLDSTB_H, vldrb_sh, vldrb_uh, vstrb_h, MO_8)
DO_VLDST_WIDE_NARROW(VLDSTB_W, vldrb_sw, vldrb_uw, vstrb_w, MO_8)
DO_VLDST_WIDE_NARROW(VLDSTH_W, vldrh_sw, vldrh_uw, vstrh_w, MO_16)

static bool do_ldst_sg(DisasContext *s, arg_vldst_sg *a, MVEGenLdStSGFn fn)
{
    TCGv_i32 addr;
    TCGv_ptr qd, qm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qm) ||
        !fn || a->rn == 15) {
        /* Rn case is UNPREDICTABLE */
        return false;
    }

    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    addr = load_reg(s, a->rn);

    qd = mve_qreg_ptr(a->qd);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qd, qm, addr);
    mve_update_eci(s);
    return true;
}

/*
 * The naming scheme here is "vldrb_sg_sh == in-memory byte loads
 * signextended to halfword elements in register". _os_ indicates that
 * the offsets in Qm should be scaled by the element size.
 */
/* This macro is just to make the arrays more compact in these functions */
#define F(N) gen_helper_mve_##N

/* VLDRB/VSTRB (ie msize 1) with OS=1 is UNPREDICTABLE; we UNDEF */
static bool trans_VLDR_S_sg(DisasContext *s, arg_vldst_sg *a)
{
    static MVEGenLdStSGFn * const fns[2][4][4] = { {
            { NULL, F(vldrb_sg_sh), F(vldrb_sg_sw), NULL },
            { NULL, NULL,           F(vldrh_sg_sw), NULL },
            { NULL, NULL,           NULL,           NULL },
            { NULL, NULL,           NULL,           NULL }
        }, {
            { NULL, NULL,              NULL,              NULL },
            { NULL, NULL,              F(vldrh_sg_os_sw), NULL },
            { NULL, NULL,              NULL,              NULL },
            { NULL, NULL,              NULL,              NULL }
        }
    };
    if (a->qd == a->qm) {
        return false; /* UNPREDICTABLE */
    }
    return do_ldst_sg(s, a, fns[a->os][a->msize][a->size]);
}

static bool trans_VLDR_U_sg(DisasContext *s, arg_vldst_sg *a)
{
    static MVEGenLdStSGFn * const fns[2][4][4] = { {
            { F(vldrb_sg_ub), F(vldrb_sg_uh), F(vldrb_sg_uw), NULL },
            { NULL,           F(vldrh_sg_uh), F(vldrh_sg_uw), NULL },
            { NULL,           NULL,           F(vldrw_sg_uw), NULL },
            { NULL,           NULL,           NULL,           F(vldrd_sg_ud) }
        }, {
            { NULL, NULL,              NULL,              NULL },
            { NULL, F(vldrh_sg_os_uh), F(vldrh_sg_os_uw), NULL },
            { NULL, NULL,              F(vldrw_sg_os_uw), NULL },
            { NULL, NULL,              NULL,              F(vldrd_sg_os_ud) }
        }
    };
    if (a->qd == a->qm) {
        return false; /* UNPREDICTABLE */
    }
    return do_ldst_sg(s, a, fns[a->os][a->msize][a->size]);
}

static bool trans_VSTR_sg(DisasContext *s, arg_vldst_sg *a)
{
    static MVEGenLdStSGFn * const fns[2][4][4] = { {
            { F(vstrb_sg_ub), F(vstrb_sg_uh), F(vstrb_sg_uw), NULL },
            { NULL,           F(vstrh_sg_uh), F(vstrh_sg_uw), NULL },
            { NULL,           NULL,           F(vstrw_sg_uw), NULL },
            { NULL,           NULL,           NULL,           F(vstrd_sg_ud) }
        }, {
            { NULL, NULL,              NULL,              NULL },
            { NULL, F(vstrh_sg_os_uh), F(vstrh_sg_os_uw), NULL },
            { NULL, NULL,              F(vstrw_sg_os_uw), NULL },
            { NULL, NULL,              NULL,              F(vstrd_sg_os_ud) }
        }
    };
    return do_ldst_sg(s, a, fns[a->os][a->msize][a->size]);
}

#undef F

static bool do_ldst_sg_imm(DisasContext *s, arg_vldst_sg_imm *a,
                           MVEGenLdStSGFn *fn, unsigned msize)
{
    uint32_t offset;
    TCGv_ptr qd, qm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qm) ||
        !fn) {
        return false;
    }

    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    offset = a->imm << msize;
    if (!a->a) {
        offset = -offset;
    }

    qd = mve_qreg_ptr(a->qd);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qd, qm, tcg_constant_i32(offset));
    mve_update_eci(s);
    return true;
}

static bool trans_VLDRW_sg_imm(DisasContext *s, arg_vldst_sg_imm *a)
{
    static MVEGenLdStSGFn * const fns[] = {
        gen_helper_mve_vldrw_sg_uw,
        gen_helper_mve_vldrw_sg_wb_uw,
    };
    if (a->qd == a->qm) {
        return false; /* UNPREDICTABLE */
    }
    return do_ldst_sg_imm(s, a, fns[a->w], MO_32);
}

static bool trans_VLDRD_sg_imm(DisasContext *s, arg_vldst_sg_imm *a)
{
    static MVEGenLdStSGFn * const fns[] = {
        gen_helper_mve_vldrd_sg_ud,
        gen_helper_mve_vldrd_sg_wb_ud,
    };
    if (a->qd == a->qm) {
        return false; /* UNPREDICTABLE */
    }
    return do_ldst_sg_imm(s, a, fns[a->w], MO_64);
}

static bool trans_VSTRW_sg_imm(DisasContext *s, arg_vldst_sg_imm *a)
{
    static MVEGenLdStSGFn * const fns[] = {
        gen_helper_mve_vstrw_sg_uw,
        gen_helper_mve_vstrw_sg_wb_uw,
    };
    return do_ldst_sg_imm(s, a, fns[a->w], MO_32);
}

static bool trans_VSTRD_sg_imm(DisasContext *s, arg_vldst_sg_imm *a)
{
    static MVEGenLdStSGFn * const fns[] = {
        gen_helper_mve_vstrd_sg_ud,
        gen_helper_mve_vstrd_sg_wb_ud,
    };
    return do_ldst_sg_imm(s, a, fns[a->w], MO_64);
}

static bool do_vldst_il(DisasContext *s, arg_vldst_il *a, MVEGenLdStIlFn *fn,
                        int addrinc)
{
    TCGv_i32 rn;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd) ||
        !fn || (a->rn == 13 && a->w) || a->rn == 15) {
        /* Variously UNPREDICTABLE or UNDEF or related-encoding */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    rn = load_reg(s, a->rn);
    /*
     * We pass the index of Qd, not a pointer, because the helper must
     * access multiple Q registers starting at Qd and working up.
     */
    fn(cpu_env, tcg_constant_i32(a->qd), rn);

    if (a->w) {
        tcg_gen_addi_i32(rn, rn, addrinc);
        store_reg(s, a->rn, rn);
    }
    mve_update_and_store_eci(s);
    return true;
}

/* This macro is just to make the arrays more compact in these functions */
#define F(N) gen_helper_mve_##N

static bool trans_VLD2(DisasContext *s, arg_vldst_il *a)
{
    static MVEGenLdStIlFn * const fns[4][4] = {
        { F(vld20b), F(vld20h), F(vld20w), NULL, },
        { F(vld21b), F(vld21h), F(vld21w), NULL, },
        { NULL, NULL, NULL, NULL },
        { NULL, NULL, NULL, NULL },
    };
    if (a->qd > 6) {
        return false;
    }
    return do_vldst_il(s, a, fns[a->pat][a->size], 32);
}

static bool trans_VLD4(DisasContext *s, arg_vldst_il *a)
{
    static MVEGenLdStIlFn * const fns[4][4] = {
        { F(vld40b), F(vld40h), F(vld40w), NULL, },
        { F(vld41b), F(vld41h), F(vld41w), NULL, },
        { F(vld42b), F(vld42h), F(vld42w), NULL, },
        { F(vld43b), F(vld43h), F(vld43w), NULL, },
    };
    if (a->qd > 4) {
        return false;
    }
    return do_vldst_il(s, a, fns[a->pat][a->size], 64);
}

static bool trans_VST2(DisasContext *s, arg_vldst_il *a)
{
    static MVEGenLdStIlFn * const fns[4][4] = {
        { F(vst20b), F(vst20h), F(vst20w), NULL, },
        { F(vst21b), F(vst21h), F(vst21w), NULL, },
        { NULL, NULL, NULL, NULL },
        { NULL, NULL, NULL, NULL },
    };
    if (a->qd > 6) {
        return false;
    }
    return do_vldst_il(s, a, fns[a->pat][a->size], 32);
}

static bool trans_VST4(DisasContext *s, arg_vldst_il *a)
{
    static MVEGenLdStIlFn * const fns[4][4] = {
        { F(vst40b), F(vst40h), F(vst40w), NULL, },
        { F(vst41b), F(vst41h), F(vst41w), NULL, },
        { F(vst42b), F(vst42h), F(vst42w), NULL, },
        { F(vst43b), F(vst43h), F(vst43w), NULL, },
    };
    if (a->qd > 4) {
        return false;
    }
    return do_vldst_il(s, a, fns[a->pat][a->size], 64);
}

#undef F

static bool trans_VDUP(DisasContext *s, arg_VDUP *a)
{
    TCGv_ptr qd;
    TCGv_i32 rt;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd)) {
        return false;
    }
    if (a->rt == 13 || a->rt == 15) {
        /* UNPREDICTABLE; we choose to UNDEF */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    rt = load_reg(s, a->rt);
    if (mve_no_predication(s)) {
        tcg_gen_gvec_dup_i32(a->size, mve_qreg_offset(a->qd), 16, 16, rt);
    } else {
        qd = mve_qreg_ptr(a->qd);
        tcg_gen_dup_i32(a->size, rt, rt);
        gen_helper_mve_vdup(cpu_env, qd, rt);
    }
    mve_update_eci(s);
    return true;
}

static bool do_1op_vec(DisasContext *s, arg_1op *a, MVEGenOneOpFn fn,
                       GVecGen2Fn vecfn)
{
    TCGv_ptr qd, qm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qm) ||
        !fn) {
        return false;
    }

    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    if (vecfn && mve_no_predication(s)) {
        vecfn(a->size, mve_qreg_offset(a->qd), mve_qreg_offset(a->qm), 16, 16);
    } else {
        qd = mve_qreg_ptr(a->qd);
        qm = mve_qreg_ptr(a->qm);
        fn(cpu_env, qd, qm);
    }
    mve_update_eci(s);
    return true;
}

static bool do_1op(DisasContext *s, arg_1op *a, MVEGenOneOpFn fn)
{
    return do_1op_vec(s, a, fn, NULL);
}

#define DO_1OP_VEC(INSN, FN, VECFN)                             \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)       \
    {                                                           \
        static MVEGenOneOpFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_1op_vec(s, a, fns[a->size], VECFN);           \
    }

#define DO_1OP(INSN, FN) DO_1OP_VEC(INSN, FN, NULL)

DO_1OP(VCLZ, vclz)
DO_1OP(VCLS, vcls)
DO_1OP_VEC(VABS, vabs, tcg_gen_gvec_abs)
DO_1OP_VEC(VNEG, vneg, tcg_gen_gvec_neg)
DO_1OP(VQABS, vqabs)
DO_1OP(VQNEG, vqneg)
DO_1OP(VMAXA, vmaxa)
DO_1OP(VMINA, vmina)

/*
 * For simple float/int conversions we use the fixed-point
 * conversion helpers with a zero shift count
 */
#define DO_VCVT(INSN, HFN, SFN)                                         \
    static void gen_##INSN##h(TCGv_ptr env, TCGv_ptr qd, TCGv_ptr qm)   \
    {                                                                   \
        gen_helper_mve_##HFN(env, qd, qm, tcg_constant_i32(0));         \
    }                                                                   \
    static void gen_##INSN##s(TCGv_ptr env, TCGv_ptr qd, TCGv_ptr qm)   \
    {                                                                   \
        gen_helper_mve_##SFN(env, qd, qm, tcg_constant_i32(0));         \
    }                                                                   \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)               \
    {                                                                   \
        static MVEGenOneOpFn * const fns[] = {                          \
            NULL,                                                       \
            gen_##INSN##h,                                              \
            gen_##INSN##s,                                              \
            NULL,                                                       \
        };                                                              \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                         \
            return false;                                               \
        }                                                               \
        return do_1op(s, a, fns[a->size]);                              \
    }

DO_VCVT(VCVT_SF, vcvt_sh, vcvt_sf)
DO_VCVT(VCVT_UF, vcvt_uh, vcvt_uf)
DO_VCVT(VCVT_FS, vcvt_hs, vcvt_fs)
DO_VCVT(VCVT_FU, vcvt_hu, vcvt_fu)

static bool do_vcvt_rmode(DisasContext *s, arg_1op *a,
                          ARMFPRounding rmode, bool u)
{
    /*
     * Handle VCVT fp to int with specified rounding mode.
     * This is a 1op fn but we must pass the rounding mode as
     * an immediate to the helper.
     */
    TCGv_ptr qd, qm;
    static MVEGenVCVTRmodeFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vcvt_rm_sh, gen_helper_mve_vcvt_rm_uh },
        { gen_helper_mve_vcvt_rm_ss, gen_helper_mve_vcvt_rm_us },
        { NULL, NULL },
    };
    MVEGenVCVTRmodeFn *fn = fns[a->size][u];

    if (!dc_isar_feature(aa32_mve_fp, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qm) ||
        !fn) {
        return false;
    }

    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qd = mve_qreg_ptr(a->qd);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qd, qm, tcg_constant_i32(arm_rmode_to_sf(rmode)));
    mve_update_eci(s);
    return true;
}

#define DO_VCVT_RMODE(INSN, RMODE, U)                           \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)       \
    {                                                           \
        return do_vcvt_rmode(s, a, RMODE, U);                   \
    }                                                           \

DO_VCVT_RMODE(VCVTAS, FPROUNDING_TIEAWAY, false)
DO_VCVT_RMODE(VCVTAU, FPROUNDING_TIEAWAY, true)
DO_VCVT_RMODE(VCVTNS, FPROUNDING_TIEEVEN, false)
DO_VCVT_RMODE(VCVTNU, FPROUNDING_TIEEVEN, true)
DO_VCVT_RMODE(VCVTPS, FPROUNDING_POSINF, false)
DO_VCVT_RMODE(VCVTPU, FPROUNDING_POSINF, true)
DO_VCVT_RMODE(VCVTMS, FPROUNDING_NEGINF, false)
DO_VCVT_RMODE(VCVTMU, FPROUNDING_NEGINF, true)

#define DO_VCVT_SH(INSN, FN)                                    \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)       \
    {                                                           \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_1op(s, a, gen_helper_mve_##FN);               \
    }                                                           \

DO_VCVT_SH(VCVTB_SH, vcvtb_sh)
DO_VCVT_SH(VCVTT_SH, vcvtt_sh)
DO_VCVT_SH(VCVTB_HS, vcvtb_hs)
DO_VCVT_SH(VCVTT_HS, vcvtt_hs)

#define DO_VRINT(INSN, RMODE)                                           \
    static void gen_##INSN##h(TCGv_ptr env, TCGv_ptr qd, TCGv_ptr qm)   \
    {                                                                   \
        gen_helper_mve_vrint_rm_h(env, qd, qm,                          \
                                  tcg_constant_i32(arm_rmode_to_sf(RMODE))); \
    }                                                                   \
    static void gen_##INSN##s(TCGv_ptr env, TCGv_ptr qd, TCGv_ptr qm)   \
    {                                                                   \
        gen_helper_mve_vrint_rm_s(env, qd, qm,                          \
                                  tcg_constant_i32(arm_rmode_to_sf(RMODE))); \
    }                                                                   \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)               \
    {                                                                   \
        static MVEGenOneOpFn * const fns[] = {                          \
            NULL,                                                       \
            gen_##INSN##h,                                              \
            gen_##INSN##s,                                              \
            NULL,                                                       \
        };                                                              \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                         \
            return false;                                               \
        }                                                               \
        return do_1op(s, a, fns[a->size]);                              \
    }

DO_VRINT(VRINTN, FPROUNDING_TIEEVEN)
DO_VRINT(VRINTA, FPROUNDING_TIEAWAY)
DO_VRINT(VRINTZ, FPROUNDING_ZERO)
DO_VRINT(VRINTM, FPROUNDING_NEGINF)
DO_VRINT(VRINTP, FPROUNDING_POSINF)

static bool trans_VRINTX(DisasContext *s, arg_1op *a)
{
    static MVEGenOneOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vrintx_h,
        gen_helper_mve_vrintx_s,
        NULL,
    };
    if (!dc_isar_feature(aa32_mve_fp, s)) {
        return false;
    }
    return do_1op(s, a, fns[a->size]);
}

/* Narrowing moves: only size 0 and 1 are valid */
#define DO_VMOVN(INSN, FN) \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)       \
    {                                                           \
        static MVEGenOneOpFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            NULL,                                               \
            NULL,                                               \
        };                                                      \
        return do_1op(s, a, fns[a->size]);                      \
    }

DO_VMOVN(VMOVNB, vmovnb)
DO_VMOVN(VMOVNT, vmovnt)
DO_VMOVN(VQMOVUNB, vqmovunb)
DO_VMOVN(VQMOVUNT, vqmovunt)
DO_VMOVN(VQMOVN_BS, vqmovnbs)
DO_VMOVN(VQMOVN_TS, vqmovnts)
DO_VMOVN(VQMOVN_BU, vqmovnbu)
DO_VMOVN(VQMOVN_TU, vqmovntu)

static bool trans_VREV16(DisasContext *s, arg_1op *a)
{
    static MVEGenOneOpFn * const fns[] = {
        gen_helper_mve_vrev16b,
        NULL,
        NULL,
        NULL,
    };
    return do_1op(s, a, fns[a->size]);
}

static bool trans_VREV32(DisasContext *s, arg_1op *a)
{
    static MVEGenOneOpFn * const fns[] = {
        gen_helper_mve_vrev32b,
        gen_helper_mve_vrev32h,
        NULL,
        NULL,
    };
    return do_1op(s, a, fns[a->size]);
}

static bool trans_VREV64(DisasContext *s, arg_1op *a)
{
    static MVEGenOneOpFn * const fns[] = {
        gen_helper_mve_vrev64b,
        gen_helper_mve_vrev64h,
        gen_helper_mve_vrev64w,
        NULL,
    };
    return do_1op(s, a, fns[a->size]);
}

static bool trans_VMVN(DisasContext *s, arg_1op *a)
{
    return do_1op_vec(s, a, gen_helper_mve_vmvn, tcg_gen_gvec_not);
}

static bool trans_VABS_fp(DisasContext *s, arg_1op *a)
{
    static MVEGenOneOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vfabsh,
        gen_helper_mve_vfabss,
        NULL,
    };
    if (!dc_isar_feature(aa32_mve_fp, s)) {
        return false;
    }
    return do_1op(s, a, fns[a->size]);
}

static bool trans_VNEG_fp(DisasContext *s, arg_1op *a)
{
    static MVEGenOneOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vfnegh,
        gen_helper_mve_vfnegs,
        NULL,
    };
    if (!dc_isar_feature(aa32_mve_fp, s)) {
        return false;
    }
    return do_1op(s, a, fns[a->size]);
}

static bool do_2op_vec(DisasContext *s, arg_2op *a, MVEGenTwoOpFn fn,
                       GVecGen3Fn *vecfn)
{
    TCGv_ptr qd, qn, qm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qn | a->qm) ||
        !fn) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    if (vecfn && mve_no_predication(s)) {
        vecfn(a->size, mve_qreg_offset(a->qd), mve_qreg_offset(a->qn),
              mve_qreg_offset(a->qm), 16, 16);
    } else {
        qd = mve_qreg_ptr(a->qd);
        qn = mve_qreg_ptr(a->qn);
        qm = mve_qreg_ptr(a->qm);
        fn(cpu_env, qd, qn, qm);
    }
    mve_update_eci(s);
    return true;
}

static bool do_2op(DisasContext *s, arg_2op *a, MVEGenTwoOpFn *fn)
{
    return do_2op_vec(s, a, fn, NULL);
}

#define DO_LOGIC(INSN, HELPER, VECFN)                           \
    static bool trans_##INSN(DisasContext *s, arg_2op *a)       \
    {                                                           \
        return do_2op_vec(s, a, HELPER, VECFN);                 \
    }

DO_LOGIC(VAND, gen_helper_mve_vand, tcg_gen_gvec_and)
DO_LOGIC(VBIC, gen_helper_mve_vbic, tcg_gen_gvec_andc)
DO_LOGIC(VORR, gen_helper_mve_vorr, tcg_gen_gvec_or)
DO_LOGIC(VORN, gen_helper_mve_vorn, tcg_gen_gvec_orc)
DO_LOGIC(VEOR, gen_helper_mve_veor, tcg_gen_gvec_xor)

static bool trans_VPSEL(DisasContext *s, arg_2op *a)
{
    /* This insn updates predication bits */
    s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
    return do_2op(s, a, gen_helper_mve_vpsel);
}

#define DO_2OP_VEC(INSN, FN, VECFN)                             \
    static bool trans_##INSN(DisasContext *s, arg_2op *a)       \
    {                                                           \
        static MVEGenTwoOpFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_2op_vec(s, a, fns[a->size], VECFN);           \
    }

#define DO_2OP(INSN, FN) DO_2OP_VEC(INSN, FN, NULL)

DO_2OP_VEC(VADD, vadd, tcg_gen_gvec_add)
DO_2OP_VEC(VSUB, vsub, tcg_gen_gvec_sub)
DO_2OP_VEC(VMUL, vmul, tcg_gen_gvec_mul)
DO_2OP(VMULH_S, vmulhs)
DO_2OP(VMULH_U, vmulhu)
DO_2OP(VRMULH_S, vrmulhs)
DO_2OP(VRMULH_U, vrmulhu)
DO_2OP_VEC(VMAX_S, vmaxs, tcg_gen_gvec_smax)
DO_2OP_VEC(VMAX_U, vmaxu, tcg_gen_gvec_umax)
DO_2OP_VEC(VMIN_S, vmins, tcg_gen_gvec_smin)
DO_2OP_VEC(VMIN_U, vminu, tcg_gen_gvec_umin)
DO_2OP(VABD_S, vabds)
DO_2OP(VABD_U, vabdu)
DO_2OP(VHADD_S, vhadds)
DO_2OP(VHADD_U, vhaddu)
DO_2OP(VHSUB_S, vhsubs)
DO_2OP(VHSUB_U, vhsubu)
DO_2OP(VMULL_BS, vmullbs)
DO_2OP(VMULL_BU, vmullbu)
DO_2OP(VMULL_TS, vmullts)
DO_2OP(VMULL_TU, vmulltu)
DO_2OP(VQDMULH, vqdmulh)
DO_2OP(VQRDMULH, vqrdmulh)
DO_2OP(VQADD_S, vqadds)
DO_2OP(VQADD_U, vqaddu)
DO_2OP(VQSUB_S, vqsubs)
DO_2OP(VQSUB_U, vqsubu)
DO_2OP(VSHL_S, vshls)
DO_2OP(VSHL_U, vshlu)
DO_2OP(VRSHL_S, vrshls)
DO_2OP(VRSHL_U, vrshlu)
DO_2OP(VQSHL_S, vqshls)
DO_2OP(VQSHL_U, vqshlu)
DO_2OP(VQRSHL_S, vqrshls)
DO_2OP(VQRSHL_U, vqrshlu)
DO_2OP(VQDMLADH, vqdmladh)
DO_2OP(VQDMLADHX, vqdmladhx)
DO_2OP(VQRDMLADH, vqrdmladh)
DO_2OP(VQRDMLADHX, vqrdmladhx)
DO_2OP(VQDMLSDH, vqdmlsdh)
DO_2OP(VQDMLSDHX, vqdmlsdhx)
DO_2OP(VQRDMLSDH, vqrdmlsdh)
DO_2OP(VQRDMLSDHX, vqrdmlsdhx)
DO_2OP(VRHADD_S, vrhadds)
DO_2OP(VRHADD_U, vrhaddu)
/*
 * VCADD Qd == Qm at size MO_32 is UNPREDICTABLE; we choose not to diagnose
 * so we can reuse the DO_2OP macro. (Our implementation calculates the
 * "expected" results in this case.) Similarly for VHCADD.
 */
DO_2OP(VCADD90, vcadd90)
DO_2OP(VCADD270, vcadd270)
DO_2OP(VHCADD90, vhcadd90)
DO_2OP(VHCADD270, vhcadd270)

static bool trans_VQDMULLB(DisasContext *s, arg_2op *a)
{
    static MVEGenTwoOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vqdmullbh,
        gen_helper_mve_vqdmullbw,
        NULL,
    };
    if (a->size == MO_32 && (a->qd == a->qm || a->qd == a->qn)) {
        /* UNPREDICTABLE; we choose to undef */
        return false;
    }
    return do_2op(s, a, fns[a->size]);
}

static bool trans_VQDMULLT(DisasContext *s, arg_2op *a)
{
    static MVEGenTwoOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vqdmullth,
        gen_helper_mve_vqdmulltw,
        NULL,
    };
    if (a->size == MO_32 && (a->qd == a->qm || a->qd == a->qn)) {
        /* UNPREDICTABLE; we choose to undef */
        return false;
    }
    return do_2op(s, a, fns[a->size]);
}

static bool trans_VMULLP_B(DisasContext *s, arg_2op *a)
{
    /*
     * Note that a->size indicates the output size, ie VMULL.P8
     * is the 8x8->16 operation and a->size is MO_16; VMULL.P16
     * is the 16x16->32 operation and a->size is MO_32.
     */
    static MVEGenTwoOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vmullpbh,
        gen_helper_mve_vmullpbw,
        NULL,
    };
    return do_2op(s, a, fns[a->size]);
}

static bool trans_VMULLP_T(DisasContext *s, arg_2op *a)
{
    /* a->size is as for trans_VMULLP_B */
    static MVEGenTwoOpFn * const fns[] = {
        NULL,
        gen_helper_mve_vmullpth,
        gen_helper_mve_vmullptw,
        NULL,
    };
    return do_2op(s, a, fns[a->size]);
}

/*
 * VADC and VSBC: these perform an add-with-carry or subtract-with-carry
 * of the 32-bit elements in each lane of the input vectors, where the
 * carry-out of each add is the carry-in of the next.  The initial carry
 * input is either fixed (0 for VADCI, 1 for VSBCI) or is from FPSCR.C
 * (for VADC and VSBC); the carry out at the end is written back to FPSCR.C.
 * These insns are subject to beat-wise execution.  Partial execution
 * of an I=1 (initial carry input fixed) insn which does not
 * execute the first beat must start with the current FPSCR.NZCV
 * value, not the fixed constant input.
 */
static bool trans_VADC(DisasContext *s, arg_2op *a)
{
    return do_2op(s, a, gen_helper_mve_vadc);
}

static bool trans_VADCI(DisasContext *s, arg_2op *a)
{
    if (mve_skip_first_beat(s)) {
        return trans_VADC(s, a);
    }
    return do_2op(s, a, gen_helper_mve_vadci);
}

static bool trans_VSBC(DisasContext *s, arg_2op *a)
{
    return do_2op(s, a, gen_helper_mve_vsbc);
}

static bool trans_VSBCI(DisasContext *s, arg_2op *a)
{
    if (mve_skip_first_beat(s)) {
        return trans_VSBC(s, a);
    }
    return do_2op(s, a, gen_helper_mve_vsbci);
}

#define DO_2OP_FP(INSN, FN)                                     \
    static bool trans_##INSN(DisasContext *s, arg_2op *a)       \
    {                                                           \
        static MVEGenTwoOpFn * const fns[] = {                  \
            NULL,                                               \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##s,                             \
            NULL,                                               \
        };                                                      \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_2op(s, a, fns[a->size]);                      \
    }

DO_2OP_FP(VADD_fp, vfadd)
DO_2OP_FP(VSUB_fp, vfsub)
DO_2OP_FP(VMUL_fp, vfmul)
DO_2OP_FP(VABD_fp, vfabd)
DO_2OP_FP(VMAXNM, vmaxnm)
DO_2OP_FP(VMINNM, vminnm)
DO_2OP_FP(VCADD90_fp, vfcadd90)
DO_2OP_FP(VCADD270_fp, vfcadd270)
DO_2OP_FP(VFMA, vfma)
DO_2OP_FP(VFMS, vfms)
DO_2OP_FP(VCMUL0, vcmul0)
DO_2OP_FP(VCMUL90, vcmul90)
DO_2OP_FP(VCMUL180, vcmul180)
DO_2OP_FP(VCMUL270, vcmul270)
DO_2OP_FP(VCMLA0, vcmla0)
DO_2OP_FP(VCMLA90, vcmla90)
DO_2OP_FP(VCMLA180, vcmla180)
DO_2OP_FP(VCMLA270, vcmla270)
DO_2OP_FP(VMAXNMA, vmaxnma)
DO_2OP_FP(VMINNMA, vminnma)

static bool do_2op_scalar(DisasContext *s, arg_2scalar *a,
                          MVEGenTwoOpScalarFn fn)
{
    TCGv_ptr qd, qn;
    TCGv_i32 rm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qn) ||
        !fn) {
        return false;
    }
    if (a->rm == 13 || a->rm == 15) {
        /* UNPREDICTABLE */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qd = mve_qreg_ptr(a->qd);
    qn = mve_qreg_ptr(a->qn);
    rm = load_reg(s, a->rm);
    fn(cpu_env, qd, qn, rm);
    mve_update_eci(s);
    return true;
}

#define DO_2OP_SCALAR(INSN, FN)                                 \
    static bool trans_##INSN(DisasContext *s, arg_2scalar *a)   \
    {                                                           \
        static MVEGenTwoOpScalarFn * const fns[] = {            \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_2op_scalar(s, a, fns[a->size]);               \
    }

DO_2OP_SCALAR(VADD_scalar, vadd_scalar)
DO_2OP_SCALAR(VSUB_scalar, vsub_scalar)
DO_2OP_SCALAR(VMUL_scalar, vmul_scalar)
DO_2OP_SCALAR(VHADD_S_scalar, vhadds_scalar)
DO_2OP_SCALAR(VHADD_U_scalar, vhaddu_scalar)
DO_2OP_SCALAR(VHSUB_S_scalar, vhsubs_scalar)
DO_2OP_SCALAR(VHSUB_U_scalar, vhsubu_scalar)
DO_2OP_SCALAR(VQADD_S_scalar, vqadds_scalar)
DO_2OP_SCALAR(VQADD_U_scalar, vqaddu_scalar)
DO_2OP_SCALAR(VQSUB_S_scalar, vqsubs_scalar)
DO_2OP_SCALAR(VQSUB_U_scalar, vqsubu_scalar)
DO_2OP_SCALAR(VQDMULH_scalar, vqdmulh_scalar)
DO_2OP_SCALAR(VQRDMULH_scalar, vqrdmulh_scalar)
DO_2OP_SCALAR(VBRSR, vbrsr)
DO_2OP_SCALAR(VMLA, vmla)
DO_2OP_SCALAR(VMLAS, vmlas)
DO_2OP_SCALAR(VQDMLAH, vqdmlah)
DO_2OP_SCALAR(VQRDMLAH, vqrdmlah)
DO_2OP_SCALAR(VQDMLASH, vqdmlash)
DO_2OP_SCALAR(VQRDMLASH, vqrdmlash)

static bool trans_VQDMULLB_scalar(DisasContext *s, arg_2scalar *a)
{
    static MVEGenTwoOpScalarFn * const fns[] = {
        NULL,
        gen_helper_mve_vqdmullb_scalarh,
        gen_helper_mve_vqdmullb_scalarw,
        NULL,
    };
    if (a->qd == a->qn && a->size == MO_32) {
        /* UNPREDICTABLE; we choose to undef */
        return false;
    }
    return do_2op_scalar(s, a, fns[a->size]);
}

static bool trans_VQDMULLT_scalar(DisasContext *s, arg_2scalar *a)
{
    static MVEGenTwoOpScalarFn * const fns[] = {
        NULL,
        gen_helper_mve_vqdmullt_scalarh,
        gen_helper_mve_vqdmullt_scalarw,
        NULL,
    };
    if (a->qd == a->qn && a->size == MO_32) {
        /* UNPREDICTABLE; we choose to undef */
        return false;
    }
    return do_2op_scalar(s, a, fns[a->size]);
}


#define DO_2OP_FP_SCALAR(INSN, FN)                              \
    static bool trans_##INSN(DisasContext *s, arg_2scalar *a)   \
    {                                                           \
        static MVEGenTwoOpScalarFn * const fns[] = {            \
            NULL,                                               \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##s,                             \
            NULL,                                               \
        };                                                      \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_2op_scalar(s, a, fns[a->size]);               \
    }

DO_2OP_FP_SCALAR(VADD_fp_scalar, vfadd_scalar)
DO_2OP_FP_SCALAR(VSUB_fp_scalar, vfsub_scalar)
DO_2OP_FP_SCALAR(VMUL_fp_scalar, vfmul_scalar)
DO_2OP_FP_SCALAR(VFMA_scalar, vfma_scalar)
DO_2OP_FP_SCALAR(VFMAS_scalar, vfmas_scalar)

static bool do_long_dual_acc(DisasContext *s, arg_vmlaldav *a,
                             MVEGenLongDualAccOpFn *fn)
{
    TCGv_ptr qn, qm;
    TCGv_i64 rda_i, rda_o;
    TCGv_i32 rdalo, rdahi;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qn | a->qm) ||
        !fn) {
        return false;
    }
    /*
     * rdahi == 13 is UNPREDICTABLE; rdahi == 15 is a related
     * encoding; rdalo always has bit 0 clear so cannot be 13 or 15.
     */
    if (a->rdahi == 13 || a->rdahi == 15) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qn = mve_qreg_ptr(a->qn);
    qm = mve_qreg_ptr(a->qm);

    /*
     * This insn is subject to beat-wise execution. Partial execution
     * of an A=0 (no-accumulate) insn which does not execute the first
     * beat must start with the current rda value, not 0.
     */
    rda_o = tcg_temp_new_i64();
    if (a->a || mve_skip_first_beat(s)) {
        rda_i = rda_o;
        rdalo = load_reg(s, a->rdalo);
        rdahi = load_reg(s, a->rdahi);
        tcg_gen_concat_i32_i64(rda_i, rdalo, rdahi);
    } else {
        rda_i = tcg_constant_i64(0);
    }

    fn(rda_o, cpu_env, qn, qm, rda_i);

    rdalo = tcg_temp_new_i32();
    rdahi = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(rdalo, rda_o);
    tcg_gen_extrh_i64_i32(rdahi, rda_o);
    store_reg(s, a->rdalo, rdalo);
    store_reg(s, a->rdahi, rdahi);
    mve_update_eci(s);
    return true;
}

static bool trans_VMLALDAV_S(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenLongDualAccOpFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vmlaldavsh, gen_helper_mve_vmlaldavxsh },
        { gen_helper_mve_vmlaldavsw, gen_helper_mve_vmlaldavxsw },
        { NULL, NULL },
    };
    return do_long_dual_acc(s, a, fns[a->size][a->x]);
}

static bool trans_VMLALDAV_U(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenLongDualAccOpFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vmlaldavuh, NULL },
        { gen_helper_mve_vmlaldavuw, NULL },
        { NULL, NULL },
    };
    return do_long_dual_acc(s, a, fns[a->size][a->x]);
}

static bool trans_VMLSLDAV(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenLongDualAccOpFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vmlsldavsh, gen_helper_mve_vmlsldavxsh },
        { gen_helper_mve_vmlsldavsw, gen_helper_mve_vmlsldavxsw },
        { NULL, NULL },
    };
    return do_long_dual_acc(s, a, fns[a->size][a->x]);
}

static bool trans_VRMLALDAVH_S(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenLongDualAccOpFn * const fns[] = {
        gen_helper_mve_vrmlaldavhsw, gen_helper_mve_vrmlaldavhxsw,
    };
    return do_long_dual_acc(s, a, fns[a->x]);
}

static bool trans_VRMLALDAVH_U(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenLongDualAccOpFn * const fns[] = {
        gen_helper_mve_vrmlaldavhuw, NULL,
    };
    return do_long_dual_acc(s, a, fns[a->x]);
}

static bool trans_VRMLSLDAVH(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenLongDualAccOpFn * const fns[] = {
        gen_helper_mve_vrmlsldavhsw, gen_helper_mve_vrmlsldavhxsw,
    };
    return do_long_dual_acc(s, a, fns[a->x]);
}

static bool do_dual_acc(DisasContext *s, arg_vmladav *a, MVEGenDualAccOpFn *fn)
{
    TCGv_ptr qn, qm;
    TCGv_i32 rda_i, rda_o;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qn) ||
        !fn) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qn = mve_qreg_ptr(a->qn);
    qm = mve_qreg_ptr(a->qm);

    /*
     * This insn is subject to beat-wise execution. Partial execution
     * of an A=0 (no-accumulate) insn which does not execute the first
     * beat must start with the current rda value, not 0.
     */
    if (a->a || mve_skip_first_beat(s)) {
        rda_o = rda_i = load_reg(s, a->rda);
    } else {
        rda_i = tcg_constant_i32(0);
        rda_o = tcg_temp_new_i32();
    }

    fn(rda_o, cpu_env, qn, qm, rda_i);
    store_reg(s, a->rda, rda_o);

    mve_update_eci(s);
    return true;
}

#define DO_DUAL_ACC(INSN, FN)                                           \
    static bool trans_##INSN(DisasContext *s, arg_vmladav *a)           \
    {                                                                   \
        static MVEGenDualAccOpFn * const fns[4][2] = {                  \
            { gen_helper_mve_##FN##b, gen_helper_mve_##FN##xb },        \
            { gen_helper_mve_##FN##h, gen_helper_mve_##FN##xh },        \
            { gen_helper_mve_##FN##w, gen_helper_mve_##FN##xw },        \
            { NULL, NULL },                                             \
        };                                                              \
        return do_dual_acc(s, a, fns[a->size][a->x]);                   \
    }

DO_DUAL_ACC(VMLADAV_S, vmladavs)
DO_DUAL_ACC(VMLSDAV, vmlsdav)

static bool trans_VMLADAV_U(DisasContext *s, arg_vmladav *a)
{
    static MVEGenDualAccOpFn * const fns[4][2] = {
        { gen_helper_mve_vmladavub, NULL },
        { gen_helper_mve_vmladavuh, NULL },
        { gen_helper_mve_vmladavuw, NULL },
        { NULL, NULL },
    };
    return do_dual_acc(s, a, fns[a->size][a->x]);
}

static void gen_vpst(DisasContext *s, uint32_t mask)
{
    /*
     * Set the VPR mask fields. We take advantage of MASK01 and MASK23
     * being adjacent fields in the register.
     *
     * Updating the masks is not predicated, but it is subject to beat-wise
     * execution, and the mask is updated on the odd-numbered beats.
     * So if PSR.ECI says we should skip beat 1, we mustn't update the
     * 01 mask field.
     */
    TCGv_i32 vpr = load_cpu_field(v7m.vpr);
    switch (s->eci) {
    case ECI_NONE:
    case ECI_A0:
        /* Update both 01 and 23 fields */
        tcg_gen_deposit_i32(vpr, vpr,
                            tcg_constant_i32(mask | (mask << 4)),
                            R_V7M_VPR_MASK01_SHIFT,
                            R_V7M_VPR_MASK01_LENGTH + R_V7M_VPR_MASK23_LENGTH);
        break;
    case ECI_A0A1:
    case ECI_A0A1A2:
    case ECI_A0A1A2B0:
        /* Update only the 23 mask field */
        tcg_gen_deposit_i32(vpr, vpr,
                            tcg_constant_i32(mask),
                            R_V7M_VPR_MASK23_SHIFT, R_V7M_VPR_MASK23_LENGTH);
        break;
    default:
        g_assert_not_reached();
    }
    store_cpu_field(vpr, v7m.vpr);
}

static bool trans_VPST(DisasContext *s, arg_VPST *a)
{
    /* mask == 0 is a "related encoding" */
    if (!dc_isar_feature(aa32_mve, s) || !a->mask) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }
    gen_vpst(s, a->mask);
    mve_update_and_store_eci(s);
    return true;
}

static bool trans_VPNOT(DisasContext *s, arg_VPNOT *a)
{
    /*
     * Invert the predicate in VPR.P0. We have call out to
     * a helper because this insn itself is beatwise and can
     * be predicated.
     */
    if (!dc_isar_feature(aa32_mve, s)) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    gen_helper_mve_vpnot(cpu_env);
    /* This insn updates predication bits */
    s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
    mve_update_eci(s);
    return true;
}

static bool trans_VADDV(DisasContext *s, arg_VADDV *a)
{
    /* VADDV: vector add across vector */
    static MVEGenVADDVFn * const fns[4][2] = {
        { gen_helper_mve_vaddvsb, gen_helper_mve_vaddvub },
        { gen_helper_mve_vaddvsh, gen_helper_mve_vaddvuh },
        { gen_helper_mve_vaddvsw, gen_helper_mve_vaddvuw },
        { NULL, NULL }
    };
    TCGv_ptr qm;
    TCGv_i32 rda_i, rda_o;

    if (!dc_isar_feature(aa32_mve, s) ||
        a->size == 3) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    /*
     * This insn is subject to beat-wise execution. Partial execution
     * of an A=0 (no-accumulate) insn which does not execute the first
     * beat must start with the current value of Rda, not zero.
     */
    if (a->a || mve_skip_first_beat(s)) {
        /* Accumulate input from Rda */
        rda_o = rda_i = load_reg(s, a->rda);
    } else {
        /* Accumulate starting at zero */
        rda_i = tcg_constant_i32(0);
        rda_o = tcg_temp_new_i32();
    }

    qm = mve_qreg_ptr(a->qm);
    fns[a->size][a->u](rda_o, cpu_env, qm, rda_i);
    store_reg(s, a->rda, rda_o);

    mve_update_eci(s);
    return true;
}

static bool trans_VADDLV(DisasContext *s, arg_VADDLV *a)
{
    /*
     * Vector Add Long Across Vector: accumulate the 32-bit
     * elements of the vector into a 64-bit result stored in
     * a pair of general-purpose registers.
     * No need to check Qm's bank: it is only 3 bits in decode.
     */
    TCGv_ptr qm;
    TCGv_i64 rda_i, rda_o;
    TCGv_i32 rdalo, rdahi;

    if (!dc_isar_feature(aa32_mve, s)) {
        return false;
    }
    /*
     * rdahi == 13 is UNPREDICTABLE; rdahi == 15 is a related
     * encoding; rdalo always has bit 0 clear so cannot be 13 or 15.
     */
    if (a->rdahi == 13 || a->rdahi == 15) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    /*
     * This insn is subject to beat-wise execution. Partial execution
     * of an A=0 (no-accumulate) insn which does not execute the first
     * beat must start with the current value of RdaHi:RdaLo, not zero.
     */
    rda_o = tcg_temp_new_i64();
    if (a->a || mve_skip_first_beat(s)) {
        /* Accumulate input from RdaHi:RdaLo */
        rda_i = rda_o;
        rdalo = load_reg(s, a->rdalo);
        rdahi = load_reg(s, a->rdahi);
        tcg_gen_concat_i32_i64(rda_i, rdalo, rdahi);
    } else {
        /* Accumulate starting at zero */
        rda_i = tcg_constant_i64(0);
    }

    qm = mve_qreg_ptr(a->qm);
    if (a->u) {
        gen_helper_mve_vaddlv_u(rda_o, cpu_env, qm, rda_i);
    } else {
        gen_helper_mve_vaddlv_s(rda_o, cpu_env, qm, rda_i);
    }

    rdalo = tcg_temp_new_i32();
    rdahi = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(rdalo, rda_o);
    tcg_gen_extrh_i64_i32(rdahi, rda_o);
    store_reg(s, a->rdalo, rdalo);
    store_reg(s, a->rdahi, rdahi);
    mve_update_eci(s);
    return true;
}

static bool do_1imm(DisasContext *s, arg_1imm *a, MVEGenOneOpImmFn *fn,
                    GVecGen2iFn *vecfn)
{
    TCGv_ptr qd;
    uint64_t imm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd) ||
        !fn) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    imm = asimd_imm_const(a->imm, a->cmode, a->op);

    if (vecfn && mve_no_predication(s)) {
        vecfn(MO_64, mve_qreg_offset(a->qd), mve_qreg_offset(a->qd),
              imm, 16, 16);
    } else {
        qd = mve_qreg_ptr(a->qd);
        fn(cpu_env, qd, tcg_constant_i64(imm));
    }
    mve_update_eci(s);
    return true;
}

static void gen_gvec_vmovi(unsigned vece, uint32_t dofs, uint32_t aofs,
                           int64_t c, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_dup_imm(vece, dofs, oprsz, maxsz, c);
}

static bool trans_Vimm_1r(DisasContext *s, arg_1imm *a)
{
    /* Handle decode of cmode/op here between VORR/VBIC/VMOV */
    MVEGenOneOpImmFn *fn;
    GVecGen2iFn *vecfn;

    if ((a->cmode & 1) && a->cmode < 12) {
        if (a->op) {
            /*
             * For op=1, the immediate will be inverted by asimd_imm_const(),
             * so the VBIC becomes a logical AND operation.
             */
            fn = gen_helper_mve_vandi;
            vecfn = tcg_gen_gvec_andi;
        } else {
            fn = gen_helper_mve_vorri;
            vecfn = tcg_gen_gvec_ori;
        }
    } else {
        /* There is one unallocated cmode/op combination in this space */
        if (a->cmode == 15 && a->op == 1) {
            return false;
        }
        /* asimd_imm_const() sorts out VMVNI vs VMOVI for us */
        fn = gen_helper_mve_vmovi;
        vecfn = gen_gvec_vmovi;
    }
    return do_1imm(s, a, fn, vecfn);
}

static bool do_2shift_vec(DisasContext *s, arg_2shift *a, MVEGenTwoOpShiftFn fn,
                          bool negateshift, GVecGen2iFn vecfn)
{
    TCGv_ptr qd, qm;
    int shift = a->shift;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qd | a->qm) ||
        !fn) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    /*
     * When we handle a right shift insn using a left-shift helper
     * which permits a negative shift count to indicate a right-shift,
     * we must negate the shift count.
     */
    if (negateshift) {
        shift = -shift;
    }

    if (vecfn && mve_no_predication(s)) {
        vecfn(a->size, mve_qreg_offset(a->qd), mve_qreg_offset(a->qm),
              shift, 16, 16);
    } else {
        qd = mve_qreg_ptr(a->qd);
        qm = mve_qreg_ptr(a->qm);
        fn(cpu_env, qd, qm, tcg_constant_i32(shift));
    }
    mve_update_eci(s);
    return true;
}

static bool do_2shift(DisasContext *s, arg_2shift *a, MVEGenTwoOpShiftFn fn,
                      bool negateshift)
{
    return do_2shift_vec(s, a, fn, negateshift, NULL);
}

#define DO_2SHIFT_VEC(INSN, FN, NEGATESHIFT, VECFN)                     \
    static bool trans_##INSN(DisasContext *s, arg_2shift *a)            \
    {                                                                   \
        static MVEGenTwoOpShiftFn * const fns[] = {                     \
            gen_helper_mve_##FN##b,                                     \
            gen_helper_mve_##FN##h,                                     \
            gen_helper_mve_##FN##w,                                     \
            NULL,                                                       \
        };                                                              \
        return do_2shift_vec(s, a, fns[a->size], NEGATESHIFT, VECFN);   \
    }

#define DO_2SHIFT(INSN, FN, NEGATESHIFT)        \
    DO_2SHIFT_VEC(INSN, FN, NEGATESHIFT, NULL)

static void do_gvec_shri_s(unsigned vece, uint32_t dofs, uint32_t aofs,
                           int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    /*
     * We get here with a negated shift count, and we must handle
     * shifts by the element size, which tcg_gen_gvec_sari() does not do.
     */
    shift = -shift;
    if (shift == (8 << vece)) {
        shift--;
    }
    tcg_gen_gvec_sari(vece, dofs, aofs, shift, oprsz, maxsz);
}

static void do_gvec_shri_u(unsigned vece, uint32_t dofs, uint32_t aofs,
                           int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    /*
     * We get here with a negated shift count, and we must handle
     * shifts by the element size, which tcg_gen_gvec_shri() does not do.
     */
    shift = -shift;
    if (shift == (8 << vece)) {
        tcg_gen_gvec_dup_imm(vece, dofs, oprsz, maxsz, 0);
    } else {
        tcg_gen_gvec_shri(vece, dofs, aofs, shift, oprsz, maxsz);
    }
}

DO_2SHIFT_VEC(VSHLI, vshli_u, false, tcg_gen_gvec_shli)
DO_2SHIFT(VQSHLI_S, vqshli_s, false)
DO_2SHIFT(VQSHLI_U, vqshli_u, false)
DO_2SHIFT(VQSHLUI, vqshlui_s, false)
/* These right shifts use a left-shift helper with negated shift count */
DO_2SHIFT_VEC(VSHRI_S, vshli_s, true, do_gvec_shri_s)
DO_2SHIFT_VEC(VSHRI_U, vshli_u, true, do_gvec_shri_u)
DO_2SHIFT(VRSHRI_S, vrshli_s, true)
DO_2SHIFT(VRSHRI_U, vrshli_u, true)

DO_2SHIFT_VEC(VSRI, vsri, false, gen_gvec_sri)
DO_2SHIFT_VEC(VSLI, vsli, false, gen_gvec_sli)

#define DO_2SHIFT_FP(INSN, FN)                                  \
    static bool trans_##INSN(DisasContext *s, arg_2shift *a)    \
    {                                                           \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_2shift(s, a, gen_helper_mve_##FN, false);     \
    }

DO_2SHIFT_FP(VCVT_SH_fixed, vcvt_sh)
DO_2SHIFT_FP(VCVT_UH_fixed, vcvt_uh)
DO_2SHIFT_FP(VCVT_HS_fixed, vcvt_hs)
DO_2SHIFT_FP(VCVT_HU_fixed, vcvt_hu)
DO_2SHIFT_FP(VCVT_SF_fixed, vcvt_sf)
DO_2SHIFT_FP(VCVT_UF_fixed, vcvt_uf)
DO_2SHIFT_FP(VCVT_FS_fixed, vcvt_fs)
DO_2SHIFT_FP(VCVT_FU_fixed, vcvt_fu)

static bool do_2shift_scalar(DisasContext *s, arg_shl_scalar *a,
                             MVEGenTwoOpShiftFn *fn)
{
    TCGv_ptr qda;
    TCGv_i32 rm;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qda) ||
        a->rm == 13 || a->rm == 15 || !fn) {
        /* Rm cases are UNPREDICTABLE */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qda = mve_qreg_ptr(a->qda);
    rm = load_reg(s, a->rm);
    fn(cpu_env, qda, qda, rm);
    mve_update_eci(s);
    return true;
}

#define DO_2SHIFT_SCALAR(INSN, FN)                                      \
    static bool trans_##INSN(DisasContext *s, arg_shl_scalar *a)        \
    {                                                                   \
        static MVEGenTwoOpShiftFn * const fns[] = {                     \
            gen_helper_mve_##FN##b,                                     \
            gen_helper_mve_##FN##h,                                     \
            gen_helper_mve_##FN##w,                                     \
            NULL,                                                       \
        };                                                              \
        return do_2shift_scalar(s, a, fns[a->size]);                    \
    }

DO_2SHIFT_SCALAR(VSHL_S_scalar, vshli_s)
DO_2SHIFT_SCALAR(VSHL_U_scalar, vshli_u)
DO_2SHIFT_SCALAR(VRSHL_S_scalar, vrshli_s)
DO_2SHIFT_SCALAR(VRSHL_U_scalar, vrshli_u)
DO_2SHIFT_SCALAR(VQSHL_S_scalar, vqshli_s)
DO_2SHIFT_SCALAR(VQSHL_U_scalar, vqshli_u)
DO_2SHIFT_SCALAR(VQRSHL_S_scalar, vqrshli_s)
DO_2SHIFT_SCALAR(VQRSHL_U_scalar, vqrshli_u)

#define DO_VSHLL(INSN, FN)                                              \
    static bool trans_##INSN(DisasContext *s, arg_2shift *a)            \
    {                                                                   \
        static MVEGenTwoOpShiftFn * const fns[] = {                     \
            gen_helper_mve_##FN##b,                                     \
            gen_helper_mve_##FN##h,                                     \
        };                                                              \
        return do_2shift_vec(s, a, fns[a->size], false, do_gvec_##FN);  \
    }

/*
 * For the VSHLL vector helpers, the vece is the size of the input
 * (ie MO_8 or MO_16); the helpers want to work in the output size.
 * The shift count can be 0..<input size>, inclusive. (0 is VMOVL.)
 */
static void do_gvec_vshllbs(unsigned vece, uint32_t dofs, uint32_t aofs,
                            int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    unsigned ovece = vece + 1;
    unsigned ibits = vece == MO_8 ? 8 : 16;
    tcg_gen_gvec_shli(ovece, dofs, aofs, ibits, oprsz, maxsz);
    tcg_gen_gvec_sari(ovece, dofs, dofs, ibits - shift, oprsz, maxsz);
}

static void do_gvec_vshllbu(unsigned vece, uint32_t dofs, uint32_t aofs,
                            int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    unsigned ovece = vece + 1;
    tcg_gen_gvec_andi(ovece, dofs, aofs,
                      ovece == MO_16 ? 0xff : 0xffff, oprsz, maxsz);
    tcg_gen_gvec_shli(ovece, dofs, dofs, shift, oprsz, maxsz);
}

static void do_gvec_vshllts(unsigned vece, uint32_t dofs, uint32_t aofs,
                            int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    unsigned ovece = vece + 1;
    unsigned ibits = vece == MO_8 ? 8 : 16;
    if (shift == 0) {
        tcg_gen_gvec_sari(ovece, dofs, aofs, ibits, oprsz, maxsz);
    } else {
        tcg_gen_gvec_andi(ovece, dofs, aofs,
                          ovece == MO_16 ? 0xff00 : 0xffff0000, oprsz, maxsz);
        tcg_gen_gvec_sari(ovece, dofs, dofs, ibits - shift, oprsz, maxsz);
    }
}

static void do_gvec_vshlltu(unsigned vece, uint32_t dofs, uint32_t aofs,
                            int64_t shift, uint32_t oprsz, uint32_t maxsz)
{
    unsigned ovece = vece + 1;
    unsigned ibits = vece == MO_8 ? 8 : 16;
    if (shift == 0) {
        tcg_gen_gvec_shri(ovece, dofs, aofs, ibits, oprsz, maxsz);
    } else {
        tcg_gen_gvec_andi(ovece, dofs, aofs,
                          ovece == MO_16 ? 0xff00 : 0xffff0000, oprsz, maxsz);
        tcg_gen_gvec_shri(ovece, dofs, dofs, ibits - shift, oprsz, maxsz);
    }
}

DO_VSHLL(VSHLL_BS, vshllbs)
DO_VSHLL(VSHLL_BU, vshllbu)
DO_VSHLL(VSHLL_TS, vshllts)
DO_VSHLL(VSHLL_TU, vshlltu)

#define DO_2SHIFT_N(INSN, FN)                                   \
    static bool trans_##INSN(DisasContext *s, arg_2shift *a)    \
    {                                                           \
        static MVEGenTwoOpShiftFn * const fns[] = {             \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
        };                                                      \
        return do_2shift(s, a, fns[a->size], false);            \
    }

DO_2SHIFT_N(VSHRNB, vshrnb)
DO_2SHIFT_N(VSHRNT, vshrnt)
DO_2SHIFT_N(VRSHRNB, vrshrnb)
DO_2SHIFT_N(VRSHRNT, vrshrnt)
DO_2SHIFT_N(VQSHRNB_S, vqshrnb_s)
DO_2SHIFT_N(VQSHRNT_S, vqshrnt_s)
DO_2SHIFT_N(VQSHRNB_U, vqshrnb_u)
DO_2SHIFT_N(VQSHRNT_U, vqshrnt_u)
DO_2SHIFT_N(VQSHRUNB, vqshrunb)
DO_2SHIFT_N(VQSHRUNT, vqshrunt)
DO_2SHIFT_N(VQRSHRNB_S, vqrshrnb_s)
DO_2SHIFT_N(VQRSHRNT_S, vqrshrnt_s)
DO_2SHIFT_N(VQRSHRNB_U, vqrshrnb_u)
DO_2SHIFT_N(VQRSHRNT_U, vqrshrnt_u)
DO_2SHIFT_N(VQRSHRUNB, vqrshrunb)
DO_2SHIFT_N(VQRSHRUNT, vqrshrunt)

static bool trans_VSHLC(DisasContext *s, arg_VSHLC *a)
{
    /*
     * Whole Vector Left Shift with Carry. The carry is taken
     * from a general purpose register and written back there.
     * An imm of 0 means "shift by 32".
     */
    TCGv_ptr qd;
    TCGv_i32 rdm;

    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qd)) {
        return false;
    }
    if (a->rdm == 13 || a->rdm == 15) {
        /* CONSTRAINED UNPREDICTABLE: we UNDEF */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qd = mve_qreg_ptr(a->qd);
    rdm = load_reg(s, a->rdm);
    gen_helper_mve_vshlc(rdm, cpu_env, qd, rdm, tcg_constant_i32(a->imm));
    store_reg(s, a->rdm, rdm);
    mve_update_eci(s);
    return true;
}

static bool do_vidup(DisasContext *s, arg_vidup *a, MVEGenVIDUPFn *fn)
{
    TCGv_ptr qd;
    TCGv_i32 rn;

    /*
     * Vector increment/decrement with wrap and duplicate (VIDUP, VDDUP).
     * This fills the vector with elements of successively increasing
     * or decreasing values, starting from Rn.
     */
    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qd)) {
        return false;
    }
    if (a->size == MO_64) {
        /* size 0b11 is another encoding */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qd = mve_qreg_ptr(a->qd);
    rn = load_reg(s, a->rn);
    fn(rn, cpu_env, qd, rn, tcg_constant_i32(a->imm));
    store_reg(s, a->rn, rn);
    mve_update_eci(s);
    return true;
}

static bool do_viwdup(DisasContext *s, arg_viwdup *a, MVEGenVIWDUPFn *fn)
{
    TCGv_ptr qd;
    TCGv_i32 rn, rm;

    /*
     * Vector increment/decrement with wrap and duplicate (VIWDUp, VDWDUP)
     * This fills the vector with elements of successively increasing
     * or decreasing values, starting from Rn. Rm specifies a point where
     * the count wraps back around to 0. The updated offset is written back
     * to Rn.
     */
    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qd)) {
        return false;
    }
    if (!fn || a->rm == 13 || a->rm == 15) {
        /*
         * size 0b11 is another encoding; Rm == 13 is UNPREDICTABLE;
         * Rm == 13 is VIWDUP, VDWDUP.
         */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qd = mve_qreg_ptr(a->qd);
    rn = load_reg(s, a->rn);
    rm = load_reg(s, a->rm);
    fn(rn, cpu_env, qd, rn, rm, tcg_constant_i32(a->imm));
    store_reg(s, a->rn, rn);
    mve_update_eci(s);
    return true;
}

static bool trans_VIDUP(DisasContext *s, arg_vidup *a)
{
    static MVEGenVIDUPFn * const fns[] = {
        gen_helper_mve_vidupb,
        gen_helper_mve_viduph,
        gen_helper_mve_vidupw,
        NULL,
    };
    return do_vidup(s, a, fns[a->size]);
}

static bool trans_VDDUP(DisasContext *s, arg_vidup *a)
{
    static MVEGenVIDUPFn * const fns[] = {
        gen_helper_mve_vidupb,
        gen_helper_mve_viduph,
        gen_helper_mve_vidupw,
        NULL,
    };
    /* VDDUP is just like VIDUP but with a negative immediate */
    a->imm = -a->imm;
    return do_vidup(s, a, fns[a->size]);
}

static bool trans_VIWDUP(DisasContext *s, arg_viwdup *a)
{
    static MVEGenVIWDUPFn * const fns[] = {
        gen_helper_mve_viwdupb,
        gen_helper_mve_viwduph,
        gen_helper_mve_viwdupw,
        NULL,
    };
    return do_viwdup(s, a, fns[a->size]);
}

static bool trans_VDWDUP(DisasContext *s, arg_viwdup *a)
{
    static MVEGenVIWDUPFn * const fns[] = {
        gen_helper_mve_vdwdupb,
        gen_helper_mve_vdwduph,
        gen_helper_mve_vdwdupw,
        NULL,
    };
    return do_viwdup(s, a, fns[a->size]);
}

static bool do_vcmp(DisasContext *s, arg_vcmp *a, MVEGenCmpFn *fn)
{
    TCGv_ptr qn, qm;

    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qm) ||
        !fn) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qn = mve_qreg_ptr(a->qn);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qn, qm);
    if (a->mask) {
        /* VPT */
        gen_vpst(s, a->mask);
    }
    /* This insn updates predication bits */
    s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
    mve_update_eci(s);
    return true;
}

static bool do_vcmp_scalar(DisasContext *s, arg_vcmp_scalar *a,
                           MVEGenScalarCmpFn *fn)
{
    TCGv_ptr qn;
    TCGv_i32 rm;

    if (!dc_isar_feature(aa32_mve, s) || !fn || a->rm == 13) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qn = mve_qreg_ptr(a->qn);
    if (a->rm == 15) {
        /* Encoding Rm=0b1111 means "constant zero" */
        rm = tcg_constant_i32(0);
    } else {
        rm = load_reg(s, a->rm);
    }
    fn(cpu_env, qn, rm);
    if (a->mask) {
        /* VPT */
        gen_vpst(s, a->mask);
    }
    /* This insn updates predication bits */
    s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
    mve_update_eci(s);
    return true;
}

#define DO_VCMP(INSN, FN)                                       \
    static bool trans_##INSN(DisasContext *s, arg_vcmp *a)      \
    {                                                           \
        static MVEGenCmpFn * const fns[] = {                    \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_vcmp(s, a, fns[a->size]);                     \
    }                                                           \
    static bool trans_##INSN##_scalar(DisasContext *s,          \
                                      arg_vcmp_scalar *a)       \
    {                                                           \
        static MVEGenScalarCmpFn * const fns[] = {              \
            gen_helper_mve_##FN##_scalarb,                      \
            gen_helper_mve_##FN##_scalarh,                      \
            gen_helper_mve_##FN##_scalarw,                      \
            NULL,                                               \
        };                                                      \
        return do_vcmp_scalar(s, a, fns[a->size]);              \
    }

DO_VCMP(VCMPEQ, vcmpeq)
DO_VCMP(VCMPNE, vcmpne)
DO_VCMP(VCMPCS, vcmpcs)
DO_VCMP(VCMPHI, vcmphi)
DO_VCMP(VCMPGE, vcmpge)
DO_VCMP(VCMPLT, vcmplt)
DO_VCMP(VCMPGT, vcmpgt)
DO_VCMP(VCMPLE, vcmple)

#define DO_VCMP_FP(INSN, FN)                                    \
    static bool trans_##INSN(DisasContext *s, arg_vcmp *a)      \
    {                                                           \
        static MVEGenCmpFn * const fns[] = {                    \
            NULL,                                               \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##s,                             \
            NULL,                                               \
        };                                                      \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_vcmp(s, a, fns[a->size]);                     \
    }                                                           \
    static bool trans_##INSN##_scalar(DisasContext *s,          \
                                      arg_vcmp_scalar *a)       \
    {                                                           \
        static MVEGenScalarCmpFn * const fns[] = {              \
            NULL,                                               \
            gen_helper_mve_##FN##_scalarh,                      \
            gen_helper_mve_##FN##_scalars,                      \
            NULL,                                               \
        };                                                      \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_vcmp_scalar(s, a, fns[a->size]);              \
    }

DO_VCMP_FP(VCMPEQ_fp, vfcmpeq)
DO_VCMP_FP(VCMPNE_fp, vfcmpne)
DO_VCMP_FP(VCMPGE_fp, vfcmpge)
DO_VCMP_FP(VCMPLT_fp, vfcmplt)
DO_VCMP_FP(VCMPGT_fp, vfcmpgt)
DO_VCMP_FP(VCMPLE_fp, vfcmple)

static bool do_vmaxv(DisasContext *s, arg_vmaxv *a, MVEGenVADDVFn fn)
{
    /*
     * MIN/MAX operations across a vector: compute the min or
     * max of the initial value in a general purpose register
     * and all the elements in the vector, and store it back
     * into the general purpose register.
     */
    TCGv_ptr qm;
    TCGv_i32 rda;

    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qm) ||
        !fn || a->rda == 13 || a->rda == 15) {
        /* Rda cases are UNPREDICTABLE */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qm = mve_qreg_ptr(a->qm);
    rda = load_reg(s, a->rda);
    fn(rda, cpu_env, qm, rda);
    store_reg(s, a->rda, rda);
    mve_update_eci(s);
    return true;
}

#define DO_VMAXV(INSN, FN)                                      \
    static bool trans_##INSN(DisasContext *s, arg_vmaxv *a)     \
    {                                                           \
        static MVEGenVADDVFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_vmaxv(s, a, fns[a->size]);                    \
    }

DO_VMAXV(VMAXV_S, vmaxvs)
DO_VMAXV(VMAXV_U, vmaxvu)
DO_VMAXV(VMAXAV, vmaxav)
DO_VMAXV(VMINV_S, vminvs)
DO_VMAXV(VMINV_U, vminvu)
DO_VMAXV(VMINAV, vminav)

#define DO_VMAXV_FP(INSN, FN)                                   \
    static bool trans_##INSN(DisasContext *s, arg_vmaxv *a)     \
    {                                                           \
        static MVEGenVADDVFn * const fns[] = {                  \
            NULL,                                               \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##s,                             \
            NULL,                                               \
        };                                                      \
        if (!dc_isar_feature(aa32_mve_fp, s)) {                 \
            return false;                                       \
        }                                                       \
        return do_vmaxv(s, a, fns[a->size]);                    \
    }

DO_VMAXV_FP(VMAXNMV, vmaxnmv)
DO_VMAXV_FP(VMINNMV, vminnmv)
DO_VMAXV_FP(VMAXNMAV, vmaxnmav)
DO_VMAXV_FP(VMINNMAV, vminnmav)

static bool do_vabav(DisasContext *s, arg_vabav *a, MVEGenVABAVFn *fn)
{
    /* Absolute difference accumulated across vector */
    TCGv_ptr qn, qm;
    TCGv_i32 rda;

    if (!dc_isar_feature(aa32_mve, s) ||
        !mve_check_qreg_bank(s, a->qm | a->qn) ||
        !fn || a->rda == 13 || a->rda == 15) {
        /* Rda cases are UNPREDICTABLE */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    qm = mve_qreg_ptr(a->qm);
    qn = mve_qreg_ptr(a->qn);
    rda = load_reg(s, a->rda);
    fn(rda, cpu_env, qn, qm, rda);
    store_reg(s, a->rda, rda);
    mve_update_eci(s);
    return true;
}

#define DO_VABAV(INSN, FN)                                      \
    static bool trans_##INSN(DisasContext *s, arg_vabav *a)     \
    {                                                           \
        static MVEGenVABAVFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_vabav(s, a, fns[a->size]);                    \
    }

DO_VABAV(VABAV_S, vabavs)
DO_VABAV(VABAV_U, vabavu)

static bool trans_VMOV_to_2gp(DisasContext *s, arg_VMOV_to_2gp *a)
{
    /*
     * VMOV two 32-bit vector lanes to two general-purpose registers.
     * This insn is not predicated but it is subject to beat-wise
     * execution if it is not in an IT block. For us this means
     * only that if PSR.ECI says we should not be executing the beat
     * corresponding to the lane of the vector register being accessed
     * then we should skip performing the move, and that we need to do
     * the usual check for bad ECI state and advance of ECI state.
     * (If PSR.ECI is non-zero then we cannot be in an IT block.)
     */
    TCGv_i32 tmp;
    int vd;

    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qd) ||
        a->rt == 13 || a->rt == 15 || a->rt2 == 13 || a->rt2 == 15 ||
        a->rt == a->rt2) {
        /* Rt/Rt2 cases are UNPREDICTABLE */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    /* Convert Qreg index to Dreg for read_neon_element32() etc */
    vd = a->qd * 2;

    if (!mve_skip_vmov(s, vd, a->idx, MO_32)) {
        tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, vd, a->idx, MO_32);
        store_reg(s, a->rt, tmp);
    }
    if (!mve_skip_vmov(s, vd + 1, a->idx, MO_32)) {
        tmp = tcg_temp_new_i32();
        read_neon_element32(tmp, vd + 1, a->idx, MO_32);
        store_reg(s, a->rt2, tmp);
    }

    mve_update_and_store_eci(s);
    return true;
}

static bool trans_VMOV_from_2gp(DisasContext *s, arg_VMOV_to_2gp *a)
{
    /*
     * VMOV two general-purpose registers to two 32-bit vector lanes.
     * This insn is not predicated but it is subject to beat-wise
     * execution if it is not in an IT block. For us this means
     * only that if PSR.ECI says we should not be executing the beat
     * corresponding to the lane of the vector register being accessed
     * then we should skip performing the move, and that we need to do
     * the usual check for bad ECI state and advance of ECI state.
     * (If PSR.ECI is non-zero then we cannot be in an IT block.)
     */
    TCGv_i32 tmp;
    int vd;

    if (!dc_isar_feature(aa32_mve, s) || !mve_check_qreg_bank(s, a->qd) ||
        a->rt == 13 || a->rt == 15 || a->rt2 == 13 || a->rt2 == 15) {
        /* Rt/Rt2 cases are UNPREDICTABLE */
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }

    /* Convert Qreg idx to Dreg for read_neon_element32() etc */
    vd = a->qd * 2;

    if (!mve_skip_vmov(s, vd, a->idx, MO_32)) {
        tmp = load_reg(s, a->rt);
        write_neon_element32(tmp, vd, a->idx, MO_32);
    }
    if (!mve_skip_vmov(s, vd + 1, a->idx, MO_32)) {
        tmp = load_reg(s, a->rt2);
        write_neon_element32(tmp, vd + 1, a->idx, MO_32);
    }

    mve_update_and_store_eci(s);
    return true;
}
