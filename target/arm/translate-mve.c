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
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "translate.h"
#include "translate-a32.h"

/* Include the generated decoder */
#include "decode-mve.c.inc"

typedef void MVEGenLdStFn(TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenOneOpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void MVEGenTwoOpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void MVEGenTwoOpScalarFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenTwoOpShiftFn(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenDualAccOpFn(TCGv_i64, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i64);
typedef void MVEGenVADDVFn(TCGv_i32, TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void MVEGenOneOpImmFn(TCGv_ptr, TCGv_ptr, TCGv_i64);

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
        gen_exception_insn(s, s->pc_curr, EXCP_INVSTATE, syn_uncategorized(),
                           default_exception_el(s));
        return false;
    }
}

static void mve_update_eci(DisasContext *s)
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
    tcg_temp_free_ptr(qreg);

    /*
     * Writeback always happens after the last beat of the insn,
     * regardless of predication
     */
    if (a->w) {
        if (!a->p) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    } else {
        tcg_temp_free_i32(addr);
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

    qd = mve_qreg_ptr(a->qd);
    rt = load_reg(s, a->rt);
    tcg_gen_dup_i32(a->size, rt, rt);
    gen_helper_mve_vdup(cpu_env, qd, rt);
    tcg_temp_free_ptr(qd);
    tcg_temp_free_i32(rt);
    mve_update_eci(s);
    return true;
}

static bool do_1op(DisasContext *s, arg_1op *a, MVEGenOneOpFn fn)
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

    qd = mve_qreg_ptr(a->qd);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qd, qm);
    tcg_temp_free_ptr(qd);
    tcg_temp_free_ptr(qm);
    mve_update_eci(s);
    return true;
}

#define DO_1OP(INSN, FN)                                        \
    static bool trans_##INSN(DisasContext *s, arg_1op *a)       \
    {                                                           \
        static MVEGenOneOpFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_1op(s, a, fns[a->size]);                      \
    }

DO_1OP(VCLZ, vclz)
DO_1OP(VCLS, vcls)
DO_1OP(VABS, vabs)
DO_1OP(VNEG, vneg)

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
    return do_1op(s, a, gen_helper_mve_vmvn);
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

static bool do_2op(DisasContext *s, arg_2op *a, MVEGenTwoOpFn fn)
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

    qd = mve_qreg_ptr(a->qd);
    qn = mve_qreg_ptr(a->qn);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qd, qn, qm);
    tcg_temp_free_ptr(qd);
    tcg_temp_free_ptr(qn);
    tcg_temp_free_ptr(qm);
    mve_update_eci(s);
    return true;
}

#define DO_LOGIC(INSN, HELPER)                                  \
    static bool trans_##INSN(DisasContext *s, arg_2op *a)       \
    {                                                           \
        return do_2op(s, a, HELPER);                            \
    }

DO_LOGIC(VAND, gen_helper_mve_vand)
DO_LOGIC(VBIC, gen_helper_mve_vbic)
DO_LOGIC(VORR, gen_helper_mve_vorr)
DO_LOGIC(VORN, gen_helper_mve_vorn)
DO_LOGIC(VEOR, gen_helper_mve_veor)

#define DO_2OP(INSN, FN) \
    static bool trans_##INSN(DisasContext *s, arg_2op *a)       \
    {                                                           \
        static MVEGenTwoOpFn * const fns[] = {                  \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_2op(s, a, fns[a->size]);                      \
    }

DO_2OP(VADD, vadd)
DO_2OP(VSUB, vsub)
DO_2OP(VMUL, vmul)
DO_2OP(VMULH_S, vmulhs)
DO_2OP(VMULH_U, vmulhu)
DO_2OP(VRMULH_S, vrmulhs)
DO_2OP(VRMULH_U, vrmulhu)
DO_2OP(VMAX_S, vmaxs)
DO_2OP(VMAX_U, vmaxu)
DO_2OP(VMIN_S, vmins)
DO_2OP(VMIN_U, vminu)
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
    tcg_temp_free_i32(rm);
    tcg_temp_free_ptr(qd);
    tcg_temp_free_ptr(qn);
    mve_update_eci(s);
    return true;
}

#define DO_2OP_SCALAR(INSN, FN) \
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

