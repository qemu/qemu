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

void cpu_loop(CPUMBState *env)
{
    int trapnr, ret, si_code, sig;
    CPUState *cs = env_cpu(env);

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_SYSCALL:
            /* Return address is 4 bytes after the call.  */
            env->regs[14] += 4;
            env->pc = env->regs[14];
            ret = do_syscall(env, 
                             env->regs[12], 
                             env->regs[5], 
                             env->regs[6], 
                             env->regs[7], 
                             env->regs[8], 
                             env->regs[9], 
                             env->regs[10],
                             0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                /* Wind back to before the syscall. */
                env->pc -= 4;
            } else if (ret != -QEMU_ESIGRETURN) {
                env->regs[3] = ret;
            }
            /* All syscall exits result in guest r14 being equal to the
             * PC we return to, because the kernel syscall exit "rtbd" does
             * this. (This is true even for sigreturn(); note that r14 is
             * not a userspace-usable register, as the kernel may clobber it
             * at any point.)
             */
            env->regs[14] = env->pc;
            break;

        case EXCP_HW_EXCP:
            env->regs[17] = env->pc + 4;
            if (env->iflags & D_FLAG) {
                env->esr |= 1 << 12;
                env->pc -= 4;
                /* FIXME: if branch was immed, replay the imm as well.  */
            }
            env->iflags &= ~(IMM_FLAG | D_FLAG);
            switch (env->esr & 31) {
            case ESR_EC_DIVZERO:
                sig = TARGET_SIGFPE;
                si_code = TARGET_FPE_INTDIV;
                break;
            case ESR_EC_FPU:
                /*
                 * Note that the kernel passes along fsr as si_code
                 * if there's no recognized bit set.  Possibly this
                 * implies that si_code is 0, but follow the structure.
                 */
                sig = TARGET_SIGFPE;
                si_code = env->fsr;
                if (si_code & FSR_IO) {
                    si_code = TARGET_FPE_FLTINV;
                } else if (si_code & FSR_OF) {
                    si_code = TARGET_FPE_FLTOVF;
                } else if (si_code & FSR_UF) {
                    si_code = TARGET_FPE_FLTUND;
                } else if (si_code & FSR_DZ) {
                    si_code = TARGET_FPE_FLTDIV;
                } else if (si_code & FSR_DO) {
                    si_code = TARGET_FPE_FLTRES;
                }
                break;
            case ESR_EC_PRIVINSN:
                sig = SIGILL;
                si_code = ILL_PRVOPC;
                break;
            default:
                fprintf(stderr, "Unhandled hw-exception: 0x%x\n",
                        env->esr & ESR_EC_MASK);
                cpu_dump_state(cs, stderr, 0);
                exit(EXIT_FAILURE);
            }
            force_sig_fault(sig, si_code, env->pc);
            break;

        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
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

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs)
{
    env->regs[0] = regs->r0;
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
    env->regs[16] = regs->r16;
    env->regs[17] = regs->r17;
    env->regs[18] = regs->r18;
    env->regs[19] = regs->r19;
    env->regs[20] = regs->r20;
    env->regs[21] = regs->r21;
    env->regs[22] = regs->r22;
    env->regs[23] = regs->r23;
    env->regs[24] = regs->r24;
    env->regs[25] = regs->r25;
    env->regs[26] = regs->r26;
    env->regs[27] = regs->r27;
    env->regs[28] = regs->r28;
    env->regs[29] = regs->r29;
    env->regs[30] = regs->r30;
    env->regs[31] = regs->r31;
    env->pc = regs->pc;
}
