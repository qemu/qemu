/*
 *  x86_64 cpu init and loop
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_CPU_H
#define TARGET_ARCH_CPU_H

#include "target_arch.h"
#include "signal-common.h"

#define TARGET_DEFAULT_CPU_MODEL "qemu64"

static inline void target_cpu_init(CPUX86State *env,
        struct target_pt_regs *regs)
{
    uint64_t *gdt_table;

    env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;
    env->hflags |= HF_PE_MASK | HF_CPL_MASK;
    if (env->features[FEAT_1_EDX] & CPUID_SSE) {
        env->cr[4] |= CR4_OSFXSR_MASK;
        env->hflags |= HF_OSFXSR_MASK;
    }

    /* enable 64 bit mode if possible */
    if (!(env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM)) {
        fprintf(stderr, "The selected x86 CPU does not support 64 bit mode\n");
        exit(1);
    }
    env->cr[4] |= CR4_PAE_MASK;
    env->efer |= MSR_EFER_LMA | MSR_EFER_LME;
    env->hflags |= HF_LMA_MASK;

    /* flags setup : we activate the IRQs by default as in user mode */
    env->eflags |= IF_MASK;

    /* register setup */
    env->regs[R_EAX] = regs->rax;
    env->regs[R_EBX] = regs->rbx;
    env->regs[R_ECX] = regs->rcx;
    env->regs[R_EDX] = regs->rdx;
    env->regs[R_ESI] = regs->rsi;
    env->regs[R_EDI] = regs->rdi;
    env->regs[R_EBP] = regs->rbp;
    env->regs[R_ESP] = regs->rsp;
    env->eip = regs->rip;

    /* interrupt setup */
    env->idt.limit = 511;

    env->idt.base = target_mmap(0, sizeof(uint64_t) * (env->idt.limit + 1),
        PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    bsd_x86_64_set_idt_base(env->idt.base);
    bsd_x86_64_set_idt(0, 0);
    bsd_x86_64_set_idt(1, 0);
    bsd_x86_64_set_idt(2, 0);
    bsd_x86_64_set_idt(3, 3);
    bsd_x86_64_set_idt(4, 3);
    bsd_x86_64_set_idt(5, 0);
    bsd_x86_64_set_idt(6, 0);
    bsd_x86_64_set_idt(7, 0);
    bsd_x86_64_set_idt(8, 0);
    bsd_x86_64_set_idt(9, 0);
    bsd_x86_64_set_idt(10, 0);
    bsd_x86_64_set_idt(11, 0);
    bsd_x86_64_set_idt(12, 0);
    bsd_x86_64_set_idt(13, 0);
    bsd_x86_64_set_idt(14, 0);
    bsd_x86_64_set_idt(15, 0);
    bsd_x86_64_set_idt(16, 0);
    bsd_x86_64_set_idt(17, 0);
    bsd_x86_64_set_idt(18, 0);
    bsd_x86_64_set_idt(19, 0);
    bsd_x86_64_set_idt(0x80, 3);

    /* segment setup */
    env->gdt.base = target_mmap(0, sizeof(uint64_t) * TARGET_GDT_ENTRIES,
            PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    env->gdt.limit = sizeof(uint64_t) * TARGET_GDT_ENTRIES - 1;
    gdt_table = g2h_untagged(env->gdt.base);

    /* 64 bit code segment */
    bsd_x86_64_write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
            DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK | DESC_L_MASK
            | (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));

    bsd_x86_64_write_dt(&gdt_table[__USER_DS >> 3], 0, 0xfffff,
            DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
            (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));

    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_DS, 0);
    cpu_x86_load_seg(env, R_ES, 0);
    cpu_x86_load_seg(env, R_FS, 0);
    cpu_x86_load_seg(env, R_GS, 0);
}

static inline G_NORETURN void target_cpu_loop(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_ulong pc;
    /* target_siginfo_t info; */

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        switch (trapnr) {
        case EXCP_SYSCALL:
            /* syscall from syscall instruction */
            env->regs[R_EAX] = do_freebsd_syscall(env,
                                                  env->regs[R_EAX],
                                                  env->regs[R_EDI],
                                                  env->regs[R_ESI],
                                                  env->regs[R_EDX],
                                                  env->regs[R_ECX],
                                                  env->regs[8],
                                                  env->regs[9], 0, 0);
            env->eip = env->exception_next_eip;
            if (((abi_ulong)env->regs[R_EAX]) >= (abi_ulong)(-515)) {
                env->regs[R_EAX] = -env->regs[R_EAX];
                env->eflags |= CC_C;
            } else {
                env->eflags &= ~CC_C;
            }
            break;

        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;

        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;

        default:
            pc = env->segs[R_CS].base + env->eip;
            fprintf(stderr, "qemu: 0x%08lx: unhandled CPU exception 0x%x - "
                    "aborting\n", (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

static inline void target_cpu_clone_regs(CPUX86State *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[R_ESP] = newsp;
    }
    env->regs[R_EAX] = 0;
}

static inline void target_cpu_reset(CPUArchState *env)
{
    cpu_reset(env_cpu(env));
}

#endif /* TARGET_ARCH_CPU_H */
