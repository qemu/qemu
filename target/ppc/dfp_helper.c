/*
 *  PowerPC Decimal Floating Point (DPF) emulation helpers for QEMU.
 *
 *  Copyright (c) 2014 IBM Corporation.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

#define DECNUMDIGITS 34
#include "libdecnumber/decContext.h"
#include "libdecnumber/decNumber.h"
#include "libdecnumber/dpd/decimal32.h"
#include "libdecnumber/dpd/decimal64.h"
#include "libdecnumber/dpd/decimal128.h"


static void get_dfp64(ppc_vsr_t *dst, ppc_fprp_t *dfp)
{
    dst->VsrD(1) = dfp->VsrD(0);
}

static void get_dfp128(ppc_vsr_t *dst, ppc_fprp_t *dfp)
{
    dst->VsrD(0) = dfp[0].VsrD(0);
    dst->VsrD(1) = dfp[1].VsrD(0);
}

static void set_dfp64(ppc_fprp_t *dfp, ppc_vsr_t *src)
{
    dfp->VsrD(0) = src->VsrD(1);
}

static void set_dfp128(ppc_fprp_t *dfp, ppc_vsr_t *src)
{
    dfp[0].VsrD(0) = src->VsrD(0);
    dfp[1].VsrD(0) = src->VsrD(1);
}

struct PPC_DFP {
    CPUPPCState *env;
    ppc_vsr_t vt, va, vb;
    decNumber t, a, b;
    decContext context;
    uint8_t crbf;
};

static void dfp_prepare_rounding_mode(decContext *context, uint64_t fpscr)
{
    enum rounding rnd;

    switch ((fpscr & FP_DRN) >> FPSCR_DRN0) {
    case 0:
        rnd = DEC_ROUND_HALF_EVEN;
        break;
    case 1:
        rnd = DEC_ROUND_DOWN;
        break;
    case 2:
         rnd = DEC_ROUND_CEILING;
         break;
    case 3:
         rnd = DEC_ROUND_FLOOR;
         break;
    case 4:
         rnd = DEC_ROUND_HALF_UP;
         break;
    case 5:
         rnd = DEC_ROUND_HALF_DOWN;
         break;
    case 6:
         rnd = DEC_ROUND_UP;
         break;
    case 7:
         rnd = DEC_ROUND_05UP;
         break;
    default:
        g_assert_not_reached();
    }

    decContextSetRounding(context, rnd);
}

static void dfp_set_round_mode_from_immediate(uint8_t r, uint8_t rmc,
                                                  struct PPC_DFP *dfp)
{
    enum rounding rnd;
    if (r == 0) {
        switch (rmc & 3) {
        case 0:
            rnd = DEC_ROUND_HALF_EVEN;
            break;
        case 1:
            rnd = DEC_ROUND_DOWN;
            break;
        case 2:
            rnd = DEC_ROUND_HALF_UP;
            break;
        case 3: /* use FPSCR rounding mode */
            return;
        default:
            assert(0); /* cannot get here */
        }
    } else { /* r == 1 */
        switch (rmc & 3) {
        case 0:
            rnd = DEC_ROUND_CEILING;
            break;
        case 1:
            rnd = DEC_ROUND_FLOOR;
            break;
        case 2:
            rnd = DEC_ROUND_UP;
            break;
        case 3:
            rnd = DEC_ROUND_HALF_DOWN;
            break;
        default:
            assert(0); /* cannot get here */
        }
    }
    decContextSetRounding(&dfp->context, rnd);
}

static void dfp_prepare_decimal64(struct PPC_DFP *dfp, ppc_fprp_t *a,
                                  ppc_fprp_t *b, CPUPPCState *env)
{
    decContextDefault(&dfp->context, DEC_INIT_DECIMAL64);
    dfp_prepare_rounding_mode(&dfp->context, env->fpscr);
    dfp->env = env;

    if (a) {
        get_dfp64(&dfp->va, a);
        decimal64ToNumber((decimal64 *)&dfp->va.VsrD(1), &dfp->a);
    } else {
        dfp->va.VsrD(1) = 0;
        decNumberZero(&dfp->a);
    }

    if (b) {
        get_dfp64(&dfp->vb, b);
        decimal64ToNumber((decimal64 *)&dfp->vb.VsrD(1), &dfp->b);
    } else {
        dfp->vb.VsrD(1) = 0;
        decNumberZero(&dfp->b);
    }
}

static void dfp_prepare_decimal128(struct PPC_DFP *dfp, ppc_fprp_t *a,
                                   ppc_fprp_t *b, CPUPPCState *env)
{
    decContextDefault(&dfp->context, DEC_INIT_DECIMAL128);
    dfp_prepare_rounding_mode(&dfp->context, env->fpscr);
    dfp->env = env;

    if (a) {
        get_dfp128(&dfp->va, a);
        decimal128ToNumber((decimal128 *)&dfp->va, &dfp->a);
    } else {
        dfp->va.VsrD(0) = dfp->va.VsrD(1) = 0;
        decNumberZero(&dfp->a);
    }

    if (b) {
        get_dfp128(&dfp->vb, b);
        decimal128ToNumber((decimal128 *)&dfp->vb, &dfp->b);
    } else {
        dfp->vb.VsrD(0) = dfp->vb.VsrD(1) = 0;
        decNumberZero(&dfp->b);
    }
}

static void dfp_finalize_decimal64(struct PPC_DFP *dfp)
{
    decimal64FromNumber((decimal64 *)&dfp->vt.VsrD(1), &dfp->t, &dfp->context);
}

static void dfp_finalize_decimal128(struct PPC_DFP *dfp)
{
    decimal128FromNumber((decimal128 *)&dfp->vt, &dfp->t, &dfp->context);
}

static void dfp_set_FPSCR_flag(struct PPC_DFP *dfp, uint64_t flag,
                uint64_t enabled)
{
    dfp->env->fpscr |= (flag | FP_FX);
    if (dfp->env->fpscr & enabled) {
        dfp->env->fpscr |= FP_FEX;
    }
}

