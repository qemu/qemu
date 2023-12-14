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

void helper_raise_exception(CPUNios2State *env, uint32_t index)
{
    CPUState *cs = env_cpu(env);
    cs->exception_index = index;
    cpu_loop_exit(cs);
}

void nios2_cpu_loop_exit_advance(CPUNios2State *env, uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    /*
     * Note that PC is advanced for all hardware exceptions.
     * Do this here, rather than in restore_state_to_opc(),
     * lest we affect QEMU internal exceptions, like EXCP_DEBUG.
     */
    cpu_restore_state(cs, retaddr);
    env->pc += 4;
    cpu_loop_exit(cs);
}

static void maybe_raise_div(CPUNios2State *env, uintptr_t ra)
{
    Nios2CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (cpu->diverr_present) {
        cs->exception_index = EXCP_DIV;
        nios2_cpu_loop_exit_advance(env, ra);
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
        nios2_cpu_loop_exit_advance(env, GETPC());
    }

    /*
     * None of estatus, bstatus, or sstatus have constraints on write;
     * do not allow reserved fields in status to be set.
     * When shadow registers are enabled, eret *does* restore CRS.
     * Rather than testing eic_present to decide, mask CRS out of
     * the set of readonly fields.
     */
    new_status &= cpu->cr_state[CR_STATUS].writable |
                  (cpu->cr_state[CR_STATUS].readonly & R_CR_STATUS_CRS_MASK);

    env->ctrl[CR_STATUS] = new_status;
    env->pc = new_pc;
    nios2_update_crs(env);
    cpu_loop_exit(cs);
}

/*
 * RDPRS and WRPRS are implemented out of line so that if PRS == CRS,
 * all of the tcg global temporaries are synced back to ENV.
 */
uint32_t helper_rdprs(CPUNios2State *env, uint32_t regno)
{
    unsigned prs = FIELD_EX32(env->ctrl[CR_STATUS], CR_STATUS, PRS);
    return env->shadow_regs[prs][regno];
}

void helper_wrprs(CPUNios2State *env, uint32_t regno, uint32_t val)
{
    unsigned prs = FIELD_EX32(env->ctrl[CR_STATUS], CR_STATUS, PRS);
    env->shadow_regs[prs][regno] = val;
}
#endif /* !CONFIG_USER_ONLY */
