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
#include "qemu.h"
#include "user-internals.h"
#include "cpu_loop-common.h"
#include "signal-common.h"

void cpu_loop(CPUNios2State *env)
{
    CPUState *cs = env_cpu(env);
    target_siginfo_t info;
    int trapnr, ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;

        case EXCP_TRAP:
            switch (env->error_code) {
            case 0:
                qemu_log_mask(CPU_LOG_INT, "\nSyscall\n");

                ret = do_syscall(env, env->regs[2],
                                 env->regs[4], env->regs[5], env->regs[6],
                                 env->regs[7], env->regs[8], env->regs[9],
                                 0, 0);

                if (env->regs[2] == 0) {    /* FIXME: syscall 0 workaround */
                    ret = 0;
                }

                env->regs[2] = abs(ret);
                /* Return value is 0..4096 */
                env->regs[7] = ret > 0xfffff000u;
                env->regs[R_PC] += 4;
                break;

            case 1:
                qemu_log_mask(CPU_LOG_INT, "\nTrap 1\n");
                force_sig_fault(TARGET_SIGUSR1, 0, env->regs[R_PC]);
                break;
            case 2:
                qemu_log_mask(CPU_LOG_INT, "\nTrap 2\n");
                force_sig_fault(TARGET_SIGUSR2, 0, env->regs[R_PC]);
                break;
            case 31:
                qemu_log_mask(CPU_LOG_INT, "\nTrap 31\n");
                force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->regs[R_PC]);
                break;
            default:
                qemu_log_mask(CPU_LOG_INT, "\nTrap %d\n", env->error_code);
                force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLTRP,
                                env->regs[R_PC]);
                break;

            case 16: /* QEMU specific, for __kuser_cmpxchg */
                {
                    abi_ptr g = env->regs[4];
                    uint32_t *h, n, o;

                    if (g & 0x3) {
                        force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, g);
                        break;
                    }
                    ret = page_get_flags(g);
                    if (!(ret & PAGE_VALID)) {
                        force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_MAPERR, g);
                        break;
                    }
                    if (!(ret & PAGE_READ) || !(ret & PAGE_WRITE)) {
                        force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_ACCERR, g);
                        break;
                    }
                    h = g2h(cs, g);
                    o = env->regs[5];
                    n = env->regs[6];
                    env->regs[2] = qatomic_cmpxchg(h, o, n) - o;
                    env->regs[R_PC] += 4;
                }
                break;
            }
            break;

        case EXCP_DEBUG:
            info.si_signo = TARGET_SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_TRAP_BRKPT;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case 0xaa:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* TODO: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->regs[R_PC];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            abort();
        }

        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    env->regs[0] = 0;
    env->regs[1] = regs->r1;
    env->regs[2] = regs->r2;
    env->regs[3] = regs->r3;
    env->regs[4] = regs->r4;
    env->regs[5] = regs->r5;
    env->regs[6] = regs->r6;
    env->regs[7] = regs->r7;
    env->regs[8] = regs->r8;
    env->regs[9] = regs->r9;
    env->regs[10] = regs->r10;
    env->regs[11] = regs->r11;
    env->regs[12] = regs->r12;
    env->regs[13] = regs->r13;
    env->regs[14] = regs->r14;
    env->regs[15] = regs->r15;
    /* TODO: unsigned long  orig_r2; */
    env->regs[R_RA] = regs->ra;
    env->regs[R_FP] = regs->fp;
    env->regs[R_SP] = regs->sp;
    env->regs[R_GP] = regs->gp;
    env->regs[CR_ESTATUS] = regs->estatus;
    env->regs[R_PC] = regs->ea;
    /* TODO: unsigned long  orig_r7; */
}
