/*
 *  x86 segmentation related helpers (user-mode code):
 *  TSS, interrupts, system calls, jumps and call/task gates, descriptors
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "exec/exec-all.h"
#include "accel/tcg/cpu-ldst.h"
#include "tcg/helper-tcg.h"
#include "tcg/seg_helper.h"

void helper_syscall(CPUX86State *env, int next_eip_addend)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_SYSCALL;
    env->exception_is_int = 0;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit(cs);
}

/*
 * fake user mode interrupt. is_int is TRUE if coming from the int
 * instruction. next_eip is the env->eip value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE or if intno
 * is EXCP_SYSCALL.
 */
static void do_interrupt_user(CPUX86State *env, int intno, int is_int,
                              int error_code, target_ulong next_eip)
{
    if (is_int) {
        SegmentCache *dt;
        target_ulong ptr;
        int dpl, cpl, shift;
        uint32_t e2;

        dt = &env->idt;
        if (env->hflags & HF_LMA_MASK) {
            shift = 4;
        } else {
            shift = 3;
        }
        ptr = dt->base + (intno << shift);
        e2 = cpu_ldl_kernel(env, ptr + 4);

        dpl = (e2 >> DESC_DPL_SHIFT) & 3;
        cpl = env->hflags & HF_CPL_MASK;
        /* check privilege if software int */
        if (dpl < cpl) {
            raise_exception_err(env, EXCP0D_GPF, (intno << shift) + 2);
        }
    }

    /* Since we emulate only user space, we cannot do more than
       exiting the emulation with the suitable exception and error
       code. So update EIP for INT 0x80 and EXCP_SYSCALL. */
    if (is_int || intno == EXCP_SYSCALL) {
        env->eip = next_eip;
    }
}

void x86_cpu_do_interrupt(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    /* if user mode only, we simulate a fake exception
       which will be handled outside the cpu execution
       loop */
    do_interrupt_user(env, cs->exception_index,
                      env->exception_is_int,
                      env->error_code,
                      env->exception_next_eip);
    /* successfully delivered */
    env->old_exception = -1;
}

void cpu_x86_load_seg(CPUX86State *env, X86Seg seg_reg, int selector)
{
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        int dpl = (env->eflags & VM_MASK) ? 3 : 0;
        selector &= 0xffff;
        cpu_x86_load_seg_cache(env, seg_reg, selector,
                               (selector << 4), 0xffff,
                               DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                               DESC_A_MASK | (dpl << DESC_DPL_SHIFT));
    } else {
        helper_load_seg(env, seg_reg, selector);
    }
}
