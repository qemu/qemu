/*
 *  arm cpu init and loop
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef _TARGET_ARCH_CPU_H_
#define _TARGET_ARCH_CPU_H_

#include "target_arch.h"

#define TARGET_DEFAULT_CPU_MODEL "any"

static inline void target_cpu_init(CPUARMState *env,
        struct target_pt_regs *regs)
{
    int i;

    cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
               CPSRWriteByInstr);
    for (i = 0; i < 16; i++) {
        env->regs[i] = regs->uregs[i];
    }
}

static inline void target_cpu_loop(CPUARMState *env)
{
    int trapnr;
    target_siginfo_t info;
    CPUState *cs = env_cpu(env);

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);
        switch (trapnr) {
        case EXCP_UDEF:
            {
                /* See arm/arm/undefined.c undefinedinstruction(); */
                info.si_addr = env->regs[15];

                /* illegal instruction */
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                info.si_code = TARGET_ILL_ILLOPC;
                queue_signal(env, info.si_signo, &info);

                /* TODO: What about instruction emulation? */
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_PREFETCH_ABORT:
            /* See arm/arm/trap.c prefetch_abort_handler() */
        case EXCP_DATA_ABORT:
            /* See arm/arm/trap.c data_abort_handler() */
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            /* XXX: check env->error_code */
            info.si_code = 0;
            info.si_addr = env->exception.vaddress;
            queue_signal(env, info.si_signo, &info);
            break;
        case EXCP_DEBUG:
            {

                info.si_signo = TARGET_SIGTRAP;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                info.si_addr = env->exception.vaddress;
                queue_signal(env, info.si_signo, &info);
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_YIELD:
            /* nothing to do here for user-mode, just resume guest code */
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            cpu_dump_state(cs, stderr, 0);
            abort();
        } /* switch() */
        process_pending_signals(env);
    } /* for (;;) */
}

static inline void target_cpu_clone_regs(CPUARMState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[13] = newsp;
    }
    env->regs[0] = 0;
}

static inline void target_cpu_reset(CPUArchState *cpu)
{
}

#endif /* !_TARGET_ARCH_CPU_H */
