/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch user cpu_loop.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "user/cpu_loop.h"
#include "signal-common.h"

/* Break codes */
enum {
    BRK_OVERFLOW = 6,
    BRK_DIVZERO = 7
};

void cpu_loop(CPULoongArchState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, si_code;
    abi_long ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCCODE_SYS:
            env->pc += 4;
            ret = do_syscall(env, env->gpr[11],
                             env->gpr[4], env->gpr[5],
                             env->gpr[6], env->gpr[7],
                             env->gpr[8], env->gpr[9],
                             -1, -1);
            if (ret == -QEMU_ERESTARTSYS) {
                env->pc -= 4;
                break;
            }
            if (ret == -QEMU_ESIGRETURN) {
                /*
                 * Returning from a successful sigreturn syscall.
                 * Avoid clobbering register state.
                 */
                break;
            }
            env->gpr[4] = ret;
            break;
        case EXCCODE_INE:
            force_sig_fault(TARGET_SIGILL, 0, env->pc);
            break;
        case EXCCODE_FPE:
            si_code = TARGET_FPE_FLTUNK;
            if (GET_FP_CAUSE(env->fcsr0) & FP_INVALID) {
                si_code = TARGET_FPE_FLTINV;
            } else if (GET_FP_CAUSE(env->fcsr0) & FP_DIV0) {
                si_code = TARGET_FPE_FLTDIV;
            } else if (GET_FP_CAUSE(env->fcsr0) & FP_OVERFLOW) {
                si_code = TARGET_FPE_FLTOVF;
            } else if (GET_FP_CAUSE(env->fcsr0) & FP_UNDERFLOW) {
                si_code = TARGET_FPE_FLTUND;
            } else if (GET_FP_CAUSE(env->fcsr0) & FP_INEXACT) {
                si_code = TARGET_FPE_FLTRES;
            }
            force_sig_fault(TARGET_SIGFPE, si_code, env->pc);
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;
        case EXCCODE_BRK:
            {
                unsigned int opcode;

                get_user_u32(opcode, env->pc);

                switch (opcode & 0x7fff) {
                case BRK_OVERFLOW:
                    force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTOVF, env->pc);
                    break;
                case BRK_DIVZERO:
                    force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTDIV, env->pc);
                    break;
                default:
                    force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
                }
            }
            break;
        case EXCCODE_BCE:
            force_sig_fault(TARGET_SIGSYS, TARGET_SI_KERNEL, env->pc);
            break;

        /*
         * Begin with LSX and LASX disabled, then enable on the first trap.
         * In this way we can tell if the unit is in use.  This is used to
         * choose the layout of any signal frame.
         */
        case EXCCODE_SXD:
            env->CSR_EUEN |= R_CSR_EUEN_SXE_MASK;
            break;
        case EXCCODE_ASXD:
            env->CSR_EUEN |= R_CSR_EUEN_ASXE_MASK;
            break;

        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n",
                      trapnr);
            exit(EXIT_FAILURE);
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs)
{
    int i;

    for (i = 0; i < 32; i++) {
        env->gpr[i] = regs->regs[i];
    }
    env->pc = regs->csr.era;

}
