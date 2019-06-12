/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu.h"
#include "cpu_loop-common.h"

void cpu_loop(CPUSH4State *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, ret;
    target_siginfo_t info;

    while (1) {
        bool arch_interrupt = true;

        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case 0x160:
            env->pc += 2;
            ret = do_syscall(env,
                             env->gregs[3],
                             env->gregs[4],
                             env->gregs[5],
                             env->gregs[6],
                             env->gregs[7],
                             env->gregs[0],
                             env->gregs[1],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 2;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->gregs[0] = ret;
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            info.si_signo = TARGET_SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_TRAP_BRKPT;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case 0xa0:
        case 0xc0:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = env->tea;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            arch_interrupt = false;
            break;
        default:
            fprintf(stderr, "Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);

        /* Most of the traps imply an exception or interrupt, which
           implies an REI instruction has been executed.  Which means
           that LDST (aka LOK_ADDR) should be cleared.  But there are
           a few exceptions for traps internal to QEMU.  */
        if (arch_interrupt) {
            env->lock_addr = -1;
        }
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    int i;

    for(i = 0; i < 16; i++) {
        env->gregs[i] = regs->regs[i];
    }
    env->pc = regs->pc;
}