static void dfp_set_FPRF_from_FRT_with_context(struct PPC_DFP *dfp,
                decContext *context)
{
    uint64_t fprf = 0;

    /* construct FPRF */
    switch (decNumberClass(&dfp->t, context)) {
    case DEC_CLASS_SNAN:
        fprf = 0x01;
        break;
    case DEC_CLASS_QNAN:
        fprf = 0x11;
        break;
    case DEC_CLASS_NEG_INF:
        fprf = 0x09;
        break;
    case DEC_CLASS_NEG_NORMAL:
        fprf = 0x08;
        break;
    case DEC_CLASS_NEG_SUBNORMAL:
        fprf = 0x18;
        break;
    case DEC_CLASS_NEG_ZERO:
        fprf = 0x12;
        break;
    case DEC_CLASS_POS_ZERO:
        fprf = 0x02;
        break;
    case DEC_CLASS_POS_SUBNORMAL:
        fprf = 0x14;
        break;
    case DEC_CLASS_POS_NORMAL:
        fprf = 0x04;
        break;
    case DEC_CLASS_POS_INF:
        fprf = 0x05;
        break;
    default:
        assert(0); /* should never get here */
    }
    dfp->env->fpscr &= ~FP_FPRF;
    dfp->env->fpscr |= (fprf << FPSCR_FPRF);
}

static void dfp_set_FPRF_from_FRT(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT_with_context(dfp, &dfp->context);
}

static void dfp_set_FPRF_from_FRT_short(struct PPC_DFP *dfp)
{
    decContext shortContext;
    decContextDefault(&shortContext, DEC_INIT_DECIMAL32);
    dfp_set_FPRF_from_FRT_with_context(dfp, &shortContext);
}

static void dfp_set_FPRF_from_FRT_long(struct PPC_DFP *dfp)
{
    decContext longContext;
    decContextDefault(&longContext, DEC_INIT_DECIMAL64);
    dfp_set_FPRF_from_FRT_with_context(dfp, &longContext);
}

static void dfp_check_for_OX(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Overflow) {
        dfp_set_FPSCR_flag(dfp, FP_OX, FP_OE);
    }
}

static void dfp_check_for_UX(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Underflow) {
        dfp_set_FPSCR_flag(dfp, FP_UX, FP_UE);
    }
}

static void dfp_check_for_XX(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Inexact) {
        dfp_set_FPSCR_flag(dfp, FP_XX | FP_FI, FP_XE);
    }
}

static void dfp_check_for_ZX(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Division_by_zero) {
        dfp_set_FPSCR_flag(dfp, FP_ZX, FP_ZE);
    }
}

static void dfp_check_for_VXSNAN(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Invalid_operation) {
        if (decNumberIsSNaN(&dfp->a) || decNumberIsSNaN(&dfp->b)) {
            dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXSNAN, FP_VE);
        }
    }
}

static void dfp_check_for_VXSNAN_and_convert_to_QNaN(struct PPC_DFP *dfp)
{
    if (decNumberIsSNaN(&dfp->t)) {
        dfp->t.bits &= ~DECSNAN;
        dfp->t.bits |= DECNAN;
        dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXSNAN, FP_VE);
    }
}

static void dfp_check_for_VXISI(struct PPC_DFP *dfp, int testForSameSign)
{
    if (dfp->context.status & DEC_Invalid_operation) {
        if (decNumberIsInfinite(&dfp->a) && decNumberIsInfinite(&dfp->b)) {
            int same = decNumberClass(&dfp->a, &dfp->context) ==
                       decNumberClass(&dfp->b, &dfp->context);
            if ((same && testForSameSign) || (!same && !testForSameSign)) {
                dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXISI, FP_VE);
            }
        }
    }
}

static void dfp_check_for_VXISI_add(struct PPC_DFP *dfp)
{
    dfp_check_for_VXISI(dfp, 0);
}

static void dfp_check_for_VXISI_subtract(struct PPC_DFP *dfp)
{
    dfp_check_for_VXISI(dfp, 1);
}

static void dfp_check_for_VXIMZ(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Invalid_operation) {
        if ((decNumberIsInfinite(&dfp->a) && decNumberIsZero(&dfp->b)) ||
            (decNumberIsInfinite(&dfp->b) && decNumberIsZero(&dfp->a))) {
            dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXIMZ, FP_VE);
        }
    }
}

static void dfp_check_for_VXZDZ(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Division_undefined) {
        dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXZDZ, FP_VE);
    }
}

static void dfp_check_for_VXIDI(struct PPC_DFP *dfp)
{
    if (dfp->context.status & DEC_Invalid_operation) {
        if (decNumberIsInfinite(&dfp->a) && decNumberIsInfinite(&dfp->b)) {
            dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXIDI, FP_VE);
        }
    }
}

static void dfp_check_for_VXVC(struct PPC_DFP *dfp)
{
    if (decNumberIsNaN(&dfp->a) || decNumberIsNaN(&dfp->b)) {
        dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXVC, FP_VE);
    }
}

static void dfp_check_for_VXCVI(struct PPC_DFP *dfp)
{
    if ((dfp->context.status & DEC_Invalid_operation) &&
        (!decNumberIsSNaN(&dfp->a)) &&
        (!decNumberIsSNaN(&dfp->b))) {
        dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXCVI, FP_VE);
    }
}

static void dfp_set_CRBF_from_T(struct PPC_DFP *dfp)
{
    if (decNumberIsNaN(&dfp->t)) {
        dfp->crbf = 1;
    } else if (decNumberIsZero(&dfp->t)) {
        dfp->crbf = 2;
    } else if (decNumberIsNegative(&dfp->t)) {
        dfp->crbf = 8;
    } else {
        dfp->crbf = 4;
    }
}

static void dfp_set_FPCC_from_CRBF(struct PPC_DFP *dfp)
{
    dfp->env->fpscr &= ~FP_FPCC;
    dfp->env->fpscr |= (dfp->crbf << FPSCR_FPCC);
}

static inline void dfp_makeQNaN(decNumber *dn)
{
    dn->bits &= ~DECSPECIAL;
    dn->bits |= DECNAN;
}

static inline int dfp_get_digit(decNumber *dn, int n)
{
    assert(DECDPUN == 3);
    int unit = n / DECDPUN;
    int dig = n % DECDPUN;
    switch (dig) {
    case 0:
        return dn->lsu[unit] % 10;
    case 1:
        return (dn->lsu[unit] / 10) % 10;
    case 2:
        return dn->lsu[unit] / 100;
    }
    g_assert_not_reached();
}

