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

void cpu_loop(CPUM68KState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    unsigned int n;

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);

        switch(trapnr) {
        case EXCP_ILLEGAL:
        case EXCP_LINEA:
        case EXCP_LINEF:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN, env->pc);
            break;
        case EXCP_CHK:
        case EXCP_TRAPCC:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTOVF, env->mmu.ar);
            break;
        case EXCP_DIV0:
            force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTDIV, env->mmu.ar);
            break;
        case EXCP_TRACE:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_TRACE, env->mmu.ar);
            break;
        case EXCP_TRAP0:
            {
                abi_long ret;
                n = env->dregs[0];
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
        case EXCP_TRAP0 + 1 ... EXCP_TRAP0 + 14:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLTRP, env->pc);
            break;
        case EXCP_DEBUG:
        case EXCP_TRAP15:
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

void init_main_thread(CPUState *cs, struct image_info *info)
{
    CPUArchState *env = cpu_env(cs);

    env->pc = info->entry;
    env->aregs[7] = info->start_stack;
    env->sr = 0;
}
