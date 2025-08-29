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

#ifndef TARGET_ARCH_CPU_H
#define TARGET_ARCH_CPU_H

#include "target_arch.h"
#include "signal-common.h"

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

static inline G_NORETURN void target_cpu_loop(CPUARMState *env)
{
    int trapnr, si_signo, si_code;
    CPUState *cs = env_cpu(env);

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        qemu_process_cpu_events(cs);
        switch (trapnr) {
        case EXCP_UDEF:
        case EXCP_NOCP:
        case EXCP_INVSTATE:
            /*
             * See arm/arm/undefined.c undefinedinstruction();
             *
             * A number of details aren't emulated (they likely don't matter):
             * o Misaligned PC generates ILL_ILLADR (these can't come from qemu)
             * o Thumb-2 instructions generate ILLADR
             * o Both modes implement coprocessor instructions, which we don't
             *   do here. FreeBSD just implements them for the VFP coprocessor
             *   and special kernel breakpoints, trace points, dtrace, etc.
             */
            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPC, env->regs[15]);
            break;
        case EXCP_SWI:
            {
                int ret;
                abi_ulong params = get_sp_from_cpustate(env);
                int32_t syscall_nr = env->regs[7];
                int32_t arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8;

                /* See arm/arm/syscall.c cpu_fetch_syscall_args() */
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
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:
            /*
             * See arm/arm/trap-v6.c prefetch_abort_handler() and
             * data_abort_handler()
             *
             * However, FreeBSD maps these to a generic value and then uses that
             * to maybe fault in pages in vm/vm_fault.c:vm_fault_trap(). I
             * believe that the indirection maps the same as Linux, but haven't
             * chased down every single possible indirection.
             */

            /* For user-only we don't set TTBCR_EAE, so look at the FSR. */
            switch (env->exception.fsr & 0x1f) {
            case 0x1: /* Alignment */
                si_signo = TARGET_SIGBUS;
                si_code = TARGET_BUS_ADRALN;
                break;
            case 0x3: /* Access flag fault, level 1 */
            case 0x6: /* Access flag fault, level 2 */
            case 0x9: /* Domain fault, level 1 */
            case 0xb: /* Domain fault, level 2 */
            case 0xd: /* Permission fault, level 1 */
            case 0xf: /* Permission fault, level 2 */
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_ACCERR;
                break;
            case 0x5: /* Translation fault, level 1 */
            case 0x7: /* Translation fault, level 2 */
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_MAPERR;
                break;
            default:
                g_assert_not_reached();
            }
            force_sig_fault(si_signo, si_code, env->exception.vaddress);
            break;
        case EXCP_DEBUG:
        case EXCP_BKPT:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->regs[15]);
            break;
        case EXCP_YIELD:
            /* nothing to do here for user-mode, just resume guest code */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
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

static inline void target_cpu_reset(CPUArchState *env)
{
}

#endif /* TARGET_ARCH_CPU_H */
