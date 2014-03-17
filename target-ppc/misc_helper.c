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
#include "cpu.h"
#include "helper.h"

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
#if !defined(CONFIG_USER_ONLY)

void helper_store_sdr1(CPUPPCState *env, target_ulong val)
{
    if (!env->external_htab) {
        ppc_store_sdr1(env, val);
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
