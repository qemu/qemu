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
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "qemu/timer.h"
#include "trace.h"
#ifdef CONFIG_USER_ONLY
#include "user/page-protection.h"
#endif

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

static void atomic_store_mask32(CPUHPPAState *env, target_ulong addr,
                                uint32_t val, uint32_t mask, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env_cpu(env), 0);
    uint32_t old, new, cmp, *haddr;
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

static void atomic_store_mask64(CPUHPPAState *env, target_ulong addr,
                                uint64_t val, uint64_t mask,
                                int size, uintptr_t ra)
{
#ifdef CONFIG_ATOMIC64
    int mmu_idx = cpu_mmu_index(env_cpu(env), 0);
    uint64_t old, new, cmp, *haddr;
    void *vaddr;

    vaddr = probe_access(env, addr, size, MMU_DATA_STORE, mmu_idx, ra);
    if (vaddr == NULL) {
        cpu_loop_exit_atomic(env_cpu(env), ra);
    }
    haddr = (uint64_t *)((uintptr_t)vaddr & -8);

    old = *haddr;
    while (1) {
        new = be32_to_cpu((cpu_to_be32(old) & ~mask) | (val & mask));
        cmp = qatomic_cmpxchg__nocheck(haddr, old, new);
        if (cmp == old) {
            return;
        }
        old = cmp;
    }
#else
    cpu_loop_exit_atomic(env_cpu(env), ra);
#endif
}

static void do_stby_b(CPUHPPAState *env, target_ulong addr, target_ulong val,
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
            atomic_store_mask32(env, addr, val, 0x00ffffffu, ra);
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

static void do_stdby_b(CPUHPPAState *env, target_ulong addr, uint64_t val,
                       bool parallel, uintptr_t ra)
{
    switch (addr & 7) {
    case 7:
        cpu_stb_data_ra(env, addr, val, ra);
        break;
    case 6:
        cpu_stw_data_ra(env, addr, val, ra);
        break;
    case 5:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask32(env, addr, val, 0x00ffffffu, ra);
        } else {
            cpu_stb_data_ra(env, addr, val >> 16, ra);
            cpu_stw_data_ra(env, addr + 1, val, ra);
        }
        break;
    case 4:
        cpu_stl_data_ra(env, addr, val, ra);
        break;
    case 3:
        /* The 5 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask64(env, addr, val, 0x000000ffffffffffull, 5, ra);
        } else {
            cpu_stb_data_ra(env, addr, val >> 32, ra);
            cpu_stl_data_ra(env, addr + 1, val, ra);
        }
        break;
    case 2:
        /* The 6 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask64(env, addr, val, 0x0000ffffffffffffull, 6, ra);
        } else {
            cpu_stw_data_ra(env, addr, val >> 32, ra);
            cpu_stl_data_ra(env, addr + 2, val, ra);
        }
        break;
    case 1:
        /* The 7 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask64(env, addr, val, 0x00ffffffffffffffull, 7, ra);
        } else {
            cpu_stb_data_ra(env, addr, val >> 48, ra);
            cpu_stw_data_ra(env, addr + 1, val >> 32, ra);
            cpu_stl_data_ra(env, addr + 3, val, ra);
        }
        break;
    default:
        cpu_stq_data_ra(env, addr, val, ra);
        break;
    }
}

void HELPER(stby_b)(CPUHPPAState *env, target_ulong addr, target_ulong val)
{
    do_stby_b(env, addr, val, false, GETPC());
}

void HELPER(stby_b_parallel)(CPUHPPAState *env, target_ulong addr,
                             target_ulong val)
{
    do_stby_b(env, addr, val, true, GETPC());
}

void HELPER(stdby_b)(CPUHPPAState *env, target_ulong addr, target_ulong val)
{
    do_stdby_b(env, addr, val, false, GETPC());
}

void HELPER(stdby_b_parallel)(CPUHPPAState *env, target_ulong addr,
                              target_ulong val)
{
    do_stdby_b(env, addr, val, true, GETPC());
}

static void do_stby_e(CPUHPPAState *env, target_ulong addr, target_ulong val,
                      bool parallel, uintptr_t ra)
{
    switch (addr & 3) {
    case 3:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask32(env, addr - 3, val, 0xffffff00u, ra);
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
        probe_write(env, addr, 0, cpu_mmu_index(env_cpu(env), 0), ra);
        break;
    }
}

static void do_stdby_e(CPUHPPAState *env, target_ulong addr, uint64_t val,
                       bool parallel, uintptr_t ra)
{
    switch (addr & 7) {
    case 7:
        /* The 7 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask64(env, addr - 7, val,
                                0xffffffffffffff00ull, 7, ra);
        } else {
            cpu_stl_data_ra(env, addr - 7, val >> 32, ra);
            cpu_stw_data_ra(env, addr - 3, val >> 16, ra);
            cpu_stb_data_ra(env, addr - 1, val >> 8, ra);
        }
        break;
    case 6:
        /* The 6 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask64(env, addr - 6, val,
                                0xffffffffffff0000ull, 6, ra);
        } else {
            cpu_stl_data_ra(env, addr - 6, val >> 32, ra);
            cpu_stw_data_ra(env, addr - 2, val >> 16, ra);
        }
        break;
    case 5:
        /* The 5 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask64(env, addr - 5, val,
                                0xffffffffff000000ull, 5, ra);
        } else {
            cpu_stl_data_ra(env, addr - 5, val >> 32, ra);
            cpu_stb_data_ra(env, addr - 1, val >> 24, ra);
        }
        break;
    case 4:
        cpu_stl_data_ra(env, addr - 4, val >> 32, ra);
        break;
    case 3:
        /* The 3 byte store must appear atomic.  */
        if (parallel) {
            atomic_store_mask32(env, addr - 3, val >> 32, 0xffffff00u, ra);
        } else {
            cpu_stw_data_ra(env, addr - 3, val >> 48, ra);
            cpu_stb_data_ra(env, addr - 1, val >> 40, ra);
        }
        break;
    case 2:
        cpu_stw_data_ra(env, addr - 2, val >> 48, ra);
        break;
    case 1:
        cpu_stb_data_ra(env, addr - 1, val >> 56, ra);
        break;
    default:
        /* Nothing is stored, but protection is checked and the
           cacheline is marked dirty.  */
        probe_write(env, addr, 0, cpu_mmu_index(env_cpu(env), 0), ra);
        break;
    }
}

