/*
 *  MIPS emulation load/store helpers for QEMU.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/memop.h"
#include "internal.h"

#ifndef CONFIG_USER_ONLY
#include "accel/tcg/probe.h"
#include "exec/tlb-flags.h"

#define HELPER_LD_ATOMIC(name, insn, almask, do_cast)                         \
target_ulong HELPER(name)(CPUMIPSState *env, target_ulong arg,                \
                           uint32_t memop_idx)                                \
{                                                                             \
    MemOpIdx oi = memop_idx;                                                  \
    unsigned mem_idx = get_mmuidx(oi);                                        \
    unsigned size = memop_size(get_memop(oi));                                \
    uintptr_t ra = GETPC();                                                   \
    CPUTLBEntryFull *full;                                                    \
    void *host_unused;                                                        \
    int flags;                                                                \
                                                                              \
    env->llval = do_cast cpu_##insn##_mmu(env, arg, oi, ra);                  \
    flags = probe_access_full(env, arg, size, MMU_DATA_LOAD, mem_idx,         \
                              true, &host_unused, &full, ra);                 \
    assert(!(flags & TLB_INVALID_MASK));                                      \
    env->CP0_LLAddr = full->phys_addr;                                        \
    env->lladdr = arg;                                                        \
    return env->llval;                                                        \
}
HELPER_LD_ATOMIC(ll, ldl, 0x3, (target_long)(int32_t))
#ifdef TARGET_MIPS64
HELPER_LD_ATOMIC(lld, ldq, 0x7, (target_ulong))
#endif
#undef HELPER_LD_ATOMIC

#endif /* !CONFIG_USER_ONLY */

static inline target_ulong get_lmask(CPUMIPSState *env,
                                     target_ulong value, unsigned bits)
{
    unsigned mask = (bits / BITS_PER_BYTE) - 1;

    value &= mask;

    if (!mips_env_is_bigendian(env)) {
        value ^= mask;
    }

    return value;
}