static bool do_long_dual_acc(DisasContext *s, arg_vmlaldav *a,
                             MVEGenDualAccOpFn *fn)
{
    TCGv_ptr qn, qm;
    TCGv_i64 rda;
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
    if (a->a || mve_skip_first_beat(s)) {
        rda = tcg_temp_new_i64();
        rdalo = load_reg(s, a->rdalo);
        rdahi = load_reg(s, a->rdahi);
        tcg_gen_concat_i32_i64(rda, rdalo, rdahi);
        tcg_temp_free_i32(rdalo);
        tcg_temp_free_i32(rdahi);
    } else {
        rda = tcg_const_i64(0);
    }

    fn(rda, cpu_env, qn, qm, rda);
    tcg_temp_free_ptr(qn);
    tcg_temp_free_ptr(qm);

    rdalo = tcg_temp_new_i32();
    rdahi = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(rdalo, rda);
    tcg_gen_extrh_i64_i32(rdahi, rda);
    store_reg(s, a->rdalo, rdalo);
    store_reg(s, a->rdahi, rdahi);
    tcg_temp_free_i64(rda);
    mve_update_eci(s);
    return true;
}

static bool trans_VMLALDAV_S(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenDualAccOpFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vmlaldavsh, gen_helper_mve_vmlaldavxsh },
        { gen_helper_mve_vmlaldavsw, gen_helper_mve_vmlaldavxsw },
        { NULL, NULL },
    };
    return do_long_dual_acc(s, a, fns[a->size][a->x]);
}

static bool trans_VMLALDAV_U(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenDualAccOpFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vmlaldavuh, NULL },
        { gen_helper_mve_vmlaldavuw, NULL },
        { NULL, NULL },
    };
    return do_long_dual_acc(s, a, fns[a->size][a->x]);
}

static bool trans_VMLSLDAV(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenDualAccOpFn * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_mve_vmlsldavsh, gen_helper_mve_vmlsldavxsh },
        { gen_helper_mve_vmlsldavsw, gen_helper_mve_vmlsldavxsw },
        { NULL, NULL },
    };
    return do_long_dual_acc(s, a, fns[a->size][a->x]);
}

static bool trans_VRMLALDAVH_S(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenDualAccOpFn * const fns[] = {
        gen_helper_mve_vrmlaldavhsw, gen_helper_mve_vrmlaldavhxsw,
    };
    return do_long_dual_acc(s, a, fns[a->x]);
}

static bool trans_VRMLALDAVH_U(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenDualAccOpFn * const fns[] = {
        gen_helper_mve_vrmlaldavhuw, NULL,
    };
    return do_long_dual_acc(s, a, fns[a->x]);
}

static bool trans_VRMLSLDAVH(DisasContext *s, arg_vmlaldav *a)
{
    static MVEGenDualAccOpFn * const fns[] = {
        gen_helper_mve_vrmlsldavhsw, gen_helper_mve_vrmlsldavhxsw,
    };
    return do_long_dual_acc(s, a, fns[a->x]);
}

static bool trans_VPST(DisasContext *s, arg_VPST *a)
{
    TCGv_i32 vpr;

    /* mask == 0 is a "related encoding" */
    if (!dc_isar_feature(aa32_mve, s) || !a->mask) {
        return false;
    }
    if (!mve_eci_check(s) || !vfp_access_check(s)) {
        return true;
    }
    /*
     * Set the VPR mask fields. We take advantage of MASK01 and MASK23
     * being adjacent fields in the register.
     *
     * This insn is not predicated, but it is subject to beat-wise
     * execution, and the mask is updated on the odd-numbered beats.
     * So if PSR.ECI says we should skip beat 1, we mustn't update the
     * 01 mask field.
     */
    vpr = load_cpu_field(v7m.vpr);
    switch (s->eci) {
    case ECI_NONE:
    case ECI_A0:
        /* Update both 01 and 23 fields */
        tcg_gen_deposit_i32(vpr, vpr,
                            tcg_constant_i32(a->mask | (a->mask << 4)),
                            R_V7M_VPR_MASK01_SHIFT,
                            R_V7M_VPR_MASK01_LENGTH + R_V7M_VPR_MASK23_LENGTH);
        break;
    case ECI_A0A1:
    case ECI_A0A1A2:
    case ECI_A0A1A2B0:
        /* Update only the 23 mask field */
        tcg_gen_deposit_i32(vpr, vpr,
                            tcg_constant_i32(a->mask),
                            R_V7M_VPR_MASK23_SHIFT, R_V7M_VPR_MASK23_LENGTH);
        break;
    default:
        g_assert_not_reached();
    }
    store_cpu_field(vpr, v7m.vpr);
    mve_update_and_store_eci(s);
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
    TCGv_i32 rda;

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
        rda = load_reg(s, a->rda);
    } else {
        /* Accumulate starting at zero */
        rda = tcg_const_i32(0);
    }

    qm = mve_qreg_ptr(a->qm);
    fns[a->size][a->u](rda, cpu_env, qm, rda);
    store_reg(s, a->rda, rda);
    tcg_temp_free_ptr(qm);

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
    TCGv_i64 rda;
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
    if (a->a || mve_skip_first_beat(s)) {
        /* Accumulate input from RdaHi:RdaLo */
        rda = tcg_temp_new_i64();
        rdalo = load_reg(s, a->rdalo);
        rdahi = load_reg(s, a->rdahi);
        tcg_gen_concat_i32_i64(rda, rdalo, rdahi);
        tcg_temp_free_i32(rdalo);
        tcg_temp_free_i32(rdahi);
    } else {
        /* Accumulate starting at zero */
        rda = tcg_const_i64(0);
    }

    qm = mve_qreg_ptr(a->qm);
    if (a->u) {
        gen_helper_mve_vaddlv_u(rda, cpu_env, qm, rda);
    } else {
        gen_helper_mve_vaddlv_s(rda, cpu_env, qm, rda);
    }
    tcg_temp_free_ptr(qm);

    rdalo = tcg_temp_new_i32();
    rdahi = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(rdalo, rda);
    tcg_gen_extrh_i64_i32(rdahi, rda);
    store_reg(s, a->rdalo, rdalo);
    store_reg(s, a->rdahi, rdahi);
    tcg_temp_free_i64(rda);
    mve_update_eci(s);
    return true;
}

