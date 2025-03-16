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
#include "user/cpu_loop.h"
#include "signal-common.h"
#include "qemu/guest-random.h"
#include "semihosting/common-semi.h"
#include "target/arm/syndrome.h"
#include "target/arm/cpu-features.h"

/* AArch64 main loop */
void cpu_loop(CPUARMState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, ec, fsc, si_code, si_signo;
    abi_long ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SWI:
            /* On syscall, PSTATE.ZA is preserved, PSTATE.SM is cleared. */
            aarch64_set_svcr(env, 0, R_SVCR_SM_MASK);
            ret = do_syscall(env,
                             env->xregs[8],
                             env->xregs[0],
                             env->xregs[1],
                             env->xregs[2],
                             env->xregs[3],
                             env->xregs[4],
                             env->xregs[5],
                             0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -QEMU_ESIGRETURN) {
                env->xregs[0] = ret;
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_UDEF:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN, env->pc);
            break;
        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:
            ec = syn_get_ec(env->exception.syndrome);
            switch (ec) {
            case EC_DATAABORT:
            case EC_INSNABORT:
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
                    si_code = TARGET_SEGV_MTESERR;
                    break;
                case 0x21: /* Alignment fault */
                    si_signo = TARGET_SIGBUS;
                    si_code = TARGET_BUS_ADRALN;
                    break;
                default:
                    g_assert_not_reached();
                }
                break;
            case EC_PCALIGNMENT:
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
        case EXCP_SEMIHOST:
            do_common_semihosting(cs);
            env->pc += 4;
            break;
        case EXCP_YIELD:
            /* nothing to do here for user-mode, just resume guest code */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }

        /* Check for MTE asynchronous faults */
        if (unlikely(env->cp15.tfsr_el[0])) {
            env->cp15.tfsr_el[0] = 0;
            force_sig_fault(TARGET_SIGSEGV, TARGET_SEGV_MTEAERR, 0);
        }

        process_pending_signals(env);
        /* Exception return on AArch64 always clears the exclusive monitor,
         * so any return to running guest code implies this.
         */
        env->exclusive_addr = -1;
    }
}

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    TaskState *ts = get_task_state(cs);
    struct image_info *info = ts->info;
    int i;

    if (!(arm_feature(env, ARM_FEATURE_AARCH64))) {
        fprintf(stderr,
                "The selected ARM CPU does not support 64 bit mode\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < 31; i++) {
        env->xregs[i] = regs->regs[i];
    }
    env->pc = regs->pc;
    env->xregs[31] = regs->sp;
#if TARGET_BIG_ENDIAN
    env->cp15.sctlr_el[1] |= SCTLR_E0E;
    for (i = 1; i < 4; ++i) {
        env->cp15.sctlr_el[i] |= SCTLR_EE;
    }
    arm_rebuild_hflags(env);
#endif

    if (cpu_isar_feature(aa64_pauth, cpu)) {
        qemu_guest_getrandom_nofail(&env->keys, sizeof(env->keys));
    }

    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
}
