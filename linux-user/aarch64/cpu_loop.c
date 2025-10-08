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

/* Use the exception syndrome to map a cpu exception to a signal. */
static void signal_for_exception(CPUARMState *env, vaddr addr)
{
    uint32_t syn = env->exception.syndrome;
    int si_code, si_signo;

    /* Let signal delivery see that ESR is live. */
    env->cp15.esr_el[1] = syn;

    switch (syn_get_ec(syn)) {
    case EC_DATAABORT:
    case EC_INSNABORT:
        /* Both EC have the same format for FSC, or close enough. */
        switch (extract32(syn, 0, 6)) {
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

    case EC_UNCATEGORIZED:         /* E.g. undefined instruction */
    case EC_SYSTEMREGISTERTRAP:    /* E.g. inaccessible register */
    case EC_SMETRAP:               /* E.g. invalid insn in streaming state */
    case EC_BTITRAP:               /* E.g. invalid guarded branch target */
    case EC_ILLEGALSTATE:
        /*
         * Illegal state happens via an ERET from a privileged mode,
         * so is not normally possible from user-only.  However, gdbstub
         * is not prevented from writing CPSR_IL, aka PSTATE.IL, which
         * would generate a trap from the next translated block.
         * In the kernel, default case -> el0_inv -> bad_el0_sync.
         */
        si_signo = TARGET_SIGILL;
        si_code = TARGET_ILL_ILLOPC;
        break;

    case EC_PACFAIL:
        si_signo = TARGET_SIGILL;
        si_code = TARGET_ILL_ILLOPN;
        break;

    case EC_GCS:
        si_signo = TARGET_SIGSEGV;
        si_code = TARGET_SEGV_CPERR;
        break;

    case EC_MOP:
        /*
         * FIXME: The kernel fixes up wrong-option exceptions.
         * For QEMU linux-user mode, you can only get these if
         * the process is doing something silly (not executing
         * the MOPS instructions in the required P/M/E sequence),
         * so it is not a problem in practice that we do not.
         *
         * We ought ideally to implement the same "rewind to the
         * start of the sequence" logic that the kernel does in
         * arm64_mops_reset_regs(). In the meantime, deliver
         * the guest a SIGILL, with the same ILLOPN si_code
         * we've always used for this.
         */
        si_signo = TARGET_SIGILL;
        si_code = TARGET_ILL_ILLOPN;
        break;

    case EC_WFX_TRAP:              /* user-only WFI implemented as NOP */
    case EC_CP15RTTRAP:            /* AArch32 */
    case EC_CP15RRTTRAP:           /* AArch32 */
    case EC_CP14RTTRAP:            /* AArch32 */
    case EC_CP14DTTRAP:            /* AArch32 */
    case EC_ADVSIMDFPACCESSTRAP:   /* user-only does not disable fpu */
    case EC_FPIDTRAP:              /* AArch32 */
    case EC_PACTRAP:               /* user-only does not disable pac regs */
    case EC_BXJTRAP:               /* AArch32 */
    case EC_CP14RRTTRAP:           /* AArch32 */
    case EC_AA32_SVC:              /* AArch32 */
    case EC_AA32_HVC:              /* AArch32 */
    case EC_AA32_SMC:              /* AArch32 */
    case EC_AA64_SVC:              /* generates EXCP_SWI */
    case EC_AA64_HVC:              /* user-only generates EC_UNCATEGORIZED */
    case EC_AA64_SMC:              /* user-only generates EC_UNCATEGORIZED */
    case EC_SVEACCESSTRAP:         /* user-only does not disable sve */
    case EC_ERETTRAP:              /* user-only generates EC_UNCATEGORIZED */
    case EC_GPC:                   /* user-only has no EL3 gpc tables */
    case EC_INSNABORT_SAME_EL:     /* el0 cannot trap to el0 */
    case EC_DATAABORT_SAME_EL:     /* el0 cannot trap to el0 */
    case EC_SPALIGNMENT:           /* sp alignment checks not implemented */
    case EC_AA32_FPTRAP:           /* fp exceptions not implemented */
    case EC_AA64_FPTRAP:           /* fp exceptions not implemented */
    case EC_SERROR:                /* user-only does not have hw faults */
    case EC_BREAKPOINT:            /* user-only does not have hw debug */
    case EC_BREAKPOINT_SAME_EL:    /* user-only does not have hw debug */
    case EC_SOFTWARESTEP:          /* user-only does not have hw debug */
    case EC_SOFTWARESTEP_SAME_EL:  /* user-only does not have hw debug */
    case EC_WATCHPOINT:            /* user-only does not have hw debug */
    case EC_WATCHPOINT_SAME_EL:    /* user-only does not have hw debug */
    case EC_AA32_BKPT:             /* AArch32 */
    case EC_VECTORCATCH:           /* AArch32 */
    case EC_AA64_BKPT:             /* generates EXCP_BKPT */
    default:
        g_assert_not_reached();
    }

    force_sig_fault(si_signo, si_code, addr);
}

/* AArch64 main loop */
void cpu_loop(CPUARMState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_long ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

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
            signal_for_exception(env, env->pc);
            break;
        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:
            signal_for_exception(env, env->exception.vaddress);
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

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUARMState *env = cpu_env(cs);
    ARMCPU *cpu = env_archcpu(env);

    if (!(arm_feature(env, ARM_FEATURE_AARCH64))) {
        fprintf(stderr,
                "The selected ARM CPU does not support 64 bit mode\n");
        exit(EXIT_FAILURE);
    }

    env->pc = info->entry & ~0x3ULL;
    env->xregs[31] = info->start_stack;

#if TARGET_BIG_ENDIAN
    env->cp15.sctlr_el[1] |= SCTLR_E0E;
    for (int i = 1; i < 4; ++i) {
        env->cp15.sctlr_el[i] |= SCTLR_EE;
    }
    arm_rebuild_hflags(env);
#endif

    if (cpu_isar_feature(aa64_pauth, cpu)) {
        qemu_guest_getrandom_nofail(&env->keys, sizeof(env->keys));
    }
}
