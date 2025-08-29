/*
 *  RISC-V CPU init and loop
 *
 *  Copyright (c) 2019 Mark Corbin
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

#define TARGET_DEFAULT_CPU_MODEL "max"

static inline void target_cpu_init(CPURISCVState *env,
        struct target_pt_regs *regs)
{
    int i;

    for (i = 1; i < 32; i++) {
        env->gpr[i] = regs->regs[i];
    }

    env->pc = regs->sepc;
}

static inline G_NORETURN void target_cpu_loop(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_long ret;
    unsigned int syscall_num;
    int32_t signo, code;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        signo = 0;

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case RISCV_EXCP_U_ECALL:
            syscall_num = env->gpr[xT0];
            env->pc += TARGET_INSN_SIZE;
            /* Compare to cpu_fetch_syscall_args() in riscv/riscv/trap.c */
            if (TARGET_FREEBSD_NR___syscall == syscall_num ||
                TARGET_FREEBSD_NR_syscall == syscall_num) {
                ret = do_freebsd_syscall(env,
                                         env->gpr[xA0],
                                         env->gpr[xA1],
                                         env->gpr[xA2],
                                         env->gpr[xA3],
                                         env->gpr[xA4],
                                         env->gpr[xA5],
                                         env->gpr[xA6],
                                         env->gpr[xA7],
                                         0);
            } else {
                ret = do_freebsd_syscall(env,
                                         syscall_num,
                                         env->gpr[xA0],
                                         env->gpr[xA1],
                                         env->gpr[xA2],
                                         env->gpr[xA3],
                                         env->gpr[xA4],
                                         env->gpr[xA5],
                                         env->gpr[xA6],
                                         env->gpr[xA7]
                    );
            }

            /*
             * Compare to cpu_set_syscall_retval() in
             * riscv/riscv/vm_machdep.c
             */
            if (ret >= 0) {
                env->gpr[xA0] = ret;
                env->gpr[xT0] = 0;
            } else if (ret == -TARGET_ERESTART) {
                env->pc -= TARGET_INSN_SIZE;
            } else if (ret != -TARGET_EJUSTRETURN) {
                env->gpr[xA0] = -ret;
                env->gpr[xT0] = 1;
            }
            break;
        case RISCV_EXCP_ILLEGAL_INST:
            signo = TARGET_SIGILL;
            code = TARGET_ILL_ILLOPC;
            break;
        case RISCV_EXCP_BREAKPOINT:
            signo = TARGET_SIGTRAP;
            code = TARGET_TRAP_BRKPT;
            break;
        case EXCP_DEBUG:
            signo = TARGET_SIGTRAP;
            code = TARGET_TRAP_BRKPT;
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception "
                "0x%x - aborting\n", trapnr);
            cpu_dump_state(cs, stderr, 0);
            abort();
        }

        if (signo) {
            force_sig_fault(signo, code, env->pc);
        }

        process_pending_signals(env);
    }
}

static inline void target_cpu_clone_regs(CPURISCVState *env, target_ulong newsp)
{
    if (newsp) {
        env->gpr[xSP] = newsp;
    }

    env->gpr[xA0] = 0;
    env->gpr[xT0] = 0;
}

static inline void target_cpu_reset(CPUArchState *env)
{
}

#endif /* TARGET_ARCH_CPU_H */
