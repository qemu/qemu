/*
 *  m68k FPU helpers
 *
 *  Copyright (c) 2006-2007 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"

int32_t HELPER(reds32)(CPUM68KState *env, FPReg *val)
{
    return floatx80_to_int32(val->d, &env->fp_status);
}

float32 HELPER(redf32)(CPUM68KState *env, FPReg *val)
{
    return floatx80_to_float32(val->d, &env->fp_status);
}

void HELPER(exts32)(CPUM68KState *env, FPReg *res, int32_t val)
{
    res->d = int32_to_floatx80(val, &env->fp_status);
}

void HELPER(extf32)(CPUM68KState *env, FPReg *res, float32 val)
{
    res->d = float32_to_floatx80(val, &env->fp_status);
}

void HELPER(extf64)(CPUM68KState *env, FPReg *res, float64 val)
{
    res->d = float64_to_floatx80(val, &env->fp_status);
}

float64 HELPER(redf64)(CPUM68KState *env, FPReg *val)
{
    return floatx80_to_float64(val->d, &env->fp_status);
}

void HELPER(firound)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_round_to_int(val->d, &env->fp_status);
}

static void m68k_restore_precision_mode(CPUM68KState *env)
{
    switch (env->fpcr & FPCR_PREC_MASK) {
    case FPCR_PREC_X: /* extended */
        set_floatx80_rounding_precision(80, &env->fp_status);
        break;
    case FPCR_PREC_S: /* single */
        set_floatx80_rounding_precision(32, &env->fp_status);
        break;
    case FPCR_PREC_D: /* double */
        set_floatx80_rounding_precision(64, &env->fp_status);
        break;
    case FPCR_PREC_U: /* undefined */
    default:
        break;
    }
}

static void cf_restore_precision_mode(CPUM68KState *env)
{
    if (env->fpcr & FPCR_PREC_S) { /* single */
        set_floatx80_rounding_precision(32, &env->fp_status);
    } else { /* double */
        set_floatx80_rounding_precision(64, &env->fp_status);
    }
}

static void restore_rounding_mode(CPUM68KState *env)
{
    switch (env->fpcr & FPCR_RND_MASK) {
    case FPCR_RND_N: /* round to nearest */
        set_float_rounding_mode(float_round_nearest_even, &env->fp_status);
        break;
    case FPCR_RND_Z: /* round to zero */
        set_float_rounding_mode(float_round_to_zero, &env->fp_status);
        break;
    case FPCR_RND_M: /* round toward minus infinity */
        set_float_rounding_mode(float_round_down, &env->fp_status);
        break;
    case FPCR_RND_P: /* round toward positive infinity */
        set_float_rounding_mode(float_round_up, &env->fp_status);
        break;
    }
}

void cpu_m68k_set_fpcr(CPUM68KState *env, uint32_t val)
{
    env->fpcr = val & 0xffff;

    if (m68k_feature(env, M68K_FEATURE_CF_FPU)) {
        cf_restore_precision_mode(env);
    } else {
        m68k_restore_precision_mode(env);
    }
    restore_rounding_mode(env);
}

void HELPER(fitrunc)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    int rounding_mode = get_float_rounding_mode(&env->fp_status);
    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    res->d = floatx80_round_to_int(val->d, &env->fp_status);
    set_float_rounding_mode(rounding_mode, &env->fp_status);
}

void HELPER(set_fpcr)(CPUM68KState *env, uint32_t val)
{
    cpu_m68k_set_fpcr(env, val);
}

void HELPER(fsqrt)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_sqrt(val->d, &env->fp_status);
}

void HELPER(fabs)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_abs(val->d);
}

void HELPER(fchs)(CPUM68KState *env, FPReg *res, FPReg *val)
{
    res->d = floatx80_chs(val->d);
}

void HELPER(fadd)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_add(val0->d, val1->d, &env->fp_status);
}

void HELPER(fsub)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_sub(val1->d, val0->d, &env->fp_status);
}

void HELPER(fmul)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_mul(val0->d, val1->d, &env->fp_status);
}

void HELPER(fdiv)(CPUM68KState *env, FPReg *res, FPReg *val0, FPReg *val1)
{
    res->d = floatx80_div(val1->d, val0->d, &env->fp_status);
}

static int float_comp_to_cc(int float_compare)
{
    switch (float_compare) {
    case float_relation_equal:
        return FPSR_CC_Z;
    case float_relation_less:
        return FPSR_CC_N;
    case float_relation_unordered:
        return FPSR_CC_A;
    case float_relation_greater:
        return 0;
    default:
        g_assert_not_reached();
    }
}

void HELPER(fcmp)(CPUM68KState *env, FPReg *val0, FPReg *val1)
{
    int float_compare;

    float_compare = floatx80_compare(val1->d, val0->d, &env->fp_status);
    env->fpsr = (env->fpsr & ~FPSR_CC_MASK) | float_comp_to_cc(float_compare);
}

void HELPER(ftst)(CPUM68KState *env, FPReg *val)
{
    uint32_t cc = 0;

    if (floatx80_is_neg(val->d)) {
        cc |= FPSR_CC_N;
    }

    if (floatx80_is_any_nan(val->d)) {
        cc |= FPSR_CC_A;
    } else if (floatx80_is_infinity(val->d)) {
        cc |= FPSR_CC_I;
    } else if (floatx80_is_zero(val->d)) {
        cc |= FPSR_CC_Z;
    }
    env->fpsr = (env->fpsr & ~FPSR_CC_MASK) | cc;
}
