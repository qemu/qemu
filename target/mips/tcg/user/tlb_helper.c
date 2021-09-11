/*
 * MIPS TLB (Translation lookaside buffer) helpers.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
#include "internal.h"

static void raise_mmu_exception(CPUMIPSState *env, target_ulong address,
                                MMUAccessType access_type)
{
    CPUState *cs = env_cpu(env);

    env->error_code = 0;
    if (access_type == MMU_INST_FETCH) {
        env->error_code |= EXCP_INST_NOTAVAIL;
    }

    /* Reference to kernel address from user mode or supervisor mode */
    /* Reference to supervisor address from user mode */
    if (access_type == MMU_DATA_STORE) {
        cs->exception_index = EXCP_AdES;
    } else {
        cs->exception_index = EXCP_AdEL;
    }

    /* Raise exception */
    if (!(env->hflags & MIPS_HFLAG_DM)) {
        env->CP0_BadVAddr = address;
    }
}

bool mips_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                       MMUAccessType access_type, int mmu_idx,
                       bool probe, uintptr_t retaddr)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    /* data access */
    raise_mmu_exception(env, address, access_type);
    do_raise_exception_err(env, cs->exception_index, env->error_code, retaddr);
}
