/*
 * Altera Nios II helper routines.
 *
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "qemu/main-loop.h"

void helper_raise_exception(CPUNios2State *env, uint32_t index)
{
    CPUState *cs = env_cpu(env);
    cs->exception_index = index;
    cpu_loop_exit(cs);
}

static void maybe_raise_div(CPUNios2State *env, uintptr_t ra)
{
    Nios2CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (cpu->diverr_present) {
        cs->exception_index = EXCP_DIV;
        cpu_loop_exit_restore(cs, ra);
    }
}

int32_t helper_divs(CPUNios2State *env, int32_t num, int32_t den)
{
    if (unlikely(den == 0) || unlikely(den == -1 && num == INT32_MIN)) {
        maybe_raise_div(env, GETPC());
        return num; /* undefined */
    }
    return num / den;
}

uint32_t helper_divu(CPUNios2State *env, uint32_t num, uint32_t den)
{
    if (unlikely(den == 0)) {
        maybe_raise_div(env, GETPC());
        return num; /* undefined */
    }
    return num / den;
}

#ifndef CONFIG_USER_ONLY
void helper_eret(CPUNios2State *env, uint32_t new_status, uint32_t new_pc)
{
    Nios2CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (unlikely(new_pc & 3)) {
        env->ctrl[CR_BADADDR] = new_pc;
        cs->exception_index = EXCP_UNALIGND;
        cpu_loop_exit_restore(cs, GETPC());
    }

    /*
     * Both estatus and bstatus have no constraints on write;
     * do not allow reserved fields in status to be set.
     * TODO: more than this is required for shadow registers.
     */
    new_status &= cpu->cr_state[CR_STATUS].writable;

    env->ctrl[CR_STATUS] = new_status;
    env->pc = new_pc;
    cpu_loop_exit(cs);
}
#endif /* !CONFIG_USER_ONLY */
