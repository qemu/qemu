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
#include "user-internals.h"
#include "cpu_loop-common.h"
#include "signal-common.h"

void cpu_loop(CPUM68KState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    unsigned int n;

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch(trapnr) {
        case EXCP_HALT_INSN:
            /* Semihosing syscall.  */
            env->pc += 4;
            do_m68k_semihosting(env, env->dregs[0]);
            break;
        case EXCP_ILLEGAL:
        case EXCP_LINEA:
        case EXCP_LINEF:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN, env->pc);
            break;
        case EXCP_CHK:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTOVF, env->pc);
            break;
        case EXCP_DIV0:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTDIV, env->pc);
            break;
        case EXCP_TRAP0:
            {
                abi_long ret;
                n = env->dregs[0];
                env->pc += 2;
                ret = do_syscall(env,
                                 n,
                                 env->dregs[1],
                                 env->dregs[2],
                                 env->dregs[3],
                                 env->dregs[4],
                                 env->dregs[5],
                                 env->aregs[0],
                                 0, 0);
                if (ret == -QEMU_ERESTARTSYS) {
                    env->pc -= 2;
                } else if (ret != -QEMU_ESIGRETURN) {
                    env->dregs[0] = ret;
                }
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    struct image_info *info = ts->info;

    env->pc = regs->pc;
    env->dregs[0] = regs->d0;
    env->dregs[1] = regs->d1;
    env->dregs[2] = regs->d2;
    env->dregs[3] = regs->d3;
    env->dregs[4] = regs->d4;
    env->dregs[5] = regs->d5;
    env->dregs[6] = regs->d6;
    env->dregs[7] = regs->d7;
    env->aregs[0] = regs->a0;
    env->aregs[1] = regs->a1;
    env->aregs[2] = regs->a2;
    env->aregs[3] = regs->a3;
    env->aregs[4] = regs->a4;
    env->aregs[5] = regs->a5;
    env->aregs[6] = regs->a6;
    env->aregs[7] = regs->usp;
    env->sr = regs->sr;

    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
}