#define DFP_HELPER_TAB(op, dnop, postprocs, size)                              \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *a,               \
                 ppc_fprp_t *b)                                                \
{                                                                              \
    struct PPC_DFP dfp;                                                        \
    dfp_prepare_decimal##size(&dfp, a, b, env);                                \
    dnop(&dfp.t, &dfp.a, &dfp.b, &dfp.context);                                \
    dfp_finalize_decimal##size(&dfp);                                          \
    postprocs(&dfp);                                                           \
    set_dfp##size(t, &dfp.vt);                                                 \
}

static void ADD_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_OX(dfp);
    dfp_check_for_UX(dfp);
    dfp_check_for_XX(dfp);
    dfp_check_for_VXSNAN(dfp);
    dfp_check_for_VXISI_add(dfp);
}

DFP_HELPER_TAB(dadd, decNumberAdd, ADD_PPs, 64)
DFP_HELPER_TAB(daddq, decNumberAdd, ADD_PPs, 128)

static void SUB_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_OX(dfp);
    dfp_check_for_UX(dfp);
    dfp_check_for_XX(dfp);
    dfp_check_for_VXSNAN(dfp);
    dfp_check_for_VXISI_subtract(dfp);
}

DFP_HELPER_TAB(dsub, decNumberSubtract, SUB_PPs, 64)
DFP_HELPER_TAB(dsubq, decNumberSubtract, SUB_PPs, 128)

static void MUL_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_OX(dfp);
    dfp_check_for_UX(dfp);
    dfp_check_for_XX(dfp);
    dfp_check_for_VXSNAN(dfp);
    dfp_check_for_VXIMZ(dfp);
}

DFP_HELPER_TAB(dmul, decNumberMultiply, MUL_PPs, 64)
DFP_HELPER_TAB(dmulq, decNumberMultiply, MUL_PPs, 128)

static void DIV_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_OX(dfp);
    dfp_check_for_UX(dfp);
    dfp_check_for_ZX(dfp);
    dfp_check_for_XX(dfp);
    dfp_check_for_VXSNAN(dfp);
    dfp_check_for_VXZDZ(dfp);
    dfp_check_for_VXIDI(dfp);
}

DFP_HELPER_TAB(ddiv, decNumberDivide, DIV_PPs, 64)
DFP_HELPER_TAB(ddivq, decNumberDivide, DIV_PPs, 128)

#define DFP_HELPER_BF_AB(op, dnop, postprocs, size)                            \
uint32_t helper_##op(CPUPPCState *env, ppc_fprp_t *a, ppc_fprp_t *b)           \
{                                                                              \
    struct PPC_DFP dfp;                                                        \
    dfp_prepare_decimal##size(&dfp, a, b, env);                                \
    dnop(&dfp.t, &dfp.a, &dfp.b, &dfp.context);                                \
    dfp_finalize_decimal##size(&dfp);                                          \
    postprocs(&dfp);                                                           \
    return dfp.crbf;                                                           \
}

static void CMPU_PPs(struct PPC_DFP *dfp)
{
    dfp_set_CRBF_from_T(dfp);
    dfp_set_FPCC_from_CRBF(dfp);
    dfp_check_for_VXSNAN(dfp);
}

DFP_HELPER_BF_AB(dcmpu, decNumberCompare, CMPU_PPs, 64)
DFP_HELPER_BF_AB(dcmpuq, decNumberCompare, CMPU_PPs, 128)

static void CMPO_PPs(struct PPC_DFP *dfp)
{
    dfp_set_CRBF_from_T(dfp);
    dfp_set_FPCC_from_CRBF(dfp);
    dfp_check_for_VXSNAN(dfp);
    dfp_check_for_VXVC(dfp);
}

DFP_HELPER_BF_AB(dcmpo, decNumberCompare, CMPO_PPs, 64)
DFP_HELPER_BF_AB(dcmpoq, decNumberCompare, CMPO_PPs, 128)

#define DFP_HELPER_TSTDC(op, size)                                       \
uint32_t helper_##op(CPUPPCState *env, ppc_fprp_t *a, uint32_t dcm)      \
{                                                                        \
    struct PPC_DFP dfp;                                                  \
    int match = 0;                                                       \
                                                                         \
    dfp_prepare_decimal##size(&dfp, a, 0, env);                          \
                                                                         \
    match |= (dcm & 0x20) && decNumberIsZero(&dfp.a);                    \
    match |= (dcm & 0x10) && decNumberIsSubnormal(&dfp.a, &dfp.context); \
    match |= (dcm & 0x08) && decNumberIsNormal(&dfp.a, &dfp.context);    \
    match |= (dcm & 0x04) && decNumberIsInfinite(&dfp.a);                \
    match |= (dcm & 0x02) && decNumberIsQNaN(&dfp.a);                    \
    match |= (dcm & 0x01) && decNumberIsSNaN(&dfp.a);                    \
                                                                         \
    if (decNumberIsNegative(&dfp.a)) {                                   \
        dfp.crbf = match ? 0xA : 0x8;                                    \
    } else {                                                             \
        dfp.crbf = match ? 0x2 : 0x0;                                    \
    }                                                                    \
                                                                         \
    dfp_set_FPCC_from_CRBF(&dfp);                                        \
    return dfp.crbf;                                                     \
}

DFP_HELPER_TSTDC(dtstdc, 64)
DFP_HELPER_TSTDC(dtstdcq, 128)

