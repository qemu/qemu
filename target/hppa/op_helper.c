/*
 * Helpers for HPPA instructions.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "qemu/timer.h"
#include "trace.h"

G_NORETURN void HELPER(excp)(CPUHPPAState *env, int excp)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    cpu_loop_exit(cs);
}

G_NORETURN void hppa_dynamic_excp(CPUHPPAState *env, int excp, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = excp;
    cpu_loop_exit_restore(cs, ra);
}

void HELPER(tsv)(CPUHPPAState *env, target_ureg cond)
{
    if (unlikely((target_sreg)cond < 0)) {
        hppa_dynamic_excp(env, EXCP_OVERFLOW, GETPC());
    }
}

void HELPER(tcond)(CPUHPPAState *env, target_ureg cond)
{
    if (unlikely(cond)) {
        hppa_dynamic_excp(env, EXCP_COND, GETPC());
    }
}

static void atomic_store_3(CPUHPPAState *env, target_ulong addr,
                           uint32_t val, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env, 0);
    uint32_t old, new, cmp, mask, *haddr;
    void *vaddr;

    vaddr = probe_access(env, addr, 3, MMU_DATA_STORE, mmu_idx, ra);
    if (vaddr == NULL) {
        cpu_loop_exit_atomic(env_cpu(env), ra);
    }
    haddr = (uint32_t *)((uintptr_t)vaddr & -4);
    mask = addr & 1 ? 0x00ffffffu : 0xffffff00u;

    old = *haddr;
    while (1) {
        new = be32_to_cpu((cpu_to_be32(old) & ~mask) | (val & mask));
        cmp = qatomic_cmpxchg(haddr, old, new);
        if (cmp == old) {
            return;
        }
        old = cmp;
    }
}

static void do_stby_b(CPUHPPAState *env, target_ulong addr, target_ureg val,
                      bool parallel, uintptr_t ra)
{
    switch (addr & 3) {
    case 3:
        cpu_stb_data_ra(env, addr, val, ra);
        break;
    case 2:
        cpu_stw_data_ra(env, addr, val, ra);
        break;
    case 1:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_3(env, addr, val, ra);
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

void HELPER(stby_b)(CPUHPPAState *env, target_ulong addr, target_ureg val)
{
    do_stby_b(env, addr, val, false, GETPC());
}

void HELPER(stby_b_parallel)(CPUHPPAState *env, target_ulong addr,
                             target_ureg val)
{
    do_stby_b(env, addr, val, true, GETPC());
}

static void do_stby_e(CPUHPPAState *env, target_ulong addr, target_ureg val,
                      bool parallel, uintptr_t ra)
{
    switch (addr & 3) {
    case 3:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_3(env, addr - 3, val, ra);
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
        probe_write(env, addr, 0, cpu_mmu_index(env, 0), ra);
        break;
    }
}

void HELPER(stby_e)(CPUHPPAState *env, target_ulong addr, target_ureg val)
{
    do_stby_e(env, addr, val, false, GETPC());
}

void HELPER(stby_e_parallel)(CPUHPPAState *env, target_ulong addr,
                             target_ureg val)
{
    do_stby_e(env, addr, val, true, GETPC());
}

void HELPER(ldc_check)(target_ulong addr)
{
    if (unlikely(addr & 0xf)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Undefined ldc to unaligned address mod 16: "
                      TARGET_FMT_lx "\n", addr);
    }
}

target_ureg HELPER(probe)(CPUHPPAState *env, target_ulong addr,
                          uint32_t level, uint32_t want)
{
#ifdef CONFIG_USER_ONLY
    return (page_check_range(addr, 1, want) == 0) ? 1 : 0;
#else
    int prot, excp;
    hwaddr phys;

    trace_hppa_tlb_probe(addr, level, want);
    /* Fail if the requested privilege level is higher than current.  */
    if (level < (env->iaoq_f & 3)) {
        return 0;
    }

    excp = hppa_get_physical_address(env, addr, level, 0, &phys, &prot);
    if (excp >= 0) {
        if (env->psw & PSW_Q) {
            /* ??? Needs tweaking for hppa64.  */
            env->cr[CR_IOR] = addr;
            env->cr[CR_ISR] = addr >> 32;
        }
        if (excp == EXCP_DTLB_MISS) {
            excp = EXCP_NA_DTLB_MISS;
        }
        hppa_dynamic_excp(env, excp, GETPC());
    }
    return (want & prot) != 0;
#endif
}

target_ureg HELPER(read_interval_timer)(void)
{
#ifdef CONFIG_USER_ONLY
    /* In user-mode, QEMU_CLOCK_VIRTUAL doesn't exist.
       Just pass through the host cpu clock ticks.  */
    return cpu_get_host_ticks();
#else
    /* In system mode we have access to a decent high-resolution clock.
       In order to make OS-level time accounting work with the cr16,
       present it with a well-timed clock fixed at 250MHz.  */
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 2;
#endif
}
