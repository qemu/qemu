/*
 *  x86 segmentation related helpers: (sysemu-only code)
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
#include "exec/cpu_ldst.h"
#include "tcg/helper-tcg.h"
#include "../seg_helper.h"

#ifdef TARGET_X86_64
void helper_syscall(CPUX86State *env, int next_eip_addend)
{
    int selector;

    if (!(env->efer & MSR_EFER_SCE)) {
        raise_exception_err_ra(env, EXCP06_ILLOP, 0, GETPC());
    }
    selector = (env->star >> 32) & 0xffff;
    if (env->hflags & HF_LMA_MASK) {
        int code64;

        env->regs[R_ECX] = env->eip + next_eip_addend;
        env->regs[11] = cpu_compute_eflags(env) & ~RF_MASK;

        code64 = env->hflags & HF_CS64_MASK;

        env->eflags &= ~(env->fmask | RF_MASK);
        cpu_load_eflags(env, env->eflags, 0);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc,
                           0, 0xffffffff,
                               DESC_G_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK |
                               DESC_L_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        if (code64) {
            env->eip = env->lstar;
        } else {
            env->eip = env->cstar;
        }
    } else {
        env->regs[R_ECX] = (uint32_t)(env->eip + next_eip_addend);

        env->eflags &= ~(IF_MASK | RF_MASK | VM_MASK);
        cpu_x86_load_seg_cache(env, R_CS, selector & 0xfffc,
                           0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_CS_MASK | DESC_R_MASK | DESC_A_MASK);
        cpu_x86_load_seg_cache(env, R_SS, (selector + 8) & 0xfffc,
                               0, 0xffffffff,
                               DESC_G_MASK | DESC_B_MASK | DESC_P_MASK |
                               DESC_S_MASK |
                               DESC_W_MASK | DESC_A_MASK);
        env->eip = (uint32_t)env->star;
    }
}
#endif /* TARGET_X86_64 */

void handle_even_inj(CPUX86State *env, int intno, int is_int,
                     int error_code, int is_hw, int rm)
{
    CPUState *cs = env_cpu(env);
    uint32_t event_inj = x86_ldl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                                          control.event_inj));

    if (!(event_inj & SVM_EVTINJ_VALID)) {
        int type;

        if (is_int) {
            type = SVM_EVTINJ_TYPE_SOFT;
        } else {
            type = SVM_EVTINJ_TYPE_EXEPT;
        }
        event_inj = intno | type | SVM_EVTINJ_VALID;
        if (!rm && exception_has_error_code(intno)) {
            event_inj |= SVM_EVTINJ_VALID_ERR;
            x86_stl_phys(cs, env->vm_vmcb + offsetof(struct vmcb,
                                             control.event_inj_err),
                     error_code);
        }
        x86_stl_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.event_inj),
                 event_inj);
    }
}

void x86_cpu_do_interrupt(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (cs->exception_index == EXCP_VMEXIT) {
        assert(env->old_exception == -1);
        do_vmexit(env);
    } else {
        do_interrupt_all(cpu, cs->exception_index,
                         env->exception_is_int,
                         env->error_code,
                         env->exception_next_eip, 0);
        /* successfully delivered */
        env->old_exception = -1;
    }
}

bool x86_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int intno;

    interrupt_request = x86_cpu_pending_interrupt(cs, interrupt_request);
    if (!interrupt_request) {
        return false;
    }

    /* Don't process multiple interrupt requests in a single call.
     * This is required to make icount-driven execution deterministic.
     */
    switch (interrupt_request) {
    case CPU_INTERRUPT_POLL:
        cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(cpu->apic_state);
        break;
    case CPU_INTERRUPT_SIPI:
        do_cpu_sipi(cpu);
        break;
    case CPU_INTERRUPT_SMI:
        cpu_svm_check_intercept_param(env, SVM_EXIT_SMI, 0, 0);
        cs->interrupt_request &= ~CPU_INTERRUPT_SMI;
        do_smm_enter(cpu);
        break;
    case CPU_INTERRUPT_NMI:
        cpu_svm_check_intercept_param(env, SVM_EXIT_NMI, 0, 0);
        cs->interrupt_request &= ~CPU_INTERRUPT_NMI;
        env->hflags2 |= HF2_NMI_MASK;
        do_interrupt_x86_hardirq(env, EXCP02_NMI, 1);
        break;
    case CPU_INTERRUPT_MCE:
        cs->interrupt_request &= ~CPU_INTERRUPT_MCE;
        do_interrupt_x86_hardirq(env, EXCP12_MCHK, 0);
        break;
    case CPU_INTERRUPT_HARD:
        cpu_svm_check_intercept_param(env, SVM_EXIT_INTR, 0, 0);
        cs->interrupt_request &= ~(CPU_INTERRUPT_HARD |
                                   CPU_INTERRUPT_VIRQ);
        intno = cpu_get_pic_interrupt(env);
        qemu_log_mask(CPU_LOG_TB_IN_ASM,
                      "Servicing hardware INT=0x%02x\n", intno);
        do_interrupt_x86_hardirq(env, intno, 1);
        break;
    case CPU_INTERRUPT_VIRQ:
        cpu_svm_check_intercept_param(env, SVM_EXIT_VINTR, 0, 0);
        intno = x86_ldl_phys(cs, env->vm_vmcb
                             + offsetof(struct vmcb, control.int_vector));
        qemu_log_mask(CPU_LOG_TB_IN_ASM,
                      "Servicing virtual hardware INT=0x%02x\n", intno);
        do_interrupt_x86_hardirq(env, intno, 1);
        cs->interrupt_request &= ~CPU_INTERRUPT_VIRQ;
        env->int_ctl &= ~V_IRQ_MASK;
        break;
    }

    /* Ensure that no TB jump will be modified as the program flow was changed.  */
    return true;
}

/* check if Port I/O is allowed in TSS */
void helper_check_io(CPUX86State *env, uint32_t addr, uint32_t size)
{
    uintptr_t retaddr = GETPC();
    uint32_t io_offset, val, mask;

    /* TSS must be a valid 32 bit one */
    if (!(env->tr.flags & DESC_P_MASK) ||
        ((env->tr.flags >> DESC_TYPE_SHIFT) & 0xf) != 9 ||
        env->tr.limit < 103) {
        goto fail;
    }
    io_offset = cpu_lduw_kernel_ra(env, env->tr.base + 0x66, retaddr);
    io_offset += (addr >> 3);
    /* Note: the check needs two bytes */
    if ((io_offset + 1) > env->tr.limit) {
        goto fail;
    }
    val = cpu_lduw_kernel_ra(env, env->tr.base + io_offset, retaddr);
    val >>= (addr & 7);
    mask = (1 << size) - 1;
    /* all bits must be zero to allow the I/O */
    if ((val & mask) != 0) {
    fail:
        raise_exception_err_ra(env, EXCP0D_GPF, 0, retaddr);
    }
}