#define DFP_HELPER_TSTDG(op, size)                                       \
uint32_t helper_##op(CPUPPCState *env, ppc_fprp_t *a, uint32_t dcm)      \
{                                                                        \
    struct PPC_DFP dfp;                                                  \
    int minexp, maxexp, nzero_digits, nzero_idx, is_negative, is_zero,   \
        is_extreme_exp, is_subnormal, is_normal, leftmost_is_nonzero,    \
        match;                                                           \
                                                                         \
    dfp_prepare_decimal##size(&dfp, a, 0, env);                          \
                                                                         \
    if ((size) == 64) {                                                  \
        minexp = -398;                                                   \
        maxexp = 369;                                                    \
        nzero_digits = 16;                                               \
        nzero_idx = 5;                                                   \
    } else if ((size) == 128) {                                          \
        minexp = -6176;                                                  \
        maxexp = 6111;                                                   \
        nzero_digits = 34;                                               \
        nzero_idx = 11;                                                  \
    }                                                                    \
                                                                         \
    is_negative = decNumberIsNegative(&dfp.a);                           \
    is_zero = decNumberIsZero(&dfp.a);                                   \
    is_extreme_exp = (dfp.a.exponent == maxexp) ||                       \
                     (dfp.a.exponent == minexp);                         \
    is_subnormal = decNumberIsSubnormal(&dfp.a, &dfp.context);           \
    is_normal = decNumberIsNormal(&dfp.a, &dfp.context);                 \
    leftmost_is_nonzero = (dfp.a.digits == nzero_digits) &&              \
                          (dfp.a.lsu[nzero_idx] != 0);                   \
    match = 0;                                                           \
                                                                         \
    match |= (dcm & 0x20) && is_zero && !is_extreme_exp;                 \
    match |= (dcm & 0x10) && is_zero && is_extreme_exp;                  \
    match |= (dcm & 0x08) &&                                             \
             (is_subnormal || (is_normal && is_extreme_exp));            \
    match |= (dcm & 0x04) && is_normal && !is_extreme_exp &&             \
             !leftmost_is_nonzero;                                       \
    match |= (dcm & 0x02) && is_normal && !is_extreme_exp &&             \
             leftmost_is_nonzero;                                        \
    match |= (dcm & 0x01) && decNumberIsSpecial(&dfp.a);                 \
                                                                         \
    if (is_negative) {                                                   \
        dfp.crbf = match ? 0xA : 0x8;                                    \
    } else {                                                             \
        dfp.crbf = match ? 0x2 : 0x0;                                    \
    }                                                                    \
                                                                         \
    dfp_set_FPCC_from_CRBF(&dfp);                                        \
    return dfp.crbf;                                                     \
}

DFP_HELPER_TSTDG(dtstdg, 64)
DFP_HELPER_TSTDG(dtstdgq, 128)

#define DFP_HELPER_TSTEX(op, size)                                       \
uint32_t helper_##op(CPUPPCState *env, ppc_fprp_t *a, ppc_fprp_t *b)     \
{                                                                        \
    struct PPC_DFP dfp;                                                  \
    int expa, expb, a_is_special, b_is_special;                          \
                                                                         \
    dfp_prepare_decimal##size(&dfp, a, b, env);                          \
                                                                         \
    expa = dfp.a.exponent;                                               \
    expb = dfp.b.exponent;                                               \
    a_is_special = decNumberIsSpecial(&dfp.a);                           \
    b_is_special = decNumberIsSpecial(&dfp.b);                           \
                                                                         \
    if (a_is_special || b_is_special) {                                  \
        int atype = a_is_special ? (decNumberIsNaN(&dfp.a) ? 4 : 2) : 1; \
        int btype = b_is_special ? (decNumberIsNaN(&dfp.b) ? 4 : 2) : 1; \
        dfp.crbf = (atype ^ btype) ? 0x1 : 0x2;                          \
    } else if (expa < expb) {                                            \
        dfp.crbf = 0x8;                                                  \
    } else if (expa > expb) {                                            \
        dfp.crbf = 0x4;                                                  \
    } else {                                                             \
        dfp.crbf = 0x2;                                                  \
    }                                                                    \
                                                                         \
    dfp_set_FPCC_from_CRBF(&dfp);                                        \
    return dfp.crbf;                                                     \
}

DFP_HELPER_TSTEX(dtstex, 64)
DFP_HELPER_TSTEX(dtstexq, 128)

#define DFP_HELPER_TSTSF(op, size)                                       \
uint32_t helper_##op(CPUPPCState *env, ppc_fprp_t *a, ppc_fprp_t *b)     \
{                                                                        \
    struct PPC_DFP dfp;                                                  \
    unsigned k;                                                          \
    ppc_vsr_t va;                                                        \
                                                                         \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                          \
                                                                         \
    get_dfp64(&va, a);                                                   \
    k = va.VsrD(1) & 0x3F;                                               \
                                                                         \
    if (unlikely(decNumberIsSpecial(&dfp.b))) {                          \
        dfp.crbf = 1;                                                    \
    } else if (k == 0) {                                                 \
        dfp.crbf = 4;                                                    \
    } else if (unlikely(decNumberIsZero(&dfp.b))) {                      \
        /* Zero has no sig digits */                                     \
        dfp.crbf = 4;                                                    \
    } else {                                                             \
        unsigned nsd = dfp.b.digits;                                     \
        if (k < nsd) {                                                   \
            dfp.crbf = 8;                                                \
        } else if (k > nsd) {                                            \
            dfp.crbf = 4;                                                \
        } else {                                                         \
            dfp.crbf = 2;                                                \
        }                                                                \
    }                                                                    \
                                                                         \
    dfp_set_FPCC_from_CRBF(&dfp);                                        \
    return dfp.crbf;                                                     \
}

DFP_HELPER_TSTSF(dtstsf, 64)
DFP_HELPER_TSTSF(dtstsfq, 128)

#define DFP_HELPER_TSTSFI(op, size)                                     \
uint32_t helper_##op(CPUPPCState *env, uint32_t a, ppc_fprp_t *b)       \
{                                                                       \
    struct PPC_DFP dfp;                                                 \
    unsigned uim;                                                       \
                                                                        \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                         \
                                                                        \
    uim = a & 0x3F;                                                     \
                                                                        \
    if (unlikely(decNumberIsSpecial(&dfp.b))) {                         \
        dfp.crbf = 1;                                                   \
    } else if (uim == 0) {                                              \
        dfp.crbf = 4;                                                   \
    } else if (unlikely(decNumberIsZero(&dfp.b))) {                     \
        /* Zero has no sig digits */                                    \
        dfp.crbf = 4;                                                   \
    } else {                                                            \
        unsigned nsd = dfp.b.digits;                                    \
        if (uim < nsd) {                                                \
            dfp.crbf = 8;                                               \
        } else if (uim > nsd) {                                         \
            dfp.crbf = 4;                                               \
        } else {                                                        \
            dfp.crbf = 2;                                               \
        }                                                               \
    }                                                                   \
                                                                        \
    dfp_set_FPCC_from_CRBF(&dfp);                                       \
    return dfp.crbf;                                                    \
}

