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

#include "cpu.h"
#include "exec/helper-proto.h"

#define DECNUMDIGITS 34
#include "libdecnumber/decContext.h"
#include "libdecnumber/decNumber.h"
#include "libdecnumber/dpd/decimal32.h"
#include "libdecnumber/dpd/decimal64.h"
#include "libdecnumber/dpd/decimal128.h"

#if defined(HOST_WORDS_BIGENDIAN)
#define HI_IDX 0
#define LO_IDX 1
#else
#define HI_IDX 1
#define LO_IDX 0
#endif

struct PPC_DFP {
    CPUPPCState *env;
    uint64_t t64[2], a64[2], b64[2];
    decNumber t, a, b;
    decContext context;
    uint8_t crbf;
};

static void dfp_prepare_rounding_mode(decContext *context, uint64_t fpscr)
{
    enum rounding rnd;

    switch ((fpscr >> 32) & 0x7) {
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

static void dfp_prepare_decimal64(struct PPC_DFP *dfp, uint64_t *a,
                uint64_t *b, CPUPPCState *env)
{
    decContextDefault(&dfp->context, DEC_INIT_DECIMAL64);
    dfp_prepare_rounding_mode(&dfp->context, env->fpscr);
    dfp->env = env;

    if (a) {
        dfp->a64[0] = *a;
        decimal64ToNumber((decimal64 *)dfp->a64, &dfp->a);
    } else {
        dfp->a64[0] = 0;
        decNumberZero(&dfp->a);
    }

    if (b) {
        dfp->b64[0] = *b;
        decimal64ToNumber((decimal64 *)dfp->b64, &dfp->b);
    } else {
        dfp->b64[0] = 0;
        decNumberZero(&dfp->b);
    }
}

static void dfp_prepare_decimal128(struct PPC_DFP *dfp, uint64_t *a,
                uint64_t *b, CPUPPCState *env)
{
    decContextDefault(&dfp->context, DEC_INIT_DECIMAL128);
    dfp_prepare_rounding_mode(&dfp->context, env->fpscr);
    dfp->env = env;

    if (a) {
        dfp->a64[0] = a[HI_IDX];
        dfp->a64[1] = a[LO_IDX];
        decimal128ToNumber((decimal128 *)dfp->a64, &dfp->a);
    } else {
        dfp->a64[0] = dfp->a64[1] = 0;
        decNumberZero(&dfp->a);
    }

    if (b) {
        dfp->b64[0] = b[HI_IDX];
        dfp->b64[1] = b[LO_IDX];
        decimal128ToNumber((decimal128 *)dfp->b64, &dfp->b);
    } else {
        dfp->b64[0] = dfp->b64[1] = 0;
        decNumberZero(&dfp->b);
    }
}

#define FP_FX       (1ull << FPSCR_FX)
#define FP_FEX      (1ull << FPSCR_FEX)
#define FP_OX       (1ull << FPSCR_OX)
#define FP_OE       (1ull << FPSCR_OE)
#define FP_UX       (1ull << FPSCR_UX)
#define FP_UE       (1ull << FPSCR_UE)
#define FP_XX       (1ull << FPSCR_XX)
#define FP_XE       (1ull << FPSCR_XE)
#define FP_ZX       (1ull << FPSCR_ZX)
#define FP_ZE       (1ull << FPSCR_ZE)
#define FP_VX       (1ull << FPSCR_VX)
#define FP_VXSNAN   (1ull << FPSCR_VXSNAN)
#define FP_VXISI    (1ull << FPSCR_VXISI)
#define FP_VXIMZ    (1ull << FPSCR_VXIMZ)
#define FP_VXZDZ    (1ull << FPSCR_VXZDZ)
#define FP_VXIDI    (1ull << FPSCR_VXIDI)
#define FP_VXVC     (1ull << FPSCR_VXVC)
#define FP_VXCVI    (1ull << FPSCR_VXCVI)
#define FP_VE       (1ull << FPSCR_VE)
#define FP_FI       (1ull << FPSCR_FI)

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
    dfp->env->fpscr &= ~(0x1F << 12);
    dfp->env->fpscr |= (fprf << 12);
}

static void dfp_set_FPRF_from_FRT(struct PPC_DFP *dfp)
{
    dfp_set_FPRF_from_FRT_with_context(dfp, &dfp->context);
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
    dfp->env->fpscr &= ~(0xF << 12);
    dfp->env->fpscr |= (dfp->crbf << 12);
}

#define DFP_HELPER_TAB(op, dnop, postprocs, size)                              \
void helper_##op(CPUPPCState *env, uint64_t *t, uint64_t *a, uint64_t *b)      \
{                                                                              \
    struct PPC_DFP dfp;                                                        \
    dfp_prepare_decimal##size(&dfp, a, b, env);                                \
    dnop(&dfp.t, &dfp.a, &dfp.b, &dfp.context);                                \
    decimal##size##FromNumber((decimal##size *)dfp.t64, &dfp.t, &dfp.context); \
    postprocs(&dfp);                                                           \
    if (size == 64) {                                                          \
        t[0] = dfp.t64[0];                                                     \
    } else if (size == 128) {                                                  \
        t[0] = dfp.t64[HI_IDX];                                                \
        t[1] = dfp.t64[LO_IDX];                                                \
    }                                                                          \
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
uint32_t helper_##op(CPUPPCState *env, uint64_t *a, uint64_t *b)               \
{                                                                              \
    struct PPC_DFP dfp;                                                        \
    dfp_prepare_decimal##size(&dfp, a, b, env);                                \
    dnop(&dfp.t, &dfp.a, &dfp.b, &dfp.context);                                \
    decimal##size##FromNumber((decimal##size *)dfp.t64, &dfp.t, &dfp.context); \
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
uint32_t helper_##op(CPUPPCState *env, uint64_t *a, uint32_t dcm)        \
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
uint32_t helper_##op(CPUPPCState *env, uint64_t *a, uint32_t dcm)        \
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
uint32_t helper_##op(CPUPPCState *env, uint64_t *a, uint64_t *b)         \
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
uint32_t helper_##op(CPUPPCState *env, uint64_t *a, uint64_t *b)         \
{                                                                        \
    struct PPC_DFP dfp;                                                  \
    unsigned k;                                                          \
                                                                         \
    dfp_prepare_decimal##size(&dfp, 0, b, env);                          \
                                                                         \
    k = *a & 0x3F;                                                       \
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
