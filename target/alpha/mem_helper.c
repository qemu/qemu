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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

/* Softmmu support */
#ifndef CONFIG_USER_ONLY
void alpha_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                   MMUAccessType access_type,
                                   int mmu_idx, uintptr_t retaddr)
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
    cs->exception_index = EXCP_MCHK;
    env->error_code = 0;

    /* ??? We should cpu_restore_state to the faulting insn, but this hook
       does not have access to the retaddr value from the original helper.
       It's all moot until the QEMU PALcode grows an MCHK handler.  */

    cpu_loop_exit(cs);
}

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = alpha_cpu_handle_mmu_fault(cs, addr, access_type, mmu_idx);
    if (unlikely(ret != 0)) {
        if (retaddr) {
            cpu_restore_state(cs, retaddr);
        }
        /* Exception index and error code are already set */
        cpu_loop_exit(cs);
    }
}
#endif /* CONFIG_USER_ONLY */