DFP_HELPER_TSTSFI(dtstsfi, 64)
DFP_HELPER_TSTSFI(dtstsfiq, 128)

static void QUA_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_XX(dfp);
    dfp_check_for_VXSNAN(dfp);
    dfp_check_for_VXCVI(dfp);
}

static void dfp_quantize(uint8_t rmc, struct PPC_DFP *dfp)
{
    dfp_set_round_mode_from_immediate(0, rmc, dfp);
    decNumberQuantize(&dfp->t, &dfp->b, &dfp->a, &dfp->context);
    if (decNumberIsSNaN(&dfp->a)) {
        dfp->t = dfp->a;
        dfp_makeQNaN(&dfp->t);
    } else if (decNumberIsSNaN(&dfp->b)) {
        dfp->t = dfp->b;
        dfp_makeQNaN(&dfp->t);
    } else if (decNumberIsQNaN(&dfp->a)) {
        dfp->t = dfp->a;
    } else if (decNumberIsQNaN(&dfp->b)) {
        dfp->t = dfp->b;
    }
}

#define DFP_HELPER_QUAI(op, size)                                       \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b,        \
                 uint32_t te, uint32_t rmc)                             \
{                                                                       \
    struct PPC_DFP dfp;                                                 \
                                                                        \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                         \
                                                                        \
    decNumberFromUInt32(&dfp.a, 1);                                     \
    dfp.a.exponent = (int32_t)((int8_t)(te << 3) >> 3);                 \
                                                                        \
    dfp_quantize(rmc, &dfp);                                            \
    dfp_finalize_decimal##size(&dfp);                                   \
    QUA_PPs(&dfp);                                                      \
                                                                        \
    set_dfp##size(t, &dfp.vt);                                          \
}

DFP_HELPER_QUAI(dquai, 64)
DFP_HELPER_QUAI(dquaiq, 128)

#define DFP_HELPER_QUA(op, size)                                        \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *a,        \
                 ppc_fprp_t *b, uint32_t rmc)                           \
{                                                                       \
    struct PPC_DFP dfp;                                                 \
                                                                        \
    dfp_prepare_decimal##size(&dfp, a, b, env);                         \
                                                                        \
    dfp_quantize(rmc, &dfp);                                            \
    dfp_finalize_decimal##size(&dfp);                                   \
    QUA_PPs(&dfp);                                                      \
                                                                        \
    set_dfp##size(t, &dfp.vt);                                          \
}

DFP_HELPER_QUA(dqua, 64)
DFP_HELPER_QUA(dquaq, 128)

static void _dfp_reround(uint8_t rmc, int32_t ref_sig, int32_t xmax,
                             struct PPC_DFP *dfp)
{
    int msd_orig, msd_rslt;

    if (unlikely((ref_sig == 0) || (dfp->b.digits <= ref_sig))) {
        dfp->t = dfp->b;
        if (decNumberIsSNaN(&dfp->b)) {
            dfp_makeQNaN(&dfp->t);
            dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXSNAN, FPSCR_VE);
        }
        return;
    }

    /* Reround is equivalent to quantizing b with 1**E(n) where */
    /* n = exp(b) + numDigits(b) - reference_significance.      */

    decNumberFromUInt32(&dfp->a, 1);
    dfp->a.exponent = dfp->b.exponent + dfp->b.digits - ref_sig;

    if (unlikely(dfp->a.exponent > xmax)) {
        dfp->t.digits = 0;
        dfp->t.bits &= ~DECNEG;
        dfp_makeQNaN(&dfp->t);
        dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXCVI, FPSCR_VE);
        return;
    }

    dfp_quantize(rmc, dfp);

    msd_orig = dfp_get_digit(&dfp->b, dfp->b.digits-1);
    msd_rslt = dfp_get_digit(&dfp->t, dfp->t.digits-1);

    /* If the quantization resulted in rounding up to the next magnitude, */
    /* then we need to shift the significand and adjust the exponent.     */

    if (unlikely((msd_orig == 9) && (msd_rslt == 1))) {

        decNumber negone;

        decNumberFromInt32(&negone, -1);
        decNumberShift(&dfp->t, &dfp->t, &negone, &dfp->context);
        dfp->t.exponent++;

        if (unlikely(dfp->t.exponent > xmax)) {
            dfp_makeQNaN(&dfp->t);
            dfp->t.digits = 0;
            dfp_set_FPSCR_flag(dfp, FP_VX | FP_VXCVI, FP_VE);
            /* Inhibit XX in this case */
            decContextClearStatus(&dfp->context, DEC_Inexact);
        }
    }
}

#define DFP_HELPER_RRND(op, size)                                       \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *a,        \
                 ppc_fprp_t *b, uint32_t rmc)                           \
{                                                                       \
    struct PPC_DFP dfp;                                                 \
    ppc_vsr_t va;                                                       \
    int32_t ref_sig;                                                    \
    int32_t xmax = ((size) == 64) ? 369 : 6111;                         \
                                                                        \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                         \
                                                                        \
    get_dfp64(&va, a);                                                  \
    ref_sig = va.VsrD(1) & 0x3f;                                        \
                                                                        \
    _dfp_reround(rmc, ref_sig, xmax, &dfp);                             \
    dfp_finalize_decimal##size(&dfp);                                   \
    QUA_PPs(&dfp);                                                      \
                                                                        \
    set_dfp##size(t, &dfp.vt);                                          \
}

DFP_HELPER_RRND(drrnd, 64)
DFP_HELPER_RRND(drrndq, 128)

#define DFP_HELPER_RINT(op, postprocs, size)                                   \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b,               \
             uint32_t r, uint32_t rmc)                                         \
{                                                                              \
    struct PPC_DFP dfp;                                                        \
                                                                               \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                                \
                                                                               \
    dfp_set_round_mode_from_immediate(r, rmc, &dfp);                           \
    decNumberToIntegralExact(&dfp.t, &dfp.b, &dfp.context);                    \
    dfp_finalize_decimal##size(&dfp);                                          \
    postprocs(&dfp);                                                           \
                                                                               \
    set_dfp##size(t, &dfp.vt);                                                 \
}

