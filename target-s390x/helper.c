/*
 *  S/390 helpers
 *
 *  Copyright (c) 2009 Ulrich Hecht
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "exec-all.h"
#include "gdbstub.h"
#include "qemu-common.h"

#include <linux/kvm.h>
#include "kvm.h"

CPUS390XState *cpu_s390x_init(const char *cpu_model)
{
    CPUS390XState *env;
    static int inited = 0;

    env = qemu_mallocz(sizeof(CPUS390XState));
    cpu_exec_init(env);
    if (!inited) {
        inited = 1;
    }

    env->cpu_model_str = cpu_model;
    cpu_reset(env);
    qemu_init_vcpu(env);
    return env;
}

void cpu_reset(CPUS390XState *env)
{
    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    memset(env, 0, offsetof(CPUS390XState, breakpoints));
    /* FIXME: reset vector? */
    tlb_flush(env, 1);
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return 0;
}

#ifndef CONFIG_USER_ONLY

int cpu_s390x_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                                int mmu_idx, int is_softmmu)
{
    target_ulong phys;
    int prot;

    /* XXX: implement mmu */

    phys = address;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    tlb_set_page(env, address & TARGET_PAGE_MASK,
                 phys & TARGET_PAGE_MASK, prot,
                 mmu_idx, TARGET_PAGE_SIZE);
    return 0;
}
#endif /* CONFIG_USER_ONLY */

void do_interrupt (CPUState *env)
{
}
