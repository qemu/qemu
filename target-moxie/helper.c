/*
 *  Moxie helper routines.
 *
 *  Copyright (c) 2008, 2009, 2010, 2013 Anthony Green
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "config.h"
#include "cpu.h"
#include "mmu.h"
#include "exec/exec-all.h"
#include "qemu/host-utils.h"
#include "helper.h"

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "exec/softmmu_template.h"

#define SHIFT 1
#include "exec/softmmu_template.h"

#define SHIFT 2
#include "exec/softmmu_template.h"

#define SHIFT 3
#include "exec/softmmu_template.h"

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
void tlb_fill(CPUMoxieState *env, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = cpu_moxie_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            cpu_restore_state(env, retaddr);
        }
    }
    cpu_loop_exit(env);
}

void helper_raise_exception(CPUMoxieState *env, int ex)
{
    env->exception_index = ex;
    /* Stash the exception type.  */
    env->sregs[2] = ex;
    /* Stash the address where the exception occurred.  */
    cpu_restore_state(env, GETPC());
    env->sregs[5] = env->pc;
    /* Jump the the exception handline routine.  */
    env->pc = env->sregs[1];
    cpu_loop_exit(env);
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
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit(env);
}

#if defined(CONFIG_USER_ONLY)

void moxie_cpu_do_interrupt(CPUState *env)
{
    env->exception_index = -1;
}

int cpu_moxie_handle_mmu_fault(CPUMoxieState *env, target_ulong address,
                               int rw, int mmu_idx)
{
    env->exception_index = 0xaa;
    env->debug1 = address;
    cpu_dump_state(env, stderr, fprintf, 0);
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

#else /* !CONFIG_USER_ONLY */

int cpu_moxie_handle_mmu_fault(CPUMoxieState *env, target_ulong address,
                               int rw, int mmu_idx)
{
    MoxieMMUResult res;
    int prot, miss;
    target_ulong phy;
    int r = 1;

    address &= TARGET_PAGE_MASK;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    miss = moxie_mmu_translate(&res, env, address, rw, mmu_idx);
    if (miss) {
        /* handle the miss.  */
        phy = 0;
        env->exception_index = MOXIE_EX_MMU_MISS;
    } else {
        phy = res.phy;
        r = 0;
    }
    tlb_set_page(env, address, phy, prot, mmu_idx, TARGET_PAGE_SIZE);
    return r;
}


void moxie_cpu_do_interrupt(CPUState *cs)
{
    MoxieCPU *cpu = MOXIE_CPU(cs);
    CPUMoxieState *env = &cpu->env;

    switch (env->exception_index) {
    case MOXIE_EX_BREAK:
        break;
    default:
        break;
    }
}

hwaddr cpu_get_phys_page_debug(CPUMoxieState *env, target_ulong addr)
{
    uint32_t phy = addr;
    MoxieMMUResult res;
    int miss;
    miss = moxie_mmu_translate(&res, env, addr, 0, 0);
    if (!miss) {
        phy = res.phy;
    }
    return phy;
}
#endif
