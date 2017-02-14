/*
 * OpenRISC float helper routines
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
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
#include "exception.h"

static inline uint32_t ieee_ex_to_openrisc(OpenRISCCPU *cpu, int fexcp)
{
    int ret = 0;
    if (fexcp) {
        if (fexcp & float_flag_invalid) {
            cpu->env.fpcsr |= FPCSR_IVF;
            ret = 1;
        }
        if (fexcp & float_flag_overflow) {
            cpu->env.fpcsr |= FPCSR_OVF;
            ret = 1;
        }
        if (fexcp & float_flag_underflow) {
            cpu->env.fpcsr |= FPCSR_UNF;
            ret = 1;
        }
        if (fexcp & float_flag_divbyzero) {
            cpu->env.fpcsr |= FPCSR_DZF;
            ret = 1;
        }
        if (fexcp & float_flag_inexact) {
            cpu->env.fpcsr |= FPCSR_IXF;
            ret = 1;
        }
    }

    return ret;
}

static inline void update_fpcsr(OpenRISCCPU *cpu)
{
    int tmp = ieee_ex_to_openrisc(cpu,
                              get_float_exception_flags(&cpu->env.fp_status));

    SET_FP_CAUSE(cpu->env.fpcsr, tmp);
    if ((GET_FP_ENABLE(cpu->env.fpcsr) & tmp) &&
        (cpu->env.fpcsr & FPCSR_FPEE)) {
        helper_exception(&cpu->env, EXCP_FPE);
    } else {
        UPDATE_FP_FLAGS(cpu->env.fpcsr, tmp);
    }
}

uint64_t HELPER(itofd)(CPUOpenRISCState *env, uint64_t val)
{
    uint64_t itofd;
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);

    set_float_exception_flags(0, &cpu->env.fp_status);
    itofd = int32_to_float64(val, &cpu->env.fp_status);
    update_fpcsr(cpu);

    return itofd;
}

uint32_t HELPER(itofs)(CPUOpenRISCState *env, uint32_t val)
{
    uint32_t itofs;
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);

    set_float_exception_flags(0, &cpu->env.fp_status);
    itofs = int32_to_float32(val, &cpu->env.fp_status);
    update_fpcsr(cpu);

    return itofs;
}

uint64_t HELPER(ftoid)(CPUOpenRISCState *env, uint64_t val)
{
    uint64_t ftoid;
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);

    set_float_exception_flags(0, &cpu->env.fp_status);
    ftoid = float32_to_int64(val, &cpu->env.fp_status);
    update_fpcsr(cpu);

    return ftoid;
}

uint32_t HELPER(ftois)(CPUOpenRISCState *env, uint32_t val)
{
    uint32_t ftois;
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);

    set_float_exception_flags(0, &cpu->env.fp_status);
    ftois = float32_to_int32(val, &cpu->env.fp_status);
    update_fpcsr(cpu);

    return ftois;
}

#define FLOAT_OP(name, p) void helper_float_##_##p(void)

#define FLOAT_CALC(name)                                                  \
uint64_t helper_float_ ## name ## _d(CPUOpenRISCState *env,               \
                                     uint64_t fdt0, uint64_t fdt1)        \
{                                                                         \
    uint64_t result;                                                      \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    result = float64_ ## name(fdt0, fdt1, &cpu->env.fp_status);           \
    update_fpcsr(cpu);                                                    \
    return result;                                                        \
}                                                                         \
                                                                          \
uint32_t helper_float_ ## name ## _s(CPUOpenRISCState *env,               \
                                     uint32_t fdt0, uint32_t fdt1)        \
{                                                                         \
    uint32_t result;                                                      \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    result = float32_ ## name(fdt0, fdt1, &cpu->env.fp_status);           \
    update_fpcsr(cpu);                                                    \
    return result;                                                        \
}                                                                         \

FLOAT_CALC(add)
FLOAT_CALC(sub)
FLOAT_CALC(mul)
FLOAT_CALC(div)
FLOAT_CALC(rem)
#undef FLOAT_CALC


uint64_t helper_float_madd_d(CPUOpenRISCState *env, uint64_t a,
                             uint64_t b, uint64_t c)
{
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);
    uint64_t result;
    set_float_exception_flags(0, &cpu->env.fp_status);
    /* Note that or1ksim doesn't use merged operation.  */
    result = float64_mul(b, c, &cpu->env.fp_status);
    result = float64_add(result, a, &cpu->env.fp_status);
    update_fpcsr(cpu);
    return result;
}

