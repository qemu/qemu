/*
 *  x86 exception helpers - user-mode specific code
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
#include "tcg/helper-tcg.h"

void x86_cpu_record_sigsegv(CPUState *cs, vaddr addr,
                            MMUAccessType access_type,
                            bool maperr, uintptr_t ra)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    /*
     * The error_code that hw reports as part of the exception frame
     * is copied to linux sigcontext.err.  The exception_index is
     * copied to linux sigcontext.trapno.  Short of inventing a new
     * place to store the trapno, we cannot let our caller raise the
     * signal and set exception_index to EXCP_INTERRUPT.
     */
    env->cr[2] = addr;
    env->error_code = ((access_type == MMU_DATA_STORE) << PG_ERROR_W_BIT)
                    | (maperr ? 0 : PG_ERROR_P_MASK)
                    | PG_ERROR_U_MASK;
    cs->exception_index = EXCP0E_PAGE;

    /* Disable do_interrupt_user. */
    env->exception_is_int = 0;
    env->exception_next_eip = -1;

    cpu_loop_exit_restore(cs, ra);
}

void x86_cpu_record_sigbus(CPUState *cs, vaddr addr,
                           MMUAccessType access_type, uintptr_t ra)
{
    X86CPU *cpu = X86_CPU(cs);
    handle_unaligned_access(&cpu->env, addr, access_type, ra);
}