static void RINTX_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_XX(dfp);
    dfp_check_for_VXSNAN(dfp);
}

DFP_HELPER_RINT(drintx, RINTX_PPs, 64)
DFP_HELPER_RINT(drintxq, RINTX_PPs, 128)

static void RINTN_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_VXSNAN(dfp);
}

DFP_HELPER_RINT(drintn, RINTN_PPs, 64)
DFP_HELPER_RINT(drintnq, RINTN_PPs, 128)

void helper_dctdp(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b)
{
    struct PPC_DFP dfp;
    ppc_vsr_t vb;
    uint32_t b_short;

    get_dfp64(&vb, b);
    b_short = (uint32_t)vb.VsrD(1);

    dfp_prepare_decimal64(&dfp, 0, 0, env);
    decimal32ToNumber((decimal32 *)&b_short, &dfp.t);
    dfp_finalize_decimal64(&dfp);
    set_dfp64(t, &dfp.vt);
    dfp_set_FPRF_from_FRT(&dfp);
}

void helper_dctqpq(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b)
{
    struct PPC_DFP dfp;
    ppc_vsr_t vb;
    dfp_prepare_decimal128(&dfp, 0, 0, env);
    get_dfp64(&vb, b);
    decimal64ToNumber((decimal64 *)&vb.VsrD(1), &dfp.t);

    dfp_check_for_VXSNAN_and_convert_to_QNaN(&dfp);
    dfp_set_FPRF_from_FRT(&dfp);

    dfp_finalize_decimal128(&dfp);
    set_dfp128(t, &dfp.vt);
}

void helper_drsp(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b)
{
    struct PPC_DFP dfp;
    uint32_t t_short = 0;
    ppc_vsr_t vt;
    dfp_prepare_decimal64(&dfp, 0, b, env);
    decimal32FromNumber((decimal32 *)&t_short, &dfp.b, &dfp.context);
    decimal32ToNumber((decimal32 *)&t_short, &dfp.t);

    dfp_set_FPRF_from_FRT_short(&dfp);
    dfp_check_for_OX(&dfp);
    dfp_check_for_UX(&dfp);
    dfp_check_for_XX(&dfp);

    vt.VsrD(1) = (uint64_t)t_short;
    set_dfp64(t, &vt);
}

void helper_drdpq(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b)
{
    struct PPC_DFP dfp;
    dfp_prepare_decimal128(&dfp, 0, b, env);
    decimal64FromNumber((decimal64 *)&dfp.vt.VsrD(1), &dfp.b, &dfp.context);
    decimal64ToNumber((decimal64 *)&dfp.vt.VsrD(1), &dfp.t);

    dfp_check_for_VXSNAN_and_convert_to_QNaN(&dfp);
    dfp_set_FPRF_from_FRT_long(&dfp);
    dfp_check_for_OX(&dfp);
    dfp_check_for_UX(&dfp);
    dfp_check_for_XX(&dfp);

    dfp.vt.VsrD(0) = dfp.vt.VsrD(1) = 0;
    dfp_finalize_decimal64(&dfp);
    set_dfp128(t, &dfp.vt);
}

#define DFP_HELPER_CFFIX(op, size)                                             \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b)               \
{                                                                              \
    struct PPC_DFP dfp;                                                        \
    ppc_vsr_t vb;                                                              \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                                \
    get_dfp64(&vb, b);                                                         \
    decNumberFromInt64(&dfp.t, (int64_t)vb.VsrD(1));                           \
    dfp_finalize_decimal##size(&dfp);                                          \
    CFFIX_PPs(&dfp);                                                           \
                                                                               \
    set_dfp##size(t, &dfp.vt);                                                 \
}

static void CFFIX_PPs(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT(dfp);
    dfp_check_for_XX(dfp);
}

DFP_HELPER_CFFIX(dcffix, 64)
DFP_HELPER_CFFIX(dcffixq, 128)

#define DFP_HELPER_CTFIX(op, size)                                            \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b)              \
{                                                                             \
    struct PPC_DFP dfp;                                                       \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                               \
                                                                              \
    if (unlikely(decNumberIsSpecial(&dfp.b))) {                               \
        uint64_t invalid_flags = FP_VX | FP_VXCVI;                            \
        if (decNumberIsInfinite(&dfp.b)) {                                    \
            dfp.vt.VsrD(1) = decNumberIsNegative(&dfp.b) ? INT64_MIN :        \
                                                           INT64_MAX;         \
        } else { /* NaN */                                                    \
            dfp.vt.VsrD(1) = INT64_MIN;                                       \
            if (decNumberIsSNaN(&dfp.b)) {                                    \
                invalid_flags |= FP_VXSNAN;                                   \
            }                                                                 \
        }                                                                     \
        dfp_set_FPSCR_flag(&dfp, invalid_flags, FP_VE);                       \
    } else if (unlikely(decNumberIsZero(&dfp.b))) {                           \
        dfp.vt.VsrD(1) = 0;                                                   \
    } else {                                                                  \
        decNumberToIntegralExact(&dfp.b, &dfp.b, &dfp.context);               \
        dfp.vt.VsrD(1) = decNumberIntegralToInt64(&dfp.b, &dfp.context);      \
        if (decContextTestStatus(&dfp.context, DEC_Invalid_operation)) {      \
            dfp.vt.VsrD(1) = decNumberIsNegative(&dfp.b) ? INT64_MIN :        \
                                                           INT64_MAX;         \
            dfp_set_FPSCR_flag(&dfp, FP_VX | FP_VXCVI, FP_VE);                \
        } else {                                                              \
            dfp_check_for_XX(&dfp);                                           \
        }                                                                     \
    }                                                                         \
                                                                              \
    set_dfp64(t, &dfp.vt);                                                    \
}

DFP_HELPER_CTFIX(dctfix, 64)
DFP_HELPER_CTFIX(dctfixq, 128)

static inline void dfp_set_bcd_digit_64(ppc_vsr_t *t, uint8_t digit,
                                        unsigned n)
{
    t->VsrD(1) |= ((uint64_t)(digit & 0xF) << (n << 2));
}