static bool do_1imm(DisasContext *s, arg_1imm *a, MVEGenOneOpImmFn *fn)
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

    qd = mve_qreg_ptr(a->qd);
    fn(cpu_env, qd, tcg_constant_i64(imm));
    tcg_temp_free_ptr(qd);
    mve_update_eci(s);
    return true;
}

static bool trans_Vimm_1r(DisasContext *s, arg_1imm *a)
{
    /* Handle decode of cmode/op here between VORR/VBIC/VMOV */
    MVEGenOneOpImmFn *fn;

    if ((a->cmode & 1) && a->cmode < 12) {
        if (a->op) {
            /*
             * For op=1, the immediate will be inverted by asimd_imm_const(),
             * so the VBIC becomes a logical AND operation.
             */
            fn = gen_helper_mve_vandi;
        } else {
            fn = gen_helper_mve_vorri;
        }
    } else {
        /* There is one unallocated cmode/op combination in this space */
        if (a->cmode == 15 && a->op == 1) {
            return false;
        }
        /* asimd_imm_const() sorts out VMVNI vs VMOVI for us */
        fn = gen_helper_mve_vmovi;
    }
    return do_1imm(s, a, fn);
}

static bool do_2shift(DisasContext *s, arg_2shift *a, MVEGenTwoOpShiftFn fn,
                      bool negateshift)
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

    qd = mve_qreg_ptr(a->qd);
    qm = mve_qreg_ptr(a->qm);
    fn(cpu_env, qd, qm, tcg_constant_i32(shift));
    tcg_temp_free_ptr(qd);
    tcg_temp_free_ptr(qm);
    mve_update_eci(s);
    return true;
}

#define DO_2SHIFT(INSN, FN, NEGATESHIFT)                         \
    static bool trans_##INSN(DisasContext *s, arg_2shift *a)    \
    {                                                           \
        static MVEGenTwoOpShiftFn * const fns[] = {             \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
            gen_helper_mve_##FN##w,                             \
            NULL,                                               \
        };                                                      \
        return do_2shift(s, a, fns[a->size], NEGATESHIFT);      \
    }

DO_2SHIFT(VSHLI, vshli_u, false)
DO_2SHIFT(VQSHLI_S, vqshli_s, false)
DO_2SHIFT(VQSHLI_U, vqshli_u, false)
DO_2SHIFT(VQSHLUI, vqshlui_s, false)
/* These right shifts use a left-shift helper with negated shift count */
DO_2SHIFT(VSHRI_S, vshli_s, true)
DO_2SHIFT(VSHRI_U, vshli_u, true)
DO_2SHIFT(VRSHRI_S, vrshli_s, true)
DO_2SHIFT(VRSHRI_U, vrshli_u, true)

DO_2SHIFT(VSRI, vsri, false)
DO_2SHIFT(VSLI, vsli, false)

#define DO_VSHLL(INSN, FN)                                      \
    static bool trans_##INSN(DisasContext *s, arg_2shift *a)    \
    {                                                           \
        static MVEGenTwoOpShiftFn * const fns[] = {             \
            gen_helper_mve_##FN##b,                             \
            gen_helper_mve_##FN##h,                             \
        };                                                      \
        return do_2shift(s, a, fns[a->size], false);            \
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
    tcg_temp_free_ptr(qd);
    mve_update_eci(s);
    return true;
}
