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

/* s390x masks the fault address it reports in si_addr for SIGSEGV and SIGBUS */
#define S390X_FAIL_ADDR_MASK -4096LL

void cpu_loop(CPUS390XState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, n, sig;
    target_siginfo_t info;
    target_ulong addr;
    abi_long ret;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* Just indicate that signals should be handled asap.  */
            break;

        case EXCP_SVC:
            n = env->int_svc_code;
            if (!n) {
                /* syscalls > 255 */
                n = env->regs[1];
            }
            env->psw.addr += env->int_svc_ilen;
            ret = do_syscall(env, n, env->regs[2], env->regs[3],
                             env->regs[4], env->regs[5],
                             env->regs[6], env->regs[7], 0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->psw.addr -= env->int_svc_ilen;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[2] = ret;
            }
            break;

        case EXCP_DEBUG:
            sig = TARGET_SIGTRAP;
            n = TARGET_TRAP_BRKPT;
            goto do_signal_pc;
        case EXCP_PGM:
            n = env->int_pgm_code;
            switch (n) {
            case PGM_OPERATION:
            case PGM_PRIVILEGED:
                sig = TARGET_SIGILL;
                n = TARGET_ILL_ILLOPC;
                goto do_signal_pc;
            case PGM_PROTECTION:
            case PGM_ADDRESSING:
                sig = TARGET_SIGSEGV;
                /* XXX: check env->error_code */
                n = TARGET_SEGV_MAPERR;
                addr = env->__excp_addr & S390X_FAIL_ADDR_MASK;
                goto do_signal;
            case PGM_EXECUTE:
            case PGM_SPECIFICATION:
            case PGM_SPECIAL_OP:
            case PGM_OPERAND:
            do_sigill_opn:
                sig = TARGET_SIGILL;
                n = TARGET_ILL_ILLOPN;
                goto do_signal_pc;

            case PGM_FIXPT_OVERFLOW:
                sig = TARGET_SIGFPE;
                n = TARGET_FPE_INTOVF;
                goto do_signal_pc;
            case PGM_FIXPT_DIVIDE:
                sig = TARGET_SIGFPE;
                n = TARGET_FPE_INTDIV;
                goto do_signal_pc;

            case PGM_DATA:
                n = (env->fpc >> 8) & 0xff;
                if (n == 0xff) {
                    /* compare-and-trap */
                    goto do_sigill_opn;
                } else {
                    /* An IEEE exception, simulated or otherwise.  */
                    if (n & 0x80) {
                        n = TARGET_FPE_FLTINV;
                    } else if (n & 0x40) {
                        n = TARGET_FPE_FLTDIV;
                    } else if (n & 0x20) {
                        n = TARGET_FPE_FLTOVF;
                    } else if (n & 0x10) {
                        n = TARGET_FPE_FLTUND;
                    } else if (n & 0x08) {
                        n = TARGET_FPE_FLTRES;
                    } else {
                        /* ??? Quantum exception; BFP, DFP error.  */
                        goto do_sigill_opn;
                    }
                    sig = TARGET_SIGFPE;
                    goto do_signal_pc;
                }

            default:
                fprintf(stderr, "Unhandled program exception: %#x\n", n);
                cpu_dump_state(cs, stderr, 0);
                exit(EXIT_FAILURE);
            }
            break;

        do_signal_pc:
            addr = env->psw.addr;
        do_signal:
            info.si_signo = sig;
            info.si_errno = 0;
            info.si_code = n;
            info._sifields._sigfault._addr = addr;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;

        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            fprintf(stderr, "Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    int i;
    for (i = 0; i < 16; i++) {
        env->regs[i] = regs->gprs[i];
    }
    env->psw.mask = regs->psw.mask;
    env->psw.addr = regs->psw.addr;
}
