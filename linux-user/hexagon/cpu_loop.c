/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "cpu_loop-common.h"
#include "translate.h"

static int do_store_exclusive(CPUHexagonState *env, bool single)
{
    target_ulong addr;
    target_ulong val;
    uint64_t val_i64;
    int segv = 0;
    int reg;

    addr = env->llsc_addr;
    reg = env->llsc_reg;

    start_exclusive();
    mmap_lock();

#ifdef DEBUG_HEX
    print_thread_prefix(env);
    printf("Store exclusive: addr = 0x%x, reg = %d, init val = %d\n",
           addr, reg, env->pred[reg]);
#endif

    env->pred[reg] = 0;
    if (single) {
        segv = get_user_s32(val, addr);
    } else {
        segv = get_user_s64(val_i64, addr);
    }
    if (!segv) {
        if (single) {
            if (val == env->llsc_val) {
                segv = put_user_u32(env->llsc_newval, addr);
                if (!segv) {
                    env->pred[reg] = 0xff;
                }
            }
        } else {
            if (val_i64 == env->llsc_val_i64) {
                segv = put_user_u64(env->llsc_newval_i64, addr);
                if (!segv) {
                    env->pred[reg] = 0xff;
                }
            }
        }
    }
    env->llsc_addr = ~0;
    if (!segv) {
        env->next_PC += 4;
    }

#ifdef DEBUG_HEX
    printf("\t final val = %d\n", env->pred[reg]);
#endif

    mmap_unlock();
    end_exclusive();
    return segv;
}

void cpu_loop(CPUHexagonState *env)
{
    CPUState *cs = CPU(hexagon_env_get_cpu(env));
    target_siginfo_t info;
    int trapnr, signum, sigcode;
    target_ulong sigaddr;
    target_ulong syscallnum;
    target_ulong ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        signum = 0;
        sigcode = 0;
        sigaddr = 0;

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_DEBUG:
            signum = gdb_handlesig(cs, TARGET_SIGTRAP);
            sigcode = TARGET_TRAP_BRKPT;
            break;
        case HEX_EXCP_TRAP0:
            syscallnum = env->gpr[6];
            env->gpr[HEX_REG_PC] += 4;
#ifdef DEBUG_HEX
            printf("syscall %d (%d, %d, %d, %d, %d, %d)\n",
                             syscallnum,
                             env->gpr[0],
                             env->gpr[1],
                             env->gpr[2],
                             env->gpr[3],
                             env->gpr[4],
                             env->gpr[5]);
#endif
#ifdef COUNT_HEX_HELPERS
            if (syscallnum == TARGET_NR_exit_group) {
                print_helper_counts();
            }
#endif
            ret = do_syscall(env,
                             syscallnum,
                             env->gpr[0],
                             env->gpr[1],
                             env->gpr[2],
                             env->gpr[3],
                             env->gpr[4],
                             env->gpr[5],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
#ifdef DEBUG_HEX
                printf("\tSyscall %d returned -TARGET_ERESTARTSYS\n",
                    syscallnum);
#endif
                env->gpr[HEX_REG_PC] -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
#ifdef DEBUG_HEX
                printf("\tSyscall %d returned %d\n", syscallnum, ret);
#endif
                env->gpr[0] = ret;
            }
#ifdef DEBUG_HEX
            else {
                printf("\tSyscall %d returned -TARGET_QEMU_ESIGRETURN\n",
                       syscallnum);
            }
#endif
            break;
        case HEX_EXCP_TRAP1:
            EXCP_DUMP(env, "\nqemu: trap1 exception %#x - aborting\n",
                     trapnr);
            exit(EXIT_FAILURE);
            break;
        case HEX_EXCP_SC4:
            if (do_store_exclusive(env, true)) {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->next_PC;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case HEX_EXCP_SC8:
            if (do_store_exclusive(env, false)) {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->next_PC;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case HEX_EXCP_FETCH_NO_UPAGE:
        case HEX_EXCP_PRIV_NO_UREAD:
        case HEX_EXCP_PRIV_NO_UWRITE:
            signum = TARGET_SIGSEGV;
            sigcode = TARGET_SEGV_MAPERR;
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            exit(EXIT_FAILURE);
        }

        if (signum) {
            target_siginfo_t info = {
                .si_signo = signum,
                .si_errno = 0,
                .si_code = sigcode,
                ._sifields._sigfault._addr = sigaddr
            };
            queue_signal(env, info.si_signo, QEMU_SI_KILL, &info);
        }

        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    env->gpr[HEX_REG_PC] = regs->sepc;
    env->gpr[HEX_REG_SP] = regs->sp;
}
