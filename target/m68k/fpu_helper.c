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

uint32_t HELPER(f64_to_i32)(CPUM68KState *env, float64 val)
{
    return float64_to_int32(val, &env->fp_status);
}

float32 HELPER(f64_to_f32)(CPUM68KState *env, float64 val)
{
    return float64_to_float32(val, &env->fp_status);
}

float64 HELPER(i32_to_f64)(CPUM68KState *env, uint32_t val)
{
    return int32_to_float64(val, &env->fp_status);
}

float64 HELPER(f32_to_f64)(CPUM68KState *env, float32 val)
{
    return float32_to_float64(val, &env->fp_status);
}

float64 HELPER(iround_f64)(CPUM68KState *env, float64 val)
{
    return float64_round_to_int(val, &env->fp_status);
}

float64 HELPER(itrunc_f64)(CPUM68KState *env, float64 val)
{
    return float64_trunc_to_int(val, &env->fp_status);
}

float64 HELPER(sqrt_f64)(CPUM68KState *env, float64 val)
{
    return float64_sqrt(val, &env->fp_status);
}

float64 HELPER(abs_f64)(float64 val)
{
    return float64_abs(val);
}

float64 HELPER(chs_f64)(float64 val)
{
    return float64_chs(val);
}

float64 HELPER(add_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_add(a, b, &env->fp_status);
}

float64 HELPER(sub_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_sub(a, b, &env->fp_status);
}

float64 HELPER(mul_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_mul(a, b, &env->fp_status);
}

float64 HELPER(div_f64)(CPUM68KState *env, float64 a, float64 b)
{
    return float64_div(a, b, &env->fp_status);
}

float64 HELPER(sub_cmp_f64)(CPUM68KState *env, float64 a, float64 b)
{
    /* ??? This may incorrectly raise exceptions.  */
    /* ??? Should flush denormals to zero.  */
    float64 res;
    res = float64_sub(a, b, &env->fp_status);
    if (float64_is_quiet_nan(res, &env->fp_status)) {
        /* +/-inf compares equal against itself, but sub returns nan.  */
        if (!float64_is_quiet_nan(a, &env->fp_status)
            && !float64_is_quiet_nan(b, &env->fp_status)) {
            res = float64_zero;
            if (float64_lt_quiet(a, res, &env->fp_status)) {
                res = float64_chs(res);
            }
        }
    }
    return res;
}

uint32_t HELPER(compare_f64)(CPUM68KState *env, float64 val)
{
    return float64_compare_quiet(val, float64_zero, &env->fp_status);
}