static inline void dfp_set_bcd_digit_128(ppc_vsr_t *t, uint8_t digit,
                                         unsigned n)
{
    t->VsrD((n & 0x10) ? 0 : 1) |=
        ((uint64_t)(digit & 0xF) << ((n & 15) << 2));
}

static inline void dfp_set_sign_64(ppc_vsr_t *t, uint8_t sgn)
{
    t->VsrD(1) <<= 4;
    t->VsrD(1) |= (sgn & 0xF);
}

static inline void dfp_set_sign_128(ppc_vsr_t *t, uint8_t sgn)
{
    t->VsrD(0) <<= 4;
    t->VsrD(0) |= (t->VsrD(1) >> 60);
    t->VsrD(1) <<= 4;
    t->VsrD(1) |= (sgn & 0xF);
}

#define DFP_HELPER_DEDPD(op, size)                                        \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b,          \
                 uint32_t sp)                                             \
{                                                                         \
    struct PPC_DFP dfp;                                                   \
    uint8_t digits[34];                                                   \
    int i, N;                                                             \
                                                                          \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                           \
                                                                          \
    decNumberGetBCD(&dfp.b, digits);                                      \
    dfp.vt.VsrD(0) = dfp.vt.VsrD(1) = 0;                                  \
    N = dfp.b.digits;                                                     \
                                                                          \
    for (i = 0; (i < N) && (i < (size)/4); i++) {                         \
        dfp_set_bcd_digit_##size(&dfp.vt, digits[N - i - 1], i);          \
    }                                                                     \
                                                                          \
    if (sp & 2) {                                                         \
        uint8_t sgn;                                                      \
                                                                          \
        if (decNumberIsNegative(&dfp.b)) {                                \
            sgn = 0xD;                                                    \
        } else {                                                          \
            sgn = ((sp & 1) ? 0xF : 0xC);                                 \
        }                                                                 \
        dfp_set_sign_##size(&dfp.vt, sgn);                                \
    }                                                                     \
                                                                          \
    set_dfp##size(t, &dfp.vt);                                            \
}

DFP_HELPER_DEDPD(ddedpd, 64)
DFP_HELPER_DEDPD(ddedpdq, 128)

static inline uint8_t dfp_get_bcd_digit_64(ppc_vsr_t *t, unsigned n)
{
    return t->VsrD(1) >> ((n << 2) & 63) & 15;
}

static inline uint8_t dfp_get_bcd_digit_128(ppc_vsr_t *t, unsigned n)
{
    return t->VsrD((n & 0x10) ? 0 : 1) >> ((n << 2) & 63) & 15;
}

#define DFP_HELPER_ENBCD(op, size)                                           \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b,             \
                 uint32_t s)                                                 \
{                                                                            \
    struct PPC_DFP dfp;                                                      \
    uint8_t digits[32];                                                      \
    int n = 0, offset = 0, sgn = 0, nonzero = 0;                             \
                                                                             \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                              \
                                                                             \
    decNumberZero(&dfp.t);                                                   \
                                                                             \
    if (s) {                                                                 \
        uint8_t sgnNibble = dfp_get_bcd_digit_##size(&dfp.vb, offset++);     \
        switch (sgnNibble) {                                                 \
        case 0xD:                                                            \
        case 0xB:                                                            \
            sgn = 1;                                                         \
            break;                                                           \
        case 0xC:                                                            \
        case 0xF:                                                            \
        case 0xA:                                                            \
        case 0xE:                                                            \
            sgn = 0;                                                         \
            break;                                                           \
        default:                                                             \
            dfp_set_FPSCR_flag(&dfp, FP_VX | FP_VXCVI, FPSCR_VE);            \
            return;                                                          \
        }                                                                    \
        }                                                                    \
                                                                             \
    while (offset < (size) / 4) {                                            \
        n++;                                                                 \
        digits[(size) / 4 - n] = dfp_get_bcd_digit_##size(&dfp.vb,           \
                                                          offset++);         \
        if (digits[(size) / 4 - n] > 10) {                                   \
            dfp_set_FPSCR_flag(&dfp, FP_VX | FP_VXCVI, FPSCR_VE);            \
            return;                                                          \
        } else {                                                             \
            nonzero |= (digits[(size) / 4 - n] > 0);                         \
        }                                                                    \
    }                                                                        \
                                                                             \
    if (nonzero) {                                                           \
        decNumberSetBCD(&dfp.t, digits + ((size) / 4) - n, n);               \
    }                                                                        \
                                                                             \
    if (s && sgn)  {                                                         \
        dfp.t.bits |= DECNEG;                                                \
    }                                                                        \
    dfp_finalize_decimal##size(&dfp);                                        \
    dfp_set_FPRF_from_FRT(&dfp);                                             \
    set_dfp##size(t, &dfp.vt);                                               \
}

DFP_HELPER_ENBCD(denbcd, 64)
DFP_HELPER_ENBCD(denbcdq, 128)

#define DFP_HELPER_XEX(op, size)                               \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *b) \
{                                                              \
    struct PPC_DFP dfp;                                        \
    ppc_vsr_t vt;                                              \
                                                               \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                \
                                                               \
    if (unlikely(decNumberIsSpecial(&dfp.b))) {                \
        if (decNumberIsInfinite(&dfp.b)) {                     \
            vt.VsrD(1) = -1;                                   \
        } else if (decNumberIsSNaN(&dfp.b)) {                  \
            vt.VsrD(1) = -3;                                   \
        } else if (decNumberIsQNaN(&dfp.b)) {                  \
            vt.VsrD(1) = -2;                                   \
        } else {                                               \
            assert(0);                                         \
        }                                                      \
        set_dfp64(t, &vt);                                     \
    } else {                                                   \
        if ((size) == 64) {                                    \
            vt.VsrD(1) = dfp.b.exponent + 398;                 \
        } else if ((size) == 128) {                            \
            vt.VsrD(1) = dfp.b.exponent + 6176;                \
        } else {                                               \
            assert(0);                                         \
        }                                                      \
        set_dfp64(t, &vt);                                     \
    }                                                          \
}

DFP_HELPER_XEX(dxex, 64)
DFP_HELPER_XEX(dxexq, 128)

