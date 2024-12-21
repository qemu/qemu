/*
 *  ARM AArch64 cpu init and loop
 *
 * Copyright (c) 2015 Stacey Son
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

#ifndef TARGET_ARCH_CPU_H
#define TARGET_ARCH_CPU_H

#include "target_arch.h"
#include "signal-common.h"
#include "target/arm/syndrome.h"

#define TARGET_DEFAULT_CPU_MODEL "any"

static inline void target_cpu_init(CPUARMState *env,
    struct target_pt_regs *regs)
{
    int i;

    if (!(arm_feature(env, ARM_FEATURE_AARCH64))) {
        fprintf(stderr, "The selected ARM CPU does not support 64 bit mode\n");
        exit(1);
    }
    for (i = 0; i < 31; i++) {
        env->xregs[i] = regs->regs[i];
    }
    env->pc = regs->pc;
    env->xregs[31] = regs->sp;
}


static inline G_NORETURN void target_cpu_loop(CPUARMState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, ec, fsc, si_code, si_signo;
    uint64_t code, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8;
    abi_long ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SWI:
            /* See arm64/arm64/trap.c cpu_fetch_syscall_args() */
            code = env->xregs[8];
            if (code == TARGET_FREEBSD_NR_syscall ||
                code == TARGET_FREEBSD_NR___syscall) {
                code = env->xregs[0];
                arg1 = env->xregs[1];
                arg2 = env->xregs[2];
                arg3 = env->xregs[3];
                arg4 = env->xregs[4];
                arg5 = env->xregs[5];
                arg6 = env->xregs[6];
                arg7 = env->xregs[7];
                arg8 = 0;
            } else {
                arg1 = env->xregs[0];
                arg2 = env->xregs[1];
                arg3 = env->xregs[2];
                arg4 = env->xregs[3];
                arg5 = env->xregs[4];
                arg6 = env->xregs[5];
                arg7 = env->xregs[6];
                arg8 = env->xregs[7];
            }
            ret = do_freebsd_syscall(env, code, arg1, arg2, arg3,
                    arg4, arg5, arg6, arg7, arg8);
            /*
             * The carry bit is cleared for no error; set for error.
             * See arm64/arm64/vm_machdep.c cpu_set_syscall_retval()
             */
            if (ret >= 0) {
                env->CF = 0;
                env->xregs[0] = ret;
            } else if (ret == -TARGET_ERESTART) {
                env->pc -= 4;
                break;
            } else if (ret != -TARGET_EJUSTRETURN) {
                env->CF = 1;
                env->xregs[0] = -ret;
            }
            break;

        case EXCP_INTERRUPT:
            /* Just indicate that signals should be handle ASAP. */
            break;

        case EXCP_UDEF:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN, env->pc);
            break;


        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:
            /* We should only arrive here with EC in {DATAABORT, INSNABORT}. */
            ec = syn_get_ec(env->exception.syndrome);
            assert(ec == EC_DATAABORT || ec == EC_INSNABORT);

            /* Both EC have the same format for FSC, or close enough. */
            fsc = extract32(env->exception.syndrome, 0, 6);
            switch (fsc) {
            case 0x04 ... 0x07: /* Translation fault, level {0-3} */
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_MAPERR;
                break;
            case 0x09 ... 0x0b: /* Access flag fault, level {1-3} */
            case 0x0d ... 0x0f: /* Permission fault, level {1-3} */
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_ACCERR;
                break;
            case 0x11: /* Synchronous Tag Check Fault */
                si_signo = TARGET_SIGSEGV;
                si_code = /* TARGET_SEGV_MTESERR; */ TARGET_SEGV_ACCERR;
                break;
            case 0x21: /* Alignment fault */
                si_signo = TARGET_SIGBUS;
                si_code = TARGET_BUS_ADRALN;
                break;
            default:
                g_assert_not_reached();
            }
            force_sig_fault(si_signo, si_code, env->exception.vaddress);
            break;

        case EXCP_DEBUG:
        case EXCP_BKPT:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
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
        /*
         * Exception return on AArch64 always clears the exclusive
         * monitor, so any return to running guest code implies this.
         * A strex (successful or otherwise) also clears the monitor, so
         * we don't need to specialcase EXCP_STREX.
         */
        env->exclusive_addr = -1;
    } /* for (;;) */
}


/* See arm64/arm64/vm_machdep.c cpu_fork() */
static inline void target_cpu_clone_regs(CPUARMState *env, target_ulong newsp)
{
    if (newsp) {
        env->xregs[31] = newsp;
    }
    env->regs[0] = 0;
    env->regs[1] = 0;
    pstate_write(env, 0);
}

static inline void target_cpu_reset(CPUArchState *env)
{
}


#endif /* TARGET_ARCH_CPU_H */
