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

void cpu_loop(CPUAlphaState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, si_code;
    abi_long sysret;

    while (1) {
        bool arch_interrupt = true;

        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_RESET:
            fprintf(stderr, "Reset requested. Exit\n");
            exit(EXIT_FAILURE);
            break;
        case EXCP_MCHK:
            fprintf(stderr, "Machine check exception. Exit\n");
            exit(EXIT_FAILURE);
            break;
        case EXCP_SMP_INTERRUPT:
        case EXCP_CLK_INTERRUPT:
        case EXCP_DEV_INTERRUPT:
            fprintf(stderr, "External interrupt. Exit\n");
            exit(EXIT_FAILURE);
            break;
        case EXCP_OPCDEC:
        do_sigill:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->pc);
            break;
        case EXCP_ARITH:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_FLTINV, env->pc);
            break;
        case EXCP_FEN:
            /* No-op.  Linux simply re-enables the FPU.  */
            break;
        case EXCP_CALL_PAL:
            switch (env->error_code) {
            case 0x80:
                /* BPT */
                goto do_sigtrap_brkpt;
            case 0x81:
                /* BUGCHK */
                goto do_sigtrap_unk;
            case 0x83:
                /* CALLSYS */
                trapnr = env->ir[IR_V0];
                sysret = do_syscall(env, trapnr,
                                    env->ir[IR_A0], env->ir[IR_A1],
                                    env->ir[IR_A2], env->ir[IR_A3],
                                    env->ir[IR_A4], env->ir[IR_A5],
                                    0, 0);
                if (sysret == -QEMU_ERESTARTSYS) {
                    env->pc -= 4;
                    break;
                }
                if (sysret == -QEMU_ESIGRETURN) {
                    break;
                }
                /* Syscall writes 0 to V0 to bypass error check, similar
                   to how this is handled internal to Linux kernel.
                   (Ab)use trapnr temporarily as boolean indicating error.  */
                trapnr = (env->ir[IR_V0] != 0 && sysret < 0);
                env->ir[IR_V0] = (trapnr ? -sysret : sysret);
                env->ir[IR_A3] = trapnr;
                break;
            case 0x86:
                /* IMB */
                /* ??? We can probably elide the code using page_unprotect
                   that is checking for self-modifying code.  Instead we
                   could simply call tb_flush here.  Until we work out the
                   changes required to turn off the extra write protection,
                   this can be a no-op.  */
                break;
            case 0x9E:
                /* RDUNIQUE */
                /* Handled in the translator for usermode.  */
                abort();
            case 0x9F:
                /* WRUNIQUE */
                /* Handled in the translator for usermode.  */
                abort();
            case 0xAA:
                /* GENTRAP */
                switch (env->ir[IR_A0]) {
                case TARGET_GEN_INTOVF:
                    si_code = TARGET_FPE_INTOVF;
                    break;
                case TARGET_GEN_INTDIV:
                    si_code = TARGET_FPE_INTDIV;
                    break;
                case TARGET_GEN_FLTOVF:
                    si_code = TARGET_FPE_FLTOVF;
                    break;
                case TARGET_GEN_FLTUND:
                    si_code = TARGET_FPE_FLTUND;
                    break;
                case TARGET_GEN_FLTINV:
                    si_code = TARGET_FPE_FLTINV;
                    break;
                case TARGET_GEN_FLTINE:
                    si_code = TARGET_FPE_FLTRES;
                    break;
                case TARGET_GEN_ROPRAND:
                    si_code = TARGET_FPE_FLTUNK;
                    break;
                default:
                    goto do_sigtrap_unk;
                }
                force_sig_fault(TARGET_SIGFPE, si_code, env->pc);
                break;
            default:
                goto do_sigill;
            }
            break;
        case EXCP_DEBUG:
        do_sigtrap_brkpt:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;
        do_sigtrap_unk:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_UNK, env->pc);
            break;
        case EXCP_INTERRUPT:
            /* Just indicate that signals should be handled asap.  */
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

        /* Most of the traps imply a transition through PALcode, which
           implies an REI instruction has been executed.  Which means
           that RX and LOCK_ADDR should be cleared.  But there are a
           few exceptions for traps internal to QEMU.  */
        if (arch_interrupt) {
            env->flags &= ~ENV_FLAG_RX_FLAG;
            env->lock_addr = -1;
        }
    }
}

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs)
{
    int i;

    for(i = 0; i < 28; i++) {
        env->ir[i] = ((abi_ulong *)regs)[i];
    }
    env->ir[IR_SP] = regs->usp;
    env->pc = regs->pc;
}