static void dfp_set_raw_exp_64(ppc_vsr_t *t, uint64_t raw)
{
    t->VsrD(1) &= 0x8003ffffffffffffULL;
    t->VsrD(1) |= (raw << (63 - 13));
}

static void dfp_set_raw_exp_128(ppc_vsr_t *t, uint64_t raw)
{
    t->VsrD(0) &= 0x80003fffffffffffULL;
    t->VsrD(0) |= (raw << (63 - 17));
}

#define DFP_HELPER_IEX(op, size)                                          \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *a,          \
                 ppc_fprp_t *b)                                           \
{                                                                         \
    struct PPC_DFP dfp;                                                   \
    uint64_t raw_qnan, raw_snan, raw_inf, max_exp;                        \
    ppc_vsr_t va;                                                         \
    int bias;                                                             \
    int64_t exp;                                                          \
                                                                          \
    get_dfp64(&va, a);                                                    \
    exp = (int64_t)va.VsrD(1);                                            \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                           \
                                                                          \
    if ((size) == 64) {                                                   \
        max_exp = 767;                                                    \
        raw_qnan = 0x1F00;                                                \
        raw_snan = 0x1F80;                                                \
        raw_inf = 0x1E00;                                                 \
        bias = 398;                                                       \
    } else if ((size) == 128) {                                           \
        max_exp = 12287;                                                  \
        raw_qnan = 0x1f000;                                               \
        raw_snan = 0x1f800;                                               \
        raw_inf = 0x1e000;                                                \
        bias = 6176;                                                      \
    } else {                                                              \
        assert(0);                                                        \
    }                                                                     \
                                                                          \
    if (unlikely((exp < 0) || (exp > max_exp))) {                         \
        dfp.vt.VsrD(0) = dfp.vb.VsrD(0);                                  \
        dfp.vt.VsrD(1) = dfp.vb.VsrD(1);                                  \
        if (exp == -1) {                                                  \
            dfp_set_raw_exp_##size(&dfp.vt, raw_inf);                     \
        } else if (exp == -3) {                                           \
            dfp_set_raw_exp_##size(&dfp.vt, raw_snan);                    \
        } else {                                                          \
            dfp_set_raw_exp_##size(&dfp.vt, raw_qnan);                    \
        }                                                                 \
    } else {                                                              \
        dfp.t = dfp.b;                                                    \
        if (unlikely(decNumberIsSpecial(&dfp.t))) {                       \
            dfp.t.bits &= ~DECSPECIAL;                                    \
        }                                                                 \
        dfp.t.exponent = exp - bias;                                      \
        dfp_finalize_decimal##size(&dfp);                                 \
    }                                                                     \
    set_dfp##size(t, &dfp.vt);                                            \
}

DFP_HELPER_IEX(diex, 64)
DFP_HELPER_IEX(diexq, 128)

static void dfp_clear_lmd_from_g5msb(uint64_t *t)
{

    /* The most significant 5 bits of the PowerPC DFP format combine bits  */
    /* from the left-most decimal digit (LMD) and the biased exponent.     */
    /* This  routine clears the LMD bits while preserving the exponent     */
    /*  bits.  See "Figure 80: Encoding of bits 0:4 of the G field for     */
    /*  Finite Numbers" in the Power ISA for additional details.           */

    uint64_t g5msb = (*t >> 58) & 0x1F;

    if ((g5msb >> 3) < 3) { /* LMD in [0-7] ? */
       *t &= ~(7ULL << 58);
    } else {
       switch (g5msb & 7) {
       case 0:
       case 1:
           g5msb = 0;
           break;
       case 2:
       case 3:
           g5msb = 0x8;
           break;
       case 4:
       case 5:
           g5msb = 0x10;
           break;
       case 6:
           g5msb = 0x1E;
           break;
       case 7:
           g5msb = 0x1F;
           break;
       }

        *t &= ~(0x1fULL << 58);
        *t |= (g5msb << 58);
    }
}

#define DFP_HELPER_SHIFT(op, size, shift_left)                      \
void helper_##op(CPUPPCState *env, ppc_fprp_t *t, ppc_fprp_t *a,    \
                 uint32_t sh)                                       \
{                                                                   \
    struct PPC_DFP dfp;                                             \
    unsigned max_digits = ((size) == 64) ? 16 : 34;                 \
                                                                    \
    dfp_prepare_decimal##size(&dfp, a, 0, env);                     \
                                                                    \
    if (sh <= max_digits) {                                         \
                                                                    \
        decNumber shd;                                              \
        unsigned special = dfp.a.bits & DECSPECIAL;                 \
                                                                    \
        if (shift_left) {                                           \
            decNumberFromUInt32(&shd, sh);                          \
        } else {                                                    \
            decNumberFromInt32(&shd, -((int32_t)sh));               \
        }                                                           \
                                                                    \
        dfp.a.bits &= ~DECSPECIAL;                                  \
        decNumberShift(&dfp.t, &dfp.a, &shd, &dfp.context);         \
                                                                    \
        dfp.t.bits |= special;                                      \
        if (special && (dfp.t.digits >= max_digits)) {              \
            dfp.t.digits = max_digits - 1;                          \
        }                                                           \
                                                                    \
        dfp_finalize_decimal##size(&dfp);                           \
    } else {                                                        \
        if ((size) == 64) {                                         \
            dfp.vt.VsrD(1) = dfp.va.VsrD(1) &                       \
                             0xFFFC000000000000ULL;                 \
            dfp_clear_lmd_from_g5msb(&dfp.vt.VsrD(1));              \
        } else {                                                    \
            dfp.vt.VsrD(0) = dfp.va.VsrD(0) &                       \
                             0xFFFFC00000000000ULL;                 \
            dfp_clear_lmd_from_g5msb(&dfp.vt.VsrD(0));              \
            dfp.vt.VsrD(1) = 0;                                     \
        }                                                           \
    }                                                               \
                                                                    \
    set_dfp##size(t, &dfp.vt);                                      \
}

DFP_HELPER_SHIFT(dscli, 64, 1)
DFP_HELPER_SHIFT(dscliq, 128, 1)
DFP_HELPER_SHIFT(dscri, 64, 0)
DFP_HELPER_SHIFT(dscriq, 128, 0)
