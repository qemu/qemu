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
#include "cpu_loop-common.h"
#include "signal-common.h"

static void xtensa_rfw(CPUXtensaState *env)
{
    xtensa_restore_owb(env);
    env->pc = env->sregs[EPC1];
}

static void xtensa_rfwu(CPUXtensaState *env)
{
    env->sregs[WINDOW_START] |= (1 << env->sregs[WINDOW_BASE]);
    xtensa_rfw(env);
}

static void xtensa_rfwo(CPUXtensaState *env)
{
    env->sregs[WINDOW_START] &= ~(1 << env->sregs[WINDOW_BASE]);
    xtensa_rfw(env);
}

static void xtensa_overflow4(CPUXtensaState *env)
{
    put_user_ual(env->regs[0], env->regs[5] - 16);
    put_user_ual(env->regs[1], env->regs[5] - 12);
    put_user_ual(env->regs[2], env->regs[5] -  8);
    put_user_ual(env->regs[3], env->regs[5] -  4);
    xtensa_rfwo(env);
}

static void xtensa_underflow4(CPUXtensaState *env)
{
    get_user_ual(env->regs[0], env->regs[5] - 16);
    get_user_ual(env->regs[1], env->regs[5] - 12);
    get_user_ual(env->regs[2], env->regs[5] -  8);
    get_user_ual(env->regs[3], env->regs[5] -  4);
    xtensa_rfwu(env);
}

static void xtensa_overflow8(CPUXtensaState *env)
{
    put_user_ual(env->regs[0], env->regs[9] - 16);
    get_user_ual(env->regs[0], env->regs[1] - 12);
    put_user_ual(env->regs[1], env->regs[9] - 12);
    put_user_ual(env->regs[2], env->regs[9] -  8);
    put_user_ual(env->regs[3], env->regs[9] -  4);
    put_user_ual(env->regs[4], env->regs[0] - 32);
    put_user_ual(env->regs[5], env->regs[0] - 28);
    put_user_ual(env->regs[6], env->regs[0] - 24);
    put_user_ual(env->regs[7], env->regs[0] - 20);
    xtensa_rfwo(env);
}

static void xtensa_underflow8(CPUXtensaState *env)
{
    get_user_ual(env->regs[0], env->regs[9] - 16);
    get_user_ual(env->regs[1], env->regs[9] - 12);
    get_user_ual(env->regs[2], env->regs[9] -  8);
    get_user_ual(env->regs[7], env->regs[1] - 12);
    get_user_ual(env->regs[3], env->regs[9] -  4);
    get_user_ual(env->regs[4], env->regs[7] - 32);
    get_user_ual(env->regs[5], env->regs[7] - 28);
    get_user_ual(env->regs[6], env->regs[7] - 24);
    get_user_ual(env->regs[7], env->regs[7] - 20);
    xtensa_rfwu(env);
}

static void xtensa_overflow12(CPUXtensaState *env)
{
    put_user_ual(env->regs[0],  env->regs[13] - 16);
    get_user_ual(env->regs[0],  env->regs[1]  - 12);
    put_user_ual(env->regs[1],  env->regs[13] - 12);
    put_user_ual(env->regs[2],  env->regs[13] -  8);
    put_user_ual(env->regs[3],  env->regs[13] -  4);
    put_user_ual(env->regs[4],  env->regs[0]  - 48);
    put_user_ual(env->regs[5],  env->regs[0]  - 44);
    put_user_ual(env->regs[6],  env->regs[0]  - 40);
    put_user_ual(env->regs[7],  env->regs[0]  - 36);
    put_user_ual(env->regs[8],  env->regs[0]  - 32);
    put_user_ual(env->regs[9],  env->regs[0]  - 28);
    put_user_ual(env->regs[10], env->regs[0]  - 24);
    put_user_ual(env->regs[11], env->regs[0]  - 20);
    xtensa_rfwo(env);
}

