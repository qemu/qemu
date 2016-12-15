/*
 * Helpers for HPPA instructions.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"

void QEMU_NORETURN HELPER(excp)(CPUHPPAState *env, int excp)
{
    HPPACPU *cpu = hppa_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

static void QEMU_NORETURN dynexcp(CPUHPPAState *env, int excp, uintptr_t ra)
{
    HPPACPU *cpu = hppa_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = excp;
    cpu_loop_exit_restore(cs, ra);
}

void HELPER(tsv)(CPUHPPAState *env, target_ulong cond)
{
    if (unlikely((target_long)cond < 0)) {
        dynexcp(env, EXCP_SIGFPE, GETPC());
    }
}

void HELPER(tcond)(CPUHPPAState *env, target_ulong cond)
{
    if (unlikely(cond)) {
        dynexcp(env, EXCP_SIGFPE, GETPC());
    }
}

static void atomic_store_3(CPUHPPAState *env, target_ulong addr, uint32_t val,
                           uint32_t mask, uintptr_t ra)
{
    uint32_t old, new, cmp;

#ifdef CONFIG_USER_ONLY
    uint32_t *haddr = g2h(addr - 1);
    old = *haddr;
    while (1) {
        new = (old & ~mask) | (val & mask);
        cmp = atomic_cmpxchg(haddr, old, new);
        if (cmp == old) {
            return;
        }
        old = cmp;
    }
#else
#error "Not implemented."
#endif
}

void HELPER(stby_b)(CPUHPPAState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();

    switch (addr & 3) {
    case 3:
        cpu_stb_data_ra(env, addr, val, ra);
        break;
    case 2:
        cpu_stw_data_ra(env, addr, val, ra);
        break;
    case 1:
        /* The 3 byte store must appear atomic.  */
        if (parallel_cpus) {
            atomic_store_3(env, addr, val, 0x00ffffffu, ra);
        } else {
            cpu_stb_data_ra(env, addr, val >> 16, ra);
            cpu_stw_data_ra(env, addr + 1, val, ra);
        }
        break;
    default:
        cpu_stl_data_ra(env, addr, val, ra);
        break;
    }
}

void HELPER(stby_e)(CPUHPPAState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();

    switch (addr & 3) {
    case 3:
        /* The 3 byte store must appear atomic.  */
        if (parallel_cpus) {
            atomic_store_3(env, addr - 3, val, 0xffffff00u, ra);
        } else {
            cpu_stw_data_ra(env, addr - 3, val >> 16, ra);
            cpu_stb_data_ra(env, addr - 1, val >> 8, ra);
        }
        break;
    case 2:
        cpu_stw_data_ra(env, addr - 2, val >> 16, ra);
        break;
    case 1:
        cpu_stb_data_ra(env, addr - 1, val >> 24, ra);
        break;
    default:
        /* Nothing is stored, but protection is checked and the
           cacheline is marked dirty.  */
#ifndef CONFIG_USER_ONLY
        probe_write(env, addr, cpu_mmu_index(env, 0), ra);
#endif
        break;
    }
}

void HELPER(loaded_fr0)(CPUHPPAState *env)
{
    uint32_t shadow = env->fr[0] >> 32;
    int rm, d;

    env->fr0_shadow = shadow;

    switch (extract32(shadow, 9, 2)) {
    default:
        rm = float_round_nearest_even;
        break;
    case 1:
        rm = float_round_to_zero;
        break;
    case 2:
        rm = float_round_up;
        break;
    case 3:
        rm = float_round_down;
        break;
    }
    set_float_rounding_mode(rm, &env->fp_status);

    d = extract32(shadow, 5, 1);
    set_flush_to_zero(d, &env->fp_status);
    set_flush_inputs_to_zero(d, &env->fp_status);
}

void cpu_hppa_loaded_fr0(CPUHPPAState *env)
{
    helper_loaded_fr0(env);
}
