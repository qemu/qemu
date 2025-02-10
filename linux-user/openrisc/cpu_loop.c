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

void cpu_loop(CPUOpenRISCState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    abi_long ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SYSCALL:
            env->pc += 4;   /* 0xc00; */
            ret = do_syscall(env,
                             cpu_get_gpr(env, 11), /* return value       */
                             cpu_get_gpr(env, 3),  /* r3 - r7 are params */
                             cpu_get_gpr(env, 4),
                             cpu_get_gpr(env, 5),
                             cpu_get_gpr(env, 6),
                             cpu_get_gpr(env, 7),
                             cpu_get_gpr(env, 8), 0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -QEMU_ESIGRETURN) {
                cpu_set_gpr(env, 11, ret);
            }
            break;
        case EXCP_ALIGN:
            force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, env->eear);
            break;
        case EXCP_ILLEGAL:
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->pc);
            break;
        case EXCP_INTERRUPT:
            /* We processed the pending cpu work above.  */
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->pc);
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_RANGE:
            /* Requires SR.OVE set, which linux-user won't do. */
            cpu_abort(cs, "Unexpected RANGE exception");
        case EXCP_FPE:
            /*
             * Requires FPSCR.FPEE set.  Writes to FPSCR from usermode not
             * yet enabled in kernel ABI, so linux-user does not either.
             */
            cpu_abort(cs, "Unexpected FPE exception");
        default:
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs)
{
    int i;

    for (i = 0; i < 32; i++) {
        cpu_set_gpr(env, i, regs->gpr[i]);
    }
    env->pc = regs->pc;
    cpu_set_sr(env, regs->sr);
}
