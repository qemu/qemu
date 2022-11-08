/*
 * Common Atomic Helper Functions
 *
 * This file should be included before the various instantiations of
 * the atomic_template.h helpers.
 *
 * Copyright (c) 2019 Linaro
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

static void atomic_trace_rmw_post(CPUArchState *env, target_ulong addr,
                                  MemOpIdx oi)
{
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_RW);
}

#if HAVE_ATOMIC128
static void atomic_trace_ld_post(CPUArchState *env, target_ulong addr,
                                 MemOpIdx oi)
{
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
}

static void atomic_trace_st_post(CPUArchState *env, target_ulong addr,
                                 MemOpIdx oi)
{
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}
#endif

/*
 * Atomic helpers callable from TCG.
 * These have a common interface and all defer to cpu_atomic_*
 * using the host return address from GETPC().
 */

#define CMPXCHG_HELPER(OP, TYPE) \
    TYPE HELPER(atomic_##OP)(CPUArchState *env, target_ulong addr,  \
                             TYPE oldv, TYPE newv, uint32_t oi)     \
    { return cpu_atomic_##OP##_mmu(env, addr, oldv, newv, oi, GETPC()); }

CMPXCHG_HELPER(cmpxchgb, uint32_t)
CMPXCHG_HELPER(cmpxchgw_be, uint32_t)
CMPXCHG_HELPER(cmpxchgw_le, uint32_t)
CMPXCHG_HELPER(cmpxchgl_be, uint32_t)
CMPXCHG_HELPER(cmpxchgl_le, uint32_t)

#ifdef CONFIG_ATOMIC64
CMPXCHG_HELPER(cmpxchgq_be, uint64_t)
CMPXCHG_HELPER(cmpxchgq_le, uint64_t)
#endif

#ifdef CONFIG_CMPXCHG128
CMPXCHG_HELPER(cmpxchgo_be, Int128)
CMPXCHG_HELPER(cmpxchgo_le, Int128)
#endif

#undef CMPXCHG_HELPER

Int128 HELPER(nonatomic_cmpxchgo_be)(CPUArchState *env, target_ulong addr,
                                     Int128 cmpv, Int128 newv, uint32_t oi)
{
#if TCG_TARGET_REG_BITS == 32
    uintptr_t ra = GETPC();
    Int128 oldv;

    oldv = cpu_ld16_be_mmu(env, addr, oi, ra);
    if (int128_eq(oldv, cmpv)) {
        cpu_st16_be_mmu(env, addr, newv, oi, ra);
    } else {
        /* Even with comparison failure, still need a write cycle. */
        probe_write(env, addr, 16, get_mmuidx(oi), ra);
    }
    return oldv;
#else
    g_assert_not_reached();
#endif
}

Int128 HELPER(nonatomic_cmpxchgo_le)(CPUArchState *env, target_ulong addr,
                                     Int128 cmpv, Int128 newv, uint32_t oi)
{
#if TCG_TARGET_REG_BITS == 32
    uintptr_t ra = GETPC();
    Int128 oldv;

    oldv = cpu_ld16_le_mmu(env, addr, oi, ra);
    if (int128_eq(oldv, cmpv)) {
        cpu_st16_le_mmu(env, addr, newv, oi, ra);
    } else {
        /* Even with comparison failure, still need a write cycle. */
        probe_write(env, addr, 16, get_mmuidx(oi), ra);
    }
    return oldv;
#else
    g_assert_not_reached();
#endif
}

#define ATOMIC_HELPER(OP, TYPE) \
    TYPE HELPER(glue(atomic_,OP))(CPUArchState *env, target_ulong addr,  \
                                  TYPE val, uint32_t oi)                 \
    { return glue(glue(cpu_atomic_,OP),_mmu)(env, addr, val, oi, GETPC()); }

#ifdef CONFIG_ATOMIC64
#define GEN_ATOMIC_HELPERS(OP)              \
    ATOMIC_HELPER(glue(OP,b), uint32_t)     \
    ATOMIC_HELPER(glue(OP,w_be), uint32_t)  \
    ATOMIC_HELPER(glue(OP,w_le), uint32_t)  \
    ATOMIC_HELPER(glue(OP,l_be), uint32_t)  \
    ATOMIC_HELPER(glue(OP,l_le), uint32_t)  \
    ATOMIC_HELPER(glue(OP,q_be), uint64_t)  \
    ATOMIC_HELPER(glue(OP,q_le), uint64_t)
#else
#define GEN_ATOMIC_HELPERS(OP)              \
    ATOMIC_HELPER(glue(OP,b), uint32_t)     \
    ATOMIC_HELPER(glue(OP,w_be), uint32_t)  \
    ATOMIC_HELPER(glue(OP,w_le), uint32_t)  \
    ATOMIC_HELPER(glue(OP,l_be), uint32_t)  \
    ATOMIC_HELPER(glue(OP,l_le), uint32_t)
#endif

GEN_ATOMIC_HELPERS(fetch_add)
GEN_ATOMIC_HELPERS(fetch_and)
GEN_ATOMIC_HELPERS(fetch_or)
GEN_ATOMIC_HELPERS(fetch_xor)
GEN_ATOMIC_HELPERS(fetch_smin)
GEN_ATOMIC_HELPERS(fetch_umin)
GEN_ATOMIC_HELPERS(fetch_smax)
GEN_ATOMIC_HELPERS(fetch_umax)

GEN_ATOMIC_HELPERS(add_fetch)
GEN_ATOMIC_HELPERS(and_fetch)
GEN_ATOMIC_HELPERS(or_fetch)
GEN_ATOMIC_HELPERS(xor_fetch)
GEN_ATOMIC_HELPERS(smin_fetch)
GEN_ATOMIC_HELPERS(umin_fetch)
GEN_ATOMIC_HELPERS(smax_fetch)
GEN_ATOMIC_HELPERS(umax_fetch)

GEN_ATOMIC_HELPERS(xchg)

#undef ATOMIC_HELPER
#undef GEN_ATOMIC_HELPERS
