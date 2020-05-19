/*
 * OpenRISC float helper routines
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
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
#include "exec/helper-proto.h"
#include "exception.h"
#include "fpu/softfloat.h"

static int ieee_ex_to_openrisc(int fexcp)
{
    int ret = 0;
    if (fexcp & float_flag_invalid) {
        ret |= FPCSR_IVF;
    }
    if (fexcp & float_flag_overflow) {
        ret |= FPCSR_OVF;
    }
    if (fexcp & float_flag_underflow) {
        ret |= FPCSR_UNF;
    }
    if (fexcp & float_flag_divbyzero) {
        ret |= FPCSR_DZF;
    }
    if (fexcp & float_flag_inexact) {
        ret |= FPCSR_IXF;
    }
    return ret;
}

void HELPER(update_fpcsr)(CPUOpenRISCState *env)
{
    int tmp = get_float_exception_flags(&env->fp_status);

    if (tmp) {
        set_float_exception_flags(0, &env->fp_status);
        tmp = ieee_ex_to_openrisc(tmp);
        if (tmp) {
            env->fpcsr |= tmp;
            if (env->fpcsr & FPCSR_FPEE) {
                helper_exception(env, EXCP_FPE);
            }
        }
    }
}

void cpu_set_fpcsr(CPUOpenRISCState *env, uint32_t val)
{
    static const int rm_to_sf[] = {
        float_round_nearest_even,
        float_round_to_zero,
        float_round_up,
        float_round_down
    };

    env->fpcsr = val & 0xfff;
    set_float_rounding_mode(rm_to_sf[extract32(val, 1, 2)], &env->fp_status);
}

uint64_t HELPER(itofd)(CPUOpenRISCState *env, uint64_t val)
{
    return int64_to_float64(val, &env->fp_status);
}

uint32_t HELPER(itofs)(CPUOpenRISCState *env, uint32_t val)
{
    return int32_to_float32(val, &env->fp_status);
}

uint64_t HELPER(ftoid)(CPUOpenRISCState *env, uint64_t val)
{
    return float64_to_int64_round_to_zero(val, &env->fp_status);
}

uint32_t HELPER(ftois)(CPUOpenRISCState *env, uint32_t val)
{
    return float32_to_int32_round_to_zero(val, &env->fp_status);
}

uint64_t HELPER(stod)(CPUOpenRISCState *env, uint32_t val)
{
    return float32_to_float64(val, &env->fp_status);
}

uint32_t HELPER(dtos)(CPUOpenRISCState *env, uint64_t val)
{
    return float64_to_float32(val, &env->fp_status);
}

#define FLOAT_CALC(name)                                                  \
uint64_t helper_float_ ## name ## _d(CPUOpenRISCState *env,               \
                                     uint64_t fdt0, uint64_t fdt1)        \
{ return float64_ ## name(fdt0, fdt1, &env->fp_status); }                 \
uint32_t helper_float_ ## name ## _s(CPUOpenRISCState *env,               \
                                     uint32_t fdt0, uint32_t fdt1)        \
{ return float32_ ## name(fdt0, fdt1, &env->fp_status); }

FLOAT_CALC(add)
FLOAT_CALC(sub)
FLOAT_CALC(mul)
FLOAT_CALC(div)
FLOAT_CALC(rem)
#undef FLOAT_CALC


uint64_t helper_float_madd_d(CPUOpenRISCState *env, uint64_t a,
                             uint64_t b, uint64_t c)
{
    /* Note that or1ksim doesn't use fused operation.  */
    b = float64_mul(b, c, &env->fp_status);
    return float64_add(a, b, &env->fp_status);
}

uint32_t helper_float_madd_s(CPUOpenRISCState *env, uint32_t a,
                             uint32_t b, uint32_t c)
{
    /* Note that or1ksim doesn't use fused operation.  */
    b = float32_mul(b, c, &env->fp_status);
    return float32_add(a, b, &env->fp_status);
}


#define FLOAT_CMP(name, impl)                                             \
target_ulong helper_float_ ## name ## _d(CPUOpenRISCState *env,           \
                                         uint64_t fdt0, uint64_t fdt1)    \
{ return float64_ ## impl(fdt0, fdt1, &env->fp_status); }                 \
target_ulong helper_float_ ## name ## _s(CPUOpenRISCState *env,           \
                                         uint32_t fdt0, uint32_t fdt1)    \
{ return float32_ ## impl(fdt0, fdt1, &env->fp_status); }

FLOAT_CMP(le, le)
FLOAT_CMP(lt, lt)
FLOAT_CMP(eq, eq_quiet)
FLOAT_CMP(un, unordered_quiet)
#undef FLOAT_CMP

#define FLOAT_UCMP(name, expr) \
target_ulong helper_float_ ## name ## _d(CPUOpenRISCState *env,           \
                                         uint64_t fdt0, uint64_t fdt1)    \
{                                                                         \
    FloatRelation r = float64_compare_quiet(fdt0, fdt1, &env->fp_status); \
    return expr;                                                          \
}                                                                         \
target_ulong helper_float_ ## name ## _s(CPUOpenRISCState *env,           \
                                         uint32_t fdt0, uint32_t fdt1)    \
{                                                                         \
    FloatRelation r = float32_compare_quiet(fdt0, fdt1, &env->fp_status); \
    return expr;                                                          \
}

FLOAT_UCMP(ueq, r == float_relation_equal || r == float_relation_unordered)
FLOAT_UCMP(ult, r == float_relation_less || r == float_relation_unordered)
FLOAT_UCMP(ule, r != float_relation_greater)
#undef FLOAT_UCMP
