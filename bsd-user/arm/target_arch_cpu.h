/*
 *  arm cpu init and loop
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef _TARGET_ARCH_CPU_H_
#define _TARGET_ARCH_CPU_H_

#include "target_arch.h"

#define TARGET_DEFAULT_CPU_MODEL "any"

static inline void target_cpu_init(CPUARMState *env,
        struct target_pt_regs *regs)
{
    int i;

    cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
               CPSRWriteByInstr);
    for (i = 0; i < 16; i++) {
        env->regs[i] = regs->uregs[i];
    }
}

static inline void target_cpu_loop(CPUARMState *env)
{
    int trapnr;
    target_siginfo_t info;
    unsigned int n;
    CPUState *cs = env_cpu(env);

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);
        switch (trapnr) {
        case EXCP_UDEF:
            {
                /* See arm/arm/undefined.c undefinedinstruction(); */
                info.si_addr = env->regs[15];

                /* illegal instruction */
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                info.si_code = TARGET_ILL_ILLOPC;
                queue_signal(env, info.si_signo, &info);

                /* TODO: What about instruction emulation? */
            }
            break;
        case EXCP_SWI:
        case EXCP_BKPT:
            {
                /*
                 * system call
                 * See arm/arm/trap.c cpu_fetch_syscall_args()
                 */
                if (trapnr == EXCP_BKPT) {
                    if (env->thumb) {
                        env->regs[15] += 2;
                    } else {
                        env->regs[15] += 4;
                    }
                }
                n = env->regs[7];
                if (bsd_type == target_freebsd) {
                    int ret;
                    abi_ulong params = get_sp_from_cpustate(env);
                    int32_t syscall_nr = n;
                    int32_t arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8;

                    /* See arm/arm/trap.c cpu_fetch_syscall_args() */
                    if (syscall_nr == TARGET_FREEBSD_NR_syscall) {
                        syscall_nr = env->regs[0];
                        arg1 = env->regs[1];
                        arg2 = env->regs[2];
                        arg3 = env->regs[3];
                        get_user_s32(arg4, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg5, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg6, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg7, params);
                        arg8 = 0;
                    } else if (syscall_nr == TARGET_FREEBSD_NR___syscall) {
                        syscall_nr = env->regs[0];
                        arg1 = env->regs[2];
                        arg2 = env->regs[3];
                        get_user_s32(arg3, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg4, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg5, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg6, params);
                        arg7 = 0;
                        arg8 = 0;
                    } else {
                        arg1 = env->regs[0];
                        arg2 = env->regs[1];
                        arg3 = env->regs[2];
                        arg4 = env->regs[3];
                        get_user_s32(arg5, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg6, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg7, params);
                        params += sizeof(int32_t);
                        get_user_s32(arg8, params);
                    }
                    ret = do_freebsd_syscall(env, syscall_nr, arg1, arg2, arg3,
                            arg4, arg5, arg6, arg7, arg8);
                    /*
                     * Compare to arm/arm/vm_machdep.c
                     * cpu_set_syscall_retval()
                     */
                    if (-TARGET_EJUSTRETURN == ret) {
                        /*
                         * Returning from a successful sigreturn syscall.
                         * Avoid clobbering register state.
                         */
                        break;
                    }
                    if (-TARGET_ERESTART == ret) {
                        env->regs[15] -= env->thumb ? 2 : 4;
                        break;
                    }
                    if ((unsigned int)ret >= (unsigned int)(-515)) {
                        ret = -ret;
                        cpsr_write(env, CPSR_C, CPSR_C, CPSRWriteByInstr);
                        env->regs[0] = ret;
                    } else {
                        cpsr_write(env, 0, CPSR_C, CPSRWriteByInstr);
                        env->regs[0] = ret; /* XXX need to handle lseek()? */
                        /* env->regs[1] = 0; */
                    }
                } else {
                    fprintf(stderr, "qemu: bsd_type (= %d) syscall "
                            "not supported\n", bsd_type);
                }
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_PREFETCH_ABORT:
            /* See arm/arm/trap.c prefetch_abort_handler() */
        case EXCP_DATA_ABORT:
            /* See arm/arm/trap.c data_abort_handler() */
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            /* XXX: check env->error_code */
            info.si_code = 0;
            info.si_addr = env->exception.vaddress;
            queue_signal(env, info.si_signo, &info);
            break;
        case EXCP_DEBUG:
            {

                info.si_signo = TARGET_SIGTRAP;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                info.si_addr = env->exception.vaddress;
                queue_signal(env, info.si_signo, &info);
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case EXCP_YIELD:
            /* nothing to do here for user-mode, just resume guest code */
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            cpu_dump_state(cs, stderr, 0);
            abort();
        } /* switch() */
        process_pending_signals(env);
    } /* for (;;) */
}

static inline void target_cpu_clone_regs(CPUARMState *env, target_ulong newsp)
{
    if (newsp) {
        env->regs[13] = newsp;
    }
    env->regs[0] = 0;
}

static inline void target_cpu_reset(CPUArchState *cpu)
{
}

#endif /* !_TARGET_ARCH_CPU_H */
