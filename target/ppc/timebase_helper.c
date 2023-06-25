/*
 *  PowerPC emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"

/*****************************************************************************/
/* SPR accesses */

target_ulong helper_load_tbl(CPUPPCState *env)
{
    return (target_ulong)cpu_ppc_load_tbl(env);
}

target_ulong helper_load_tbu(CPUPPCState *env)
{
    return cpu_ppc_load_tbu(env);
}

target_ulong helper_load_atbl(CPUPPCState *env)
{
    return (target_ulong)cpu_ppc_load_atbl(env);
}

target_ulong helper_load_atbu(CPUPPCState *env)
{
    return cpu_ppc_load_atbu(env);
}

target_ulong helper_load_vtb(CPUPPCState *env)
{
    return cpu_ppc_load_vtb(env);
}

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
target_ulong helper_load_purr(CPUPPCState *env)
{
    return (target_ulong)cpu_ppc_load_purr(env);
}

void helper_store_purr(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_purr(env, val);
}
#endif

#if !defined(CONFIG_USER_ONLY)
void helper_store_tbl(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_tbl(env, val);
}

void helper_store_tbu(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_tbu(env, val);
}

void helper_store_atbl(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_atbl(env, val);
}

void helper_store_atbu(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_atbu(env, val);
}

target_ulong helper_load_decr(CPUPPCState *env)
{
    return cpu_ppc_load_decr(env);
}

void helper_store_decr(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_decr(env, val);
}

target_ulong helper_load_hdecr(CPUPPCState *env)
{
    return cpu_ppc_load_hdecr(env);
}

void helper_store_hdecr(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_hdecr(env, val);
}

void helper_store_vtb(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_vtb(env, val);
}

void helper_store_tbu40(CPUPPCState *env, target_ulong val)
{
    cpu_ppc_store_tbu40(env, val);
}

target_ulong helper_load_40x_pit(CPUPPCState *env)
{
    return load_40x_pit(env);
}

void helper_store_40x_pit(CPUPPCState *env, target_ulong val)
{
    store_40x_pit(env, val);
}

void helper_store_40x_tcr(CPUPPCState *env, target_ulong val)
{
    store_40x_tcr(env, val);
}

void helper_store_40x_tsr(CPUPPCState *env, target_ulong val)
{
    store_40x_tsr(env, val);
}

void helper_store_booke_tcr(CPUPPCState *env, target_ulong val)
{
    store_booke_tcr(env, val);
}

void helper_store_booke_tsr(CPUPPCState *env, target_ulong val)
{
    store_booke_tsr(env, val);
}

#if defined(TARGET_PPC64)
/* POWER processor Timebase Facility */
target_ulong helper_load_tfmr(CPUPPCState *env)
{
    return env->spr[SPR_TFMR];
}

void helper_store_tfmr(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_TFMR] = val;
}
#endif

/*****************************************************************************/
/* Embedded PowerPC specific helpers */

/* XXX: to be improved to check access rights when in user-mode */
target_ulong helper_load_dcr(CPUPPCState *env, target_ulong dcrn)
{
    uint32_t val = 0;

    if (unlikely(env->dcr_env == NULL)) {
        qemu_log_mask(LOG_GUEST_ERROR, "No DCR environment\n");
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL |
                               POWERPC_EXCP_INVAL_INVAL, GETPC());
    } else {
        int ret;

        qemu_mutex_lock_iothread();
        ret = ppc_dcr_read(env->dcr_env, (uint32_t)dcrn, &val);
        qemu_mutex_unlock_iothread();
        if (unlikely(ret != 0)) {
            qemu_log_mask(LOG_GUEST_ERROR, "DCR read error %d %03x\n",
                          (uint32_t)dcrn, (uint32_t)dcrn);
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL, GETPC());
        }
    }
    return val;
}

void helper_store_dcr(CPUPPCState *env, target_ulong dcrn, target_ulong val)
{
    if (unlikely(env->dcr_env == NULL)) {
        qemu_log_mask(LOG_GUEST_ERROR, "No DCR environment\n");
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_INVAL |
                               POWERPC_EXCP_INVAL_INVAL, GETPC());
    } else {
        int ret;
        qemu_mutex_lock_iothread();
        ret = ppc_dcr_write(env->dcr_env, (uint32_t)dcrn, (uint32_t)val);
        qemu_mutex_unlock_iothread();
        if (unlikely(ret != 0)) {
            qemu_log_mask(LOG_GUEST_ERROR, "DCR write error %d %03x\n",
                          (uint32_t)dcrn, (uint32_t)dcrn);
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_INVAL, GETPC());
        }
    }
}
#endif
