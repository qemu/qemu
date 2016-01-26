/*
 * Miscellaneous PowerPC emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include "helper_regs.h"

/*****************************************************************************/
/* SPR accesses */
void helper_load_dump_spr(CPUPPCState *env, uint32_t sprn)
{
    qemu_log("Read SPR %d %03x => " TARGET_FMT_lx "\n", sprn, sprn,
             env->spr[sprn]);
}

void helper_store_dump_spr(CPUPPCState *env, uint32_t sprn)
{
    qemu_log("Write SPR %d %03x <= " TARGET_FMT_lx "\n", sprn, sprn,
             env->spr[sprn]);
}

#ifdef TARGET_PPC64
static void raise_fu_exception(CPUPPCState *env, uint32_t bit,
                               uint32_t sprn, uint32_t cause)
{
    qemu_log("Facility SPR %d is unavailable (SPR FSCR:%d)\n", sprn, bit);

    env->spr[SPR_FSCR] &= ~((target_ulong)FSCR_IC_MASK << FSCR_IC_POS);
    cause &= FSCR_IC_MASK;
    env->spr[SPR_FSCR] |= (target_ulong)cause << FSCR_IC_POS;

    helper_raise_exception_err(env, POWERPC_EXCP_FU, 0);
}
#endif

void helper_fscr_facility_check(CPUPPCState *env, uint32_t bit,
                                uint32_t sprn, uint32_t cause)
{
#ifdef TARGET_PPC64
    if (env->spr[SPR_FSCR] & (1ULL << bit)) {
        /* Facility is enabled, continue */
        return;
    }
    raise_fu_exception(env, bit, sprn, cause);
#endif
}

void helper_msr_facility_check(CPUPPCState *env, uint32_t bit,
                               uint32_t sprn, uint32_t cause)
{
#ifdef TARGET_PPC64
    if (env->msr & (1ULL << bit)) {
        /* Facility is enabled, continue */
        return;
    }
    raise_fu_exception(env, bit, sprn, cause);
#endif
}

#if !defined(CONFIG_USER_ONLY)

void helper_store_sdr1(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    if (!env->external_htab) {
        if (env->spr[SPR_SDR1] != val) {
            ppc_store_sdr1(env, val);
            tlb_flush(CPU(cpu), 1);
        }
    }
}

void helper_store_hid0_601(CPUPPCState *env, target_ulong val)
{
    target_ulong hid0;

    hid0 = env->spr[SPR_HID0];
    if ((val ^ hid0) & 0x00000008) {
        /* Change current endianness */
        env->hflags &= ~(1 << MSR_LE);
        env->hflags_nmsr &= ~(1 << MSR_LE);
        env->hflags_nmsr |= (1 << MSR_LE) & (((val >> 3) & 1) << MSR_LE);
        env->hflags |= env->hflags_nmsr;
        qemu_log("%s: set endianness to %c => " TARGET_FMT_lx "\n", __func__,
                 val & 0x8 ? 'l' : 'b', env->hflags);
    }
    env->spr[SPR_HID0] = (uint32_t)val;
}

void helper_store_403_pbr(CPUPPCState *env, uint32_t num, target_ulong value)
{
    PowerPCCPU *cpu = ppc_env_get_cpu(env);

    if (likely(env->pb[num] != value)) {
        env->pb[num] = value;
        /* Should be optimized */
        tlb_flush(CPU(cpu), 1);
    }
}

void helper_store_40x_dbcr0(CPUPPCState *env, target_ulong val)
{
    store_40x_dbcr0(env, val);
}

void helper_store_40x_sler(CPUPPCState *env, target_ulong val)
{
    store_40x_sler(env, val);
}
#endif
/*****************************************************************************/
/* PowerPC 601 specific instructions (POWER bridge) */

target_ulong helper_clcs(CPUPPCState *env, uint32_t arg)
{
    switch (arg) {
    case 0x0CUL:
        /* Instruction cache line size */
        return env->icache_line_size;
        break;
    case 0x0DUL:
        /* Data cache line size */
        return env->dcache_line_size;
        break;
    case 0x0EUL:
        /* Minimum cache line size */
        return (env->icache_line_size < env->dcache_line_size) ?
            env->icache_line_size : env->dcache_line_size;
        break;
    case 0x0FUL:
        /* Maximum cache line size */
        return (env->icache_line_size > env->dcache_line_size) ?
            env->icache_line_size : env->dcache_line_size;
        break;
    default:
        /* Undefined */
        return 0;
        break;
    }
}

/*****************************************************************************/
/* Special registers manipulation */

/* GDBstub can read and write MSR... */
void ppc_store_msr(CPUPPCState *env, target_ulong value)
{
    hreg_store_msr(env, value, 0);
}
