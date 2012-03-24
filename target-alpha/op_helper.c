/*
 *  Alpha emulation cpu micro-operations helpers for qemu.
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
#include "dyngen-exec.h"
#include "host-utils.h"
#include "softfloat.h"
#include "helper.h"
#include "sysemu.h"
#include "qemu-timer.h"


/*****************************************************************************/
/* Softmmu support */
#if !defined (CONFIG_USER_ONLY)
uint64_t helper_ldl_phys(uint64_t p)
{
    return (int32_t)ldl_phys(p);
}

uint64_t helper_ldq_phys(uint64_t p)
{
    return ldq_phys(p);
}

uint64_t helper_ldl_l_phys(uint64_t p)
{
    env->lock_addr = p;
    return env->lock_value = (int32_t)ldl_phys(p);
}

uint64_t helper_ldq_l_phys(uint64_t p)
{
    env->lock_addr = p;
    return env->lock_value = ldl_phys(p);
}

void helper_stl_phys(uint64_t p, uint64_t v)
{
    stl_phys(p, v);
}

void helper_stq_phys(uint64_t p, uint64_t v)
{
    stq_phys(p, v);
}

uint64_t helper_stl_c_phys(uint64_t p, uint64_t v)
{
    uint64_t ret = 0;

    if (p == env->lock_addr) {
        int32_t old = ldl_phys(p);
        if (old == (int32_t)env->lock_value) {
            stl_phys(p, v);
            ret = 1;
        }
    }
    env->lock_addr = -1;

    return ret;
}

uint64_t helper_stq_c_phys(uint64_t p, uint64_t v)
{
    uint64_t ret = 0;

    if (p == env->lock_addr) {
        uint64_t old = ldq_phys(p);
        if (old == env->lock_value) {
            stq_phys(p, v);
            ret = 1;
        }
    }
    env->lock_addr = -1;

    return ret;
}

static void QEMU_NORETURN do_unaligned_access(target_ulong addr, int is_write,
                                              int is_user, void *retaddr)
{
    uint64_t pc;
    uint32_t insn;

    do_restore_state(env, retaddr);

    pc = env->pc;
    insn = ldl_code(pc);

    env->trap_arg0 = addr;
    env->trap_arg1 = insn >> 26;                /* opcode */
    env->trap_arg2 = (insn >> 21) & 31;         /* dest regno */
    env->exception_index = EXCP_UNALIGN;
    env->error_code = 0;
    cpu_loop_exit(env);
}

void QEMU_NORETURN cpu_unassigned_access(CPUAlphaState *env1,
                                         target_phys_addr_t addr, int is_write,
                                         int is_exec, int unused, int size)
{
    env = env1;
    env->trap_arg0 = addr;
    env->trap_arg1 = is_write;
    dynamic_excp(env1, GETPC(), EXCP_MCHK, 0);
}

#include "softmmu_exec.h"

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUAlphaState *env1, target_ulong addr, int is_write, int mmu_idx,
              void *retaddr)
{
    CPUAlphaState *saved_env;
    int ret;

    saved_env = env;
    env = env1;
    ret = cpu_alpha_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (unlikely(ret != 0)) {
        do_restore_state(env, retaddr);
        /* Exception index and error code are already set */
        cpu_loop_exit(env);
    }
    env = saved_env;
}
#endif
