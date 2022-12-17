/*
 * Helpers for HPPA FPU instructions.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

void HELPER(loaded_fr0)(CPUHPPAState *env)
{
    uint32_t shadow = env->fr[0] >> 32;
    int rm, d;

    env->fr0_shadow = shadow;

    switch (extract32(shadow, 9, 2)) {
    default:
        rm = float_round_nearest_even;
        break;
    case 1:
        rm = float_round_to_zero;
        break;
    case 2:
        rm = float_round_up;
        break;
    case 3:
        rm = float_round_down;
        break;
    }
    set_float_rounding_mode(rm, &env->fp_status);

    d = extract32(shadow, 5, 1);
    set_flush_to_zero(d, &env->fp_status);
    set_flush_inputs_to_zero(d, &env->fp_status);
}

void cpu_hppa_loaded_fr0(CPUHPPAState *env)
{
    helper_loaded_fr0(env);
}

#define CONVERT_BIT(X, SRC, DST)        \
    ((SRC) > (DST)                      \
     ? (X) / ((SRC) / (DST)) & (DST)    \
     : ((X) & (SRC)) * ((DST) / (SRC)))

static void update_fr0_op(CPUHPPAState *env, uintptr_t ra)
{
    uint32_t soft_exp = get_float_exception_flags(&env->fp_status);
    uint32_t hard_exp = 0;
    uint32_t shadow = env->fr0_shadow;

    if (likely(soft_exp == 0)) {
        env->fr[0] = (uint64_t)shadow << 32;
        return;
    }
    set_float_exception_flags(0, &env->fp_status);

    hard_exp |= CONVERT_BIT(soft_exp, float_flag_inexact,   1u << 0);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_underflow, 1u << 1);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_overflow,  1u << 2);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_divbyzero, 1u << 3);
    hard_exp |= CONVERT_BIT(soft_exp, float_flag_invalid,   1u << 4);
    shadow |= hard_exp << (32 - 5);
    env->fr0_shadow = shadow;
    env->fr[0] = (uint64_t)shadow << 32;

    if (hard_exp & shadow) {
        hppa_dynamic_excp(env, EXCP_ASSIST, ra);
    }
}

float32 HELPER(fsqrt_s)(CPUHPPAState *env, float32 arg)
{
    float32 ret = float32_sqrt(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(frnd_s)(CPUHPPAState *env, float32 arg)
{
    float32 ret = float32_round_to_int(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fadd_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_add(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fsub_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_sub(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fmpy_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_mul(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fdiv_s)(CPUHPPAState *env, float32 a, float32 b)
{
    float32 ret = float32_div(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fsqrt_d)(CPUHPPAState *env, float64 arg)
{
    float64 ret = float64_sqrt(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(frnd_d)(CPUHPPAState *env, float64 arg)
{
    float64 ret = float64_round_to_int(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fadd_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_add(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fsub_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_sub(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fmpy_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_mul(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fdiv_d)(CPUHPPAState *env, float64 a, float64 b)
{
    float64 ret = float64_div(a, b, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_s_d)(CPUHPPAState *env, float32 arg)
{
    float64 ret = float32_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_d_s)(CPUHPPAState *env, float64 arg)
{
    float32 ret = float64_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_w_s)(CPUHPPAState *env, int32_t arg)
{
    float32 ret = int32_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_dw_s)(CPUHPPAState *env, int64_t arg)
{
    float32 ret = int64_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_w_d)(CPUHPPAState *env, int32_t arg)
{
    float64 ret = int32_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_dw_d)(CPUHPPAState *env, int64_t arg)
{
    float64 ret = int64_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_s_w)(CPUHPPAState *env, float32 arg)
{
    int32_t ret = float32_to_int32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_d_w)(CPUHPPAState *env, float64 arg)
{
    int32_t ret = float64_to_int32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_s_dw)(CPUHPPAState *env, float32 arg)
{
    int64_t ret = float32_to_int64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_d_dw)(CPUHPPAState *env, float64 arg)
{
    int64_t ret = float64_to_int64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_t_s_w)(CPUHPPAState *env, float32 arg)
{
    int32_t ret = float32_to_int32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int32_t HELPER(fcnv_t_d_w)(CPUHPPAState *env, float64 arg)
{
    int32_t ret = float64_to_int32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_t_s_dw)(CPUHPPAState *env, float32 arg)
{
    int64_t ret = float32_to_int64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

int64_t HELPER(fcnv_t_d_dw)(CPUHPPAState *env, float64 arg)
{
    int64_t ret = float64_to_int64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_uw_s)(CPUHPPAState *env, uint32_t arg)
{
    float32 ret = uint32_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fcnv_udw_s)(CPUHPPAState *env, uint64_t arg)
{
    float32 ret = uint64_to_float32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_uw_d)(CPUHPPAState *env, uint32_t arg)
{
    float64 ret = uint32_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fcnv_udw_d)(CPUHPPAState *env, uint64_t arg)
{
    float64 ret = uint64_to_float64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_s_uw)(CPUHPPAState *env, float32 arg)
{
    uint32_t ret = float32_to_uint32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_d_uw)(CPUHPPAState *env, float64 arg)
{
    uint32_t ret = float64_to_uint32(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_s_udw)(CPUHPPAState *env, float32 arg)
{
    uint64_t ret = float32_to_uint64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_d_udw)(CPUHPPAState *env, float64 arg)
{
    uint64_t ret = float64_to_uint64(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_t_s_uw)(CPUHPPAState *env, float32 arg)
{
    uint32_t ret = float32_to_uint32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint32_t HELPER(fcnv_t_d_uw)(CPUHPPAState *env, float64 arg)
{
    uint32_t ret = float64_to_uint32_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_t_s_udw)(CPUHPPAState *env, float32 arg)
{
    uint64_t ret = float32_to_uint64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

uint64_t HELPER(fcnv_t_d_udw)(CPUHPPAState *env, float64 arg)
{
    uint64_t ret = float64_to_uint64_round_to_zero(arg, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

static void update_fr0_cmp(CPUHPPAState *env, uint32_t y,
                           uint32_t c, FloatRelation r)
{
    uint32_t shadow = env->fr0_shadow;

    switch (r) {
    case float_relation_greater:
        c = extract32(c, 4, 1);
        break;
    case float_relation_less:
        c = extract32(c, 3, 1);
        break;
    case float_relation_equal:
        c = extract32(c, 2, 1);
        break;
    case float_relation_unordered:
        c = extract32(c, 1, 1);
        break;
    default:
        g_assert_not_reached();
    }

    if (y) {
        /* targeted comparison */
        /* set fpsr[ca[y - 1]] to current compare */
        shadow = deposit32(shadow, 21 - (y - 1), 1, c);
    } else {
        /* queued comparison */
        /* shift cq right by one place */
        shadow = deposit32(shadow, 11, 10, extract32(shadow, 12, 10));
        /* move fpsr[c] to fpsr[cq[0]] */
        shadow = deposit32(shadow, 21, 1, extract32(shadow, 26, 1));
        /* set fpsr[c] to current compare */
        shadow = deposit32(shadow, 26, 1, c);
    }

    env->fr0_shadow = shadow;
    env->fr[0] = (uint64_t)shadow << 32;
}