static void xtensa_underflow12(CPUXtensaState *env)
{
    get_user_ual(env->regs[0],  env->regs[13] - 16);
    get_user_ual(env->regs[1],  env->regs[13] - 12);
    get_user_ual(env->regs[2],  env->regs[13] -  8);
    get_user_ual(env->regs[11], env->regs[1]  - 12);
    get_user_ual(env->regs[3],  env->regs[13] -  4);
    get_user_ual(env->regs[4],  env->regs[11] - 48);
    get_user_ual(env->regs[5],  env->regs[11] - 44);
    get_user_ual(env->regs[6],  env->regs[11] - 40);
    get_user_ual(env->regs[7],  env->regs[11] - 36);
    get_user_ual(env->regs[8],  env->regs[11] - 32);
    get_user_ual(env->regs[9],  env->regs[11] - 28);
    get_user_ual(env->regs[10], env->regs[11] - 24);
    get_user_ual(env->regs[11], env->regs[11] - 20);
    xtensa_rfwu(env);
}

void cpu_loop(CPUXtensaState *env)
{
    CPUState *cs = env_cpu(env);
    abi_ulong ret;
    int trapnr;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        env->sregs[PS] &= ~PS_EXCM;
        switch (trapnr) {
        case EXCP_INTERRUPT:
            break;

        case EXC_WINDOW_OVERFLOW4:
            xtensa_overflow4(env);
            break;
        case EXC_WINDOW_UNDERFLOW4:
            xtensa_underflow4(env);
            break;
        case EXC_WINDOW_OVERFLOW8:
            xtensa_overflow8(env);
            break;
        case EXC_WINDOW_UNDERFLOW8:
            xtensa_underflow8(env);
            break;
        case EXC_WINDOW_OVERFLOW12:
            xtensa_overflow12(env);
            break;
        case EXC_WINDOW_UNDERFLOW12:
            xtensa_underflow12(env);
            break;

        case EXC_USER:
            switch (env->sregs[EXCCAUSE]) {
            case ILLEGAL_INSTRUCTION_CAUSE:
                force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC,
                                env->sregs[EPC1]);
                break;
            case PRIVILEGED_CAUSE:
                force_sig_fault(TARGET_SIGILL, TARGET_ILL_PRVOPC,
                                env->sregs[EPC1]);
                break;

            case SYSCALL_CAUSE:
                env->pc += 3;
                ret = do_syscall(env, env->regs[2],
                                 env->regs[6], env->regs[3],
                                 env->regs[4], env->regs[5],
                                 env->regs[8], env->regs[9], 0, 0);
                switch (ret) {
                default:
                    env->regs[2] = ret;
                    break;

                case -QEMU_ERESTARTSYS:
                    env->pc -= 3;
                    break;

                case -QEMU_ESIGRETURN:
                    break;
                }
                break;

            case ALLOCA_CAUSE:
                env->sregs[PS] = deposit32(env->sregs[PS],
                                           PS_OWB_SHIFT,
                                           PS_OWB_LEN,
                                           env->sregs[WINDOW_BASE]);

                switch (env->regs[0] & 0xc0000000) {
                case 0x00000000:
                case 0x40000000:
                    xtensa_rotate_window(env, -1);
                    xtensa_underflow4(env);
                    break;

                case 0x80000000:
                    xtensa_rotate_window(env, -2);
                    xtensa_underflow8(env);
                    break;

                case 0xc0000000:
                    xtensa_rotate_window(env, -3);
                    xtensa_underflow12(env);
                    break;
                }
                break;

            case INTEGER_DIVIDE_BY_ZERO_CAUSE:
                force_sig_fault(TARGET_SIGFPE, TARGET_FPE_INTDIV,
                                env->sregs[EPC1]);
                break;

            default:
                fprintf(stderr, "exccause = %d\n", env->sregs[EXCCAUSE]);
                g_assert_not_reached();
            }
            break;
        case EXCP_DEBUG:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT,
                            env->sregs[EPC1]);
            break;
        case EXC_DEBUG:
        default:
            fprintf(stderr, "trapnr = %d\n", trapnr);
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    int i;
    for (i = 0; i < 16; ++i) {
        env->regs[i] = regs->areg[i];
    }
    env->sregs[WINDOW_START] = regs->windowstart;
    env->pc = regs->pc;
}
