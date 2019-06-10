/*
 *  Moxie helper routines.
 *
 *  Copyright (c) 2008, 2009, 2010, 2013 Anthony Green
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "mmu.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"

void helper_raise_exception(CPUMoxieState *env, int ex)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = ex;
    /* Stash the exception type.  */
    env->sregs[2] = ex;
    /* Stash the address where the exception occurred.  */
    cpu_restore_state(cs, GETPC(), true);
    env->sregs[5] = env->pc;
    /* Jump to the exception handline routine.  */
    env->pc = env->sregs[1];
    cpu_loop_exit(cs);
}

uint32_t helper_div(CPUMoxieState *env, uint32_t a, uint32_t b)
{
    if (unlikely(b == 0)) {
        helper_raise_exception(env, MOXIE_EX_DIV0);
        return 0;
    }
    if (unlikely(a == INT_MIN && b == -1)) {
        return INT_MIN;
    }

    return (int32_t)a / (int32_t)b;
}

uint32_t helper_udiv(CPUMoxieState *env, uint32_t a, uint32_t b)
{
    if (unlikely(b == 0)) {
        helper_raise_exception(env, MOXIE_EX_DIV0);
        return 0;
    }
    return a / b;
}

void helper_debug(CPUMoxieState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

bool moxie_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    MoxieCPU *cpu = MOXIE_CPU(cs);
    CPUMoxieState *env = &cpu->env;
    MoxieMMUResult res;
    int prot, miss;

    address &= TARGET_PAGE_MASK;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    miss = moxie_mmu_translate(&res, env, address, access_type, mmu_idx);
    if (likely(!miss)) {
        tlb_set_page(cs, address, res.phy, prot, mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }
    if (probe) {
        return false;
    }

    cs->exception_index = MOXIE_EX_MMU_MISS;
    cpu_loop_exit_restore(cs, retaddr);
}

void moxie_cpu_do_interrupt(CPUState *cs)
{
    switch (cs->exception_index) {
    case MOXIE_EX_BREAK:
        break;
    default:
        break;
    }
}

hwaddr moxie_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    MoxieCPU *cpu = MOXIE_CPU(cs);
    uint32_t phy = addr;
    MoxieMMUResult res;
    int miss;

    miss = moxie_mmu_translate(&res, &cpu->env, addr, 0, 0);
    if (!miss) {
        phy = res.phy;
    }
    return phy;
}