void HELPER(fcmp_s)(CPUHPPAState *env, float32 a, float32 b,
                    uint32_t y, uint32_t c)
{
    FloatRelation r;
    if (c & 1) {
        r = float32_compare(a, b, &env->fp_status);
    } else {
        r = float32_compare_quiet(a, b, &env->fp_status);
    }
    update_fr0_op(env, GETPC());
    update_fr0_cmp(env, y, c, r);
}

void HELPER(fcmp_d)(CPUHPPAState *env, float64 a, float64 b,
                    uint32_t y, uint32_t c)
{
    FloatRelation r;
    if (c & 1) {
        r = float64_compare(a, b, &env->fp_status);
    } else {
        r = float64_compare_quiet(a, b, &env->fp_status);
    }
    update_fr0_op(env, GETPC());
    update_fr0_cmp(env, y, c, r);
}

float32 HELPER(fmpyfadd_s)(CPUHPPAState *env, float32 a, float32 b, float32 c)
{
    float32 ret = float32_muladd(a, b, c, 0, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float32 HELPER(fmpynfadd_s)(CPUHPPAState *env, float32 a, float32 b, float32 c)
{
    float32 ret = float32_muladd(a, b, c, float_muladd_negate_product,
                                 &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fmpyfadd_d)(CPUHPPAState *env, float64 a, float64 b, float64 c)
{
    float64 ret = float64_muladd(a, b, c, 0, &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}

float64 HELPER(fmpynfadd_d)(CPUHPPAState *env, float64 a, float64 b, float64 c)
{
    float64 ret = float64_muladd(a, b, c, float_muladd_negate_product,
                                 &env->fp_status);
    update_fr0_op(env, GETPC());
    return ret;
}