uint32_t helper_float_madd_s(CPUOpenRISCState *env, uint32_t a,
                             uint32_t b, uint32_t c)
{
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);
    uint32_t result;
    set_float_exception_flags(0, &cpu->env.fp_status);
    /* Note that or1ksim doesn't use merged operation.  */
    result = float32_mul(b, c, &cpu->env.fp_status);
    result = float32_add(result, a, &cpu->env.fp_status);
    update_fpcsr(cpu);
    return result;
}


#define FLOAT_CMP(name)                                                   \
uint64_t helper_float_ ## name ## _d(CPUOpenRISCState *env,               \
                                     uint64_t fdt0, uint64_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = float64_ ## name(fdt0, fdt1, &cpu->env.fp_status);              \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}                                                                         \
                                                                          \
uint32_t helper_float_ ## name ## _s(CPUOpenRISCState *env,               \
                                             uint32_t fdt0, uint32_t fdt1)\
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = float32_ ## name(fdt0, fdt1, &cpu->env.fp_status);              \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}

FLOAT_CMP(le)
FLOAT_CMP(eq)
FLOAT_CMP(lt)
#undef FLOAT_CMP


#define FLOAT_CMPNE(name)                                                 \
uint64_t helper_float_ ## name ## _d(CPUOpenRISCState *env,               \
                                     uint64_t fdt0, uint64_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = !float64_eq_quiet(fdt0, fdt1, &cpu->env.fp_status);             \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}                                                                         \
                                                                          \
uint32_t helper_float_ ## name ## _s(CPUOpenRISCState *env,               \
                                     uint32_t fdt0, uint32_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = !float32_eq_quiet(fdt0, fdt1, &cpu->env.fp_status);             \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}

FLOAT_CMPNE(ne)
#undef FLOAT_CMPNE

#define FLOAT_CMPGT(name)                                                 \
uint64_t helper_float_ ## name ## _d(CPUOpenRISCState *env,               \
                                     uint64_t fdt0, uint64_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = !float64_le(fdt0, fdt1, &cpu->env.fp_status);                   \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}                                                                         \
                                                                          \
uint32_t helper_float_ ## name ## _s(CPUOpenRISCState *env,               \
                                     uint32_t fdt0, uint32_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = !float32_le(fdt0, fdt1, &cpu->env.fp_status);                   \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}
FLOAT_CMPGT(gt)
#undef FLOAT_CMPGT

#define FLOAT_CMPGE(name)                                                 \
uint64_t helper_float_ ## name ## _d(CPUOpenRISCState *env,               \
                                     uint64_t fdt0, uint64_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = !float64_lt(fdt0, fdt1, &cpu->env.fp_status);                   \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}                                                                         \
                                                                          \
uint32_t helper_float_ ## name ## _s(CPUOpenRISCState *env,               \
                                     uint32_t fdt0, uint32_t fdt1)        \
{                                                                         \
    int res;                                                              \
    OpenRISCCPU *cpu = openrisc_env_get_cpu(env);                         \
    set_float_exception_flags(0, &cpu->env.fp_status);                    \
    res = !float32_lt(fdt0, fdt1, &cpu->env.fp_status);                   \
    update_fpcsr(cpu);                                                    \
    return res;                                                           \
}

FLOAT_CMPGE(ge)
#undef FLOAT_CMPGE
