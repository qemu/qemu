/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "cpu_loop-common.h"
#include "signal-common.h"
#include "internal.h"

void cpu_loop(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr;
    target_ulong syscallnum;
    target_ulong ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case HEX_EXCP_TRAP0:
            syscallnum = env->gpr[6];
            env->gpr[HEX_REG_PC] += 4;
            ret = do_syscall(env,
                             syscallnum,
                             env->gpr[0],
                             env->gpr[1],
                             env->gpr[2],
                             env->gpr[3],
                             env->gpr[4],
                             env->gpr[5],
                             0, 0);
            if (ret == -QEMU_ERESTARTSYS) {
                env->gpr[HEX_REG_PC] -= 4;
            } else if (ret != -QEMU_ESIGRETURN) {
                env->gpr[0] = ret;
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, 0);
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            exit(EXIT_FAILURE);
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    env->gpr[HEX_REG_PC] = regs->sepc;
    env->gpr[HEX_REG_SP] = regs->sp;
    env->gpr[HEX_REG_USR] = 0x56000;
}