void HELPER(stby_e)(CPUHPPAState *env, target_ulong addr, target_ulong val)
{
    do_stby_e(env, addr, val, false, GETPC());
}

void HELPER(stby_e_parallel)(CPUHPPAState *env, target_ulong addr,
                             target_ulong val)
{
    do_stby_e(env, addr, val, true, GETPC());
}

void HELPER(stdby_e)(CPUHPPAState *env, target_ulong addr, target_ulong val)
{
    do_stdby_e(env, addr, val, false, GETPC());
}

void HELPER(stdby_e_parallel)(CPUHPPAState *env, target_ulong addr,
                              target_ulong val)
{
    do_stdby_e(env, addr, val, true, GETPC());
}

void HELPER(ldc_check)(target_ulong addr)
{
    if (unlikely(addr & 0xf)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Undefined ldc to unaligned address mod 16: "
                      TARGET_FMT_lx "\n", addr);
    }
}

target_ulong HELPER(probe)(CPUHPPAState *env, target_ulong addr,
                          uint32_t level, uint32_t want)
{
#ifdef CONFIG_USER_ONLY
    return page_check_range(addr, 1, want);
#else
    int prot, excp, mmu_idx;
    hwaddr phys;

    trace_hppa_tlb_probe(addr, level, want);
    /* Fail if the requested privilege level is higher than current.  */
    if (level < (env->iaoq_f & 3)) {
        return 0;
    }

    mmu_idx = PRIV_P_TO_MMU_IDX(level, env->psw & PSW_P);
    excp = hppa_get_physical_address(env, addr, mmu_idx, 0, 0, &phys, &prot);
    if (excp >= 0) {
        cpu_restore_state(env_cpu(env), GETPC());
        hppa_set_ior_and_isr(env, addr, MMU_IDX_MMU_DISABLED(mmu_idx));
        if (excp == EXCP_DTLB_MISS) {
            excp = EXCP_NA_DTLB_MISS;
        }
        helper_excp(env, excp);
    }
    return (want & prot) != 0;
#endif
}

target_ulong HELPER(read_interval_timer)(void)
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

uint64_t HELPER(hadd_ss)(uint64_t r1, uint64_t r2)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = sextract64(r1, i, 16);
        int f2 = sextract64(r2, i, 16);
        int fr = f1 + f2;

        fr = MIN(fr, INT16_MAX);
        fr = MAX(fr, INT16_MIN);
        ret = deposit64(ret, i, 16, fr);
    }
    return ret;
}

uint64_t HELPER(hadd_us)(uint64_t r1, uint64_t r2)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = extract64(r1, i, 16);
        int f2 = sextract64(r2, i, 16);
        int fr = f1 + f2;

        fr = MIN(fr, UINT16_MAX);
        fr = MAX(fr, 0);
        ret = deposit64(ret, i, 16, fr);
    }
    return ret;
}

uint64_t HELPER(havg)(uint64_t r1, uint64_t r2)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = extract64(r1, i, 16);
        int f2 = extract64(r2, i, 16);
        int fr = f1 + f2;

        ret = deposit64(ret, i, 16, (fr >> 1) | (fr & 1));
    }
    return ret;
}

uint64_t HELPER(hsub_ss)(uint64_t r1, uint64_t r2)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = sextract64(r1, i, 16);
        int f2 = sextract64(r2, i, 16);
        int fr = f1 - f2;

        fr = MIN(fr, INT16_MAX);
        fr = MAX(fr, INT16_MIN);
        ret = deposit64(ret, i, 16, fr);
    }
    return ret;
}

uint64_t HELPER(hsub_us)(uint64_t r1, uint64_t r2)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = extract64(r1, i, 16);
        int f2 = sextract64(r2, i, 16);
        int fr = f1 - f2;

        fr = MIN(fr, UINT16_MAX);
        fr = MAX(fr, 0);
        ret = deposit64(ret, i, 16, fr);
    }
    return ret;
}

uint64_t HELPER(hshladd)(uint64_t r1, uint64_t r2, uint32_t sh)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = sextract64(r1, i, 16);
        int f2 = sextract64(r2, i, 16);
        int fr = (f1 << sh) + f2;

        fr = MIN(fr, INT16_MAX);
        fr = MAX(fr, INT16_MIN);
        ret = deposit64(ret, i, 16, fr);
    }
    return ret;
}

uint64_t HELPER(hshradd)(uint64_t r1, uint64_t r2, uint32_t sh)
{
    uint64_t ret = 0;

    for (int i = 0; i < 64; i += 16) {
        int f1 = sextract64(r1, i, 16);
        int f2 = sextract64(r2, i, 16);
        int fr = (f1 >> sh) + f2;

        fr = MIN(fr, INT16_MAX);
        fr = MAX(fr, INT16_MIN);
        ret = deposit64(ret, i, 16, fr);
    }
    return ret;
}
