/*
 *  x86 exception helpers
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/log.h"
#include "system/runstate.h"
#include "exec/helper-proto.h"
#include "helper-tcg.h"

G_NORETURN void helper_raise_interrupt(CPUX86State *env, int intno,
                                          int next_eip_addend)
{
    raise_interrupt(env, intno, next_eip_addend);
}

G_NORETURN void helper_raise_exception(CPUX86State *env, int exception_index)
{
    raise_exception(env, exception_index);
}

/*
 * Check nested exceptions and change to double or triple fault if
 * needed. It should only be called, if this is not an interrupt.
 * Returns the new exception number.
 */
static int check_exception(CPUX86State *env, int intno, int *error_code,
                           uintptr_t retaddr)
{
    int first_contributory = env->old_exception == 0 ||
                              (env->old_exception >= 10 &&
                               env->old_exception <= 13);
    int second_contributory = intno == 0 ||
                               (intno >= 10 && intno <= 13);

    qemu_log_mask(CPU_LOG_INT, "check_exception old: 0x%x new 0x%x\n",
                env->old_exception, intno);

#if !defined(CONFIG_USER_ONLY)
    if (env->old_exception == EXCP08_DBLE) {
        if (env->hflags & HF_GUEST_MASK) {
            cpu_vmexit(env, SVM_EXIT_SHUTDOWN, 0, retaddr); /* does not return */
        }

        qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");

        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return EXCP_HLT;
    }
#endif

    if ((first_contributory && second_contributory)
        || (env->old_exception == EXCP0E_PAGE &&
            (second_contributory || (intno == EXCP0E_PAGE)))) {
        intno = EXCP08_DBLE;
        *error_code = 0;
    }

    if (second_contributory || (intno == EXCP0E_PAGE) ||
        (intno == EXCP08_DBLE)) {
        env->old_exception = intno;
    }

    return intno;
}

/*
 * Signal an interruption. It is executed in the main CPU loop.
 * is_int is TRUE if coming from the int instruction. next_eip is the
 * env->eip value AFTER the interrupt instruction. It is only relevant if
 * is_int is TRUE.
 */
static G_NORETURN
void raise_interrupt2(CPUX86State *env, int intno,
                      int is_int, int error_code,
                      int next_eip_addend,
                      uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    if (!is_int) {
        cpu_svm_check_intercept_param(env, SVM_EXIT_EXCP_BASE + intno,
                                      error_code, retaddr);
        intno = check_exception(env, intno, &error_code, retaddr);
    } else {
        cpu_svm_check_intercept_param(env, SVM_EXIT_SWINT, 0, retaddr);
    }

    cs->exception_index = intno;
    env->error_code = error_code;
    env->exception_is_int = is_int;
    env->exception_next_eip = env->eip + next_eip_addend;
    cpu_loop_exit_restore(cs, retaddr);
}

/* shortcuts to generate exceptions */

G_NORETURN void raise_interrupt(CPUX86State *env, int intno, int next_eip_addend)
{
    raise_interrupt2(env, intno, 1, 0, next_eip_addend, 0);
}

G_NORETURN void raise_exception_err(CPUX86State *env, int exception_index,
                                    int error_code)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, 0);
}

G_NORETURN void raise_exception_err_ra(CPUX86State *env, int exception_index,
                                       int error_code, uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, error_code, 0, retaddr);
}

G_NORETURN void raise_exception(CPUX86State *env, int exception_index)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, 0);
}

G_NORETURN void raise_exception_ra(CPUX86State *env, int exception_index,
                                   uintptr_t retaddr)
{
    raise_interrupt2(env, exception_index, 0, 0, 0, retaddr);
}

G_NORETURN void helper_icebp(CPUX86State *env)
{
    CPUState *cs = env_cpu(env);

    do_end_instruction(env);

    /*
     * INT1 aka ICEBP generates a trap-like #DB, but it is pretty special.
     *
     * "Although the ICEBP instruction dispatches through IDT vector 1,
     * that event is not interceptable by means of the #DB exception
     * intercept".  Instead there is a separate fault-like ICEBP intercept.
     */
    cs->exception_index = EXCP01_DB;
    env->error_code = 0;
    env->exception_is_int = 0;
    env->exception_next_eip = env->eip;
    cpu_loop_exit(cs);
}

G_NORETURN void handle_unaligned_access(CPUX86State *env, vaddr vaddr,
                                        MMUAccessType access_type,
                                        uintptr_t retaddr)
{
    /*
     * Unaligned accesses are currently only triggered by SSE/AVX
     * instructions that impose alignment requirements on memory
     * operands. These instructions raise #GP(0) upon accessing an
     * unaligned address.
     */
    raise_exception_ra(env, EXCP0D_GPF, retaddr);
}
