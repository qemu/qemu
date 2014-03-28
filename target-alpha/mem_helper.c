/*
 *  Helpers for loads and stores
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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


/* Softmmu support */
#ifndef CONFIG_USER_ONLY

uint64_t helper_ldl_phys(CPUAlphaState *env, uint64_t p)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    return (int32_t)ldl_phys(cs->as, p);
}

uint64_t helper_ldq_phys(CPUAlphaState *env, uint64_t p)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    return ldq_phys(cs->as, p);
}

uint64_t helper_ldl_l_phys(CPUAlphaState *env, uint64_t p)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    env->lock_addr = p;
    return env->lock_value = (int32_t)ldl_phys(cs->as, p);
}

uint64_t helper_ldq_l_phys(CPUAlphaState *env, uint64_t p)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    env->lock_addr = p;
    return env->lock_value = ldq_phys(cs->as, p);
}

void helper_stl_phys(CPUAlphaState *env, uint64_t p, uint64_t v)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    stl_phys(cs->as, p, v);
}

void helper_stq_phys(CPUAlphaState *env, uint64_t p, uint64_t v)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    stq_phys(cs->as, p, v);
}

uint64_t helper_stl_c_phys(CPUAlphaState *env, uint64_t p, uint64_t v)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    uint64_t ret = 0;

    if (p == env->lock_addr) {
        int32_t old = ldl_phys(cs->as, p);
        if (old == (int32_t)env->lock_value) {
            stl_phys(cs->as, p, v);
            ret = 1;
        }
    }
    env->lock_addr = -1;

    return ret;
}

uint64_t helper_stq_c_phys(CPUAlphaState *env, uint64_t p, uint64_t v)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    uint64_t ret = 0;

    if (p == env->lock_addr) {
        uint64_t old = ldq_phys(cs->as, p);
        if (old == env->lock_value) {
            stq_phys(cs->as, p, v);
            ret = 1;
        }
    }
    env->lock_addr = -1;

    return ret;
}

void alpha_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   int is_write, int is_user, uintptr_t retaddr)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;
    uint64_t pc;
    uint32_t insn;

    if (retaddr) {
        cpu_restore_state(cs, retaddr);
    }

    pc = env->pc;
    insn = cpu_ldl_code(env, pc);

    env->trap_arg0 = addr;
    env->trap_arg1 = insn >> 26;                /* opcode */
    env->trap_arg2 = (insn >> 21) & 31;         /* dest regno */
    cs->exception_index = EXCP_UNALIGN;
    env->error_code = 0;
    cpu_loop_exit(cs);
}

void alpha_cpu_unassigned_access(CPUState *cs, hwaddr addr,
                                 bool is_write, bool is_exec, int unused,
                                 unsigned size)
{
    AlphaCPU *cpu = ALPHA_CPU(cs);
    CPUAlphaState *env = &cpu->env;

    env->trap_arg0 = addr;
    env->trap_arg1 = is_write ? 1 : 0;
    dynamic_excp(env, 0, EXCP_MCHK, 0);
}

#include "exec/softmmu_exec.h"

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "exec/softmmu_template.h"

#define SHIFT 1
#include "exec/softmmu_template.h"

#define SHIFT 2
#include "exec/softmmu_template.h"

#define SHIFT 3
#include "exec/softmmu_template.h"

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = alpha_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (unlikely(ret != 0)) {
        if (retaddr) {
            cpu_restore_state(cs, retaddr);
        }
        /* Exception index and error code are already set */
        cpu_loop_exit(cs);
    }
}
#endif /* CONFIG_USER_ONLY */
