/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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
#include <stdlib.h>
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

#define SSOV(env, ret, arg, len) do {               \
    int64_t max_pos = INT##len ##_MAX;              \
    int64_t max_neg = INT##len ##_MIN;              \
    if (arg > max_pos) {                            \
        env->PSW_USB_V = (1 << 31);                 \
        env->PSW_USB_SV = (1 << 31);                \
        ret = (target_ulong)max_pos;                \
    } else {                                        \
        if (arg < max_neg) {                        \
            env->PSW_USB_V = (1 << 31);             \
            env->PSW_USB_SV = (1 << 31);            \
            ret = (target_ulong)max_neg;            \
        } else {                                    \
            env->PSW_USB_V = 0;                     \
            ret = (target_ulong)arg;                \
        }                                           \
    }                                               \
    env->PSW_USB_AV = arg ^ arg * 2u;               \
    env->PSW_USB_SAV |= env->PSW_USB_AV;            \
} while (0)

target_ulong helper_add_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    target_ulong ret;
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 + t2;
    SSOV(env, ret, result, 32);
    return ret;
}

target_ulong helper_sub_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    target_ulong ret;
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 - t2;
    SSOV(env, ret, result, 32);
    return ret;
}

static inline void QEMU_NORETURN do_raise_exception_err(CPUTriCoreState *env,
                                                        uint32_t exception,
                                                        int error_code,
                                                        uintptr_t pc)
{
    CPUState *cs = CPU(tricore_env_get_cpu(env));
    cs->exception_index = exception;
    env->error_code = error_code;

    if (pc) {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, pc);
    }

    cpu_loop_exit(cs);
}

static inline void QEMU_NORETURN do_raise_exception(CPUTriCoreState *env,
                                                    uint32_t exception,
                                                    uintptr_t pc)
{
    do_raise_exception_err(env, exception, 0, pc);
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;
    ret = cpu_tricore_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (ret) {
        TriCoreCPU *cpu = TRICORE_CPU(cs);
        CPUTriCoreState *env = &cpu->env;
        do_raise_exception_err(env, cs->exception_index,
                               env->error_code, retaddr);
    }
}