void HELPER(swl)(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    target_ulong lmask = get_lmask(env, arg2, 32);
    int dir = mips_env_is_bigendian(env) ? 1 : -1;

    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)(arg1 >> 24), mem_idx, GETPC());

    if (lmask <= 2) {
        cpu_stb_mmuidx_ra(env, arg2 + 1 * dir, (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (lmask <= 1) {
        cpu_stb_mmuidx_ra(env, arg2 + 2 * dir, (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (lmask == 0) {
        cpu_stb_mmuidx_ra(env, arg2 + 3 * dir, (uint8_t)arg1,
                          mem_idx, GETPC());
    }
}

void HELPER(swr)(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    target_ulong lmask = get_lmask(env, arg2, 32);
    int dir = mips_env_is_bigendian(env) ? 1 : -1;

    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)arg1, mem_idx, GETPC());

    if (lmask >= 1) {
        cpu_stb_mmuidx_ra(env, arg2 - 1 * dir, (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (lmask >= 2) {
        cpu_stb_mmuidx_ra(env, arg2 - 2 * dir, (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (lmask == 3) {
        cpu_stb_mmuidx_ra(env, arg2 - 3 * dir, (uint8_t)(arg1 >> 24),
                          mem_idx, GETPC());
    }
}

#if defined(TARGET_MIPS64)
/*
 * "half" load and stores.  We must do the memory access inline,
 * or fault handling won't work.
 */

void HELPER(sdl)(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    target_ulong lmask = get_lmask(env, arg2, 64);
    int dir = mips_env_is_bigendian(env) ? 1 : -1;

    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)(arg1 >> 56), mem_idx, GETPC());

    if (lmask <= 6) {
        cpu_stb_mmuidx_ra(env, arg2 + 1 * dir, (uint8_t)(arg1 >> 48),
                          mem_idx, GETPC());
    }

    if (lmask <= 5) {
        cpu_stb_mmuidx_ra(env, arg2 + 2 * dir, (uint8_t)(arg1 >> 40),
                          mem_idx, GETPC());
    }

    if (lmask <= 4) {
        cpu_stb_mmuidx_ra(env, arg2 + 3 * dir, (uint8_t)(arg1 >> 32),
                          mem_idx, GETPC());
    }

    if (lmask <= 3) {
        cpu_stb_mmuidx_ra(env, arg2 + 4 * dir, (uint8_t)(arg1 >> 24),
                          mem_idx, GETPC());
    }

    if (lmask <= 2) {
        cpu_stb_mmuidx_ra(env, arg2 + 5 * dir, (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (lmask <= 1) {
        cpu_stb_mmuidx_ra(env, arg2 + 6 * dir, (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (lmask <= 0) {
        cpu_stb_mmuidx_ra(env, arg2 + 7 * dir, (uint8_t)arg1,
                          mem_idx, GETPC());
    }
}

void HELPER(sdr)(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    target_ulong lmask = get_lmask(env, arg2, 64);
    int dir = mips_env_is_bigendian(env) ? 1 : -1;

    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)arg1, mem_idx, GETPC());

    if (lmask >= 1) {
        cpu_stb_mmuidx_ra(env, arg2 - 1 * dir, (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (lmask >= 2) {
        cpu_stb_mmuidx_ra(env, arg2 - 2 * dir, (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (lmask >= 3) {
        cpu_stb_mmuidx_ra(env, arg2 - 3 * dir, (uint8_t)(arg1 >> 24),
                          mem_idx, GETPC());
    }

    if (lmask >= 4) {
        cpu_stb_mmuidx_ra(env, arg2 - 4 * dir, (uint8_t)(arg1 >> 32),
                          mem_idx, GETPC());
    }

    if (lmask >= 5) {
        cpu_stb_mmuidx_ra(env, arg2 - 5 * dir, (uint8_t)(arg1 >> 40),
                          mem_idx, GETPC());
    }

    if (lmask >= 6) {
        cpu_stb_mmuidx_ra(env, arg2 - 6 * dir, (uint8_t)(arg1 >> 48),
                          mem_idx, GETPC());
    }

    if (lmask == 7) {
        cpu_stb_mmuidx_ra(env, arg2 - 7 * dir, (uint8_t)(arg1 >> 56),
                          mem_idx, GETPC());
    }
}
#endif /* TARGET_MIPS64 */

static const int multiple_regs[] = { 16, 17, 18, 19, 20, 21, 22, 23, 30 };

void HELPER(lwm)(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t memop_idx)
{
    MemOpIdx oi = memop_idx;
    unsigned base_reglist = reglist & 0xf;
    bool do_r31 = reglist & 0x10;
    target_ulong *gpr = env->active_tc.gpr;
    uintptr_t ra = GETPC();

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        for (unsigned i = 0; i < base_reglist; i++) {
            gpr[multiple_regs[i]] = (target_long)cpu_ldl_mmu(env, addr, oi, ra);
            addr += 4;
        }
    }

    if (do_r31) {
        gpr[31] = (target_long)cpu_ldl_mmu(env, addr, oi, ra);
    }
}

void HELPER(swm)(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t memop_idx)
{
    MemOpIdx oi = memop_idx;
    unsigned base_reglist = reglist & 0xf;
    bool do_r31 = reglist & 0x10;
    target_ulong *gpr = env->active_tc.gpr;
    uintptr_t ra = GETPC();

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        for (unsigned i = 0; i < base_reglist; i++) {
            cpu_stl_mmu(env, addr, gpr[multiple_regs[i]], oi, ra);
            addr += 4;
        }
    }

    if (do_r31) {
        cpu_stl_mmu(env, addr, gpr[31], oi, ra);
    }
}

#if defined(TARGET_MIPS64)
void HELPER(ldm)(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t memop_idx)
{
    MemOpIdx oi = memop_idx;
    unsigned base_reglist = reglist & 0xf;
    bool do_r31 = reglist & 0x10;
    target_ulong *gpr = env->active_tc.gpr;
    uintptr_t ra = GETPC();

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        for (unsigned i = 0; i < base_reglist; i++) {
            gpr[multiple_regs[i]] = cpu_ldq_mmu(env, addr, oi, ra);
            addr += 8;
        }
    }

    if (do_r31) {
        gpr[31] = cpu_ldq_mmu(env, addr, oi, ra);
    }
}

void HELPER(sdm)(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t memop_idx)
{
    MemOpIdx oi = memop_idx;
    unsigned base_reglist = reglist & 0xf;
    bool do_r31 = reglist & 0x10;
    target_ulong *gpr = env->active_tc.gpr;
    uintptr_t ra = GETPC();

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        for (unsigned i = 0; i < base_reglist; i++) {
            cpu_stq_mmu(env, addr, gpr[multiple_regs[i]], oi, ra);
            addr += 8;
        }
    }

    if (do_r31) {
        cpu_stq_mmu(env, addr, gpr[31], oi, ra);
    }
}

#endif /* TARGET_MIPS64 */
