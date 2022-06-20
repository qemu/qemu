/*
 * ARM SME Operations
 *
 * Copyright (c) 2022 Linaro, Ltd.
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
#include "internals.h"
#include "exec/helper-proto.h"

/* ResetSVEState */
void arm_reset_sve_state(CPUARMState *env)
{
    memset(env->vfp.zregs, 0, sizeof(env->vfp.zregs));
    /* Recall that FFR is stored as pregs[16]. */
    memset(env->vfp.pregs, 0, sizeof(env->vfp.pregs));
    vfp_set_fpcr(env, 0x0800009f);
}

void helper_set_pstate_sm(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, SM)) {
        return;
    }
    env->svcr ^= R_SVCR_SM_MASK;
    arm_reset_sve_state(env);
}

void helper_set_pstate_za(CPUARMState *env, uint32_t i)
{
    if (i == FIELD_EX64(env->svcr, SVCR, ZA)) {
        return;
    }
    env->svcr ^= R_SVCR_ZA_MASK;

    /*
     * ResetSMEState.
     *
     * SetPSTATE_ZA zeros on enable and disable.  We can zero this only
     * on enable: while disabled, the storage is inaccessible and the
     * value does not matter.  We're not saving the storage in vmstate
     * when disabled either.
     */
    if (i) {
        memset(env->zarray, 0, sizeof(env->zarray));
    }
}
