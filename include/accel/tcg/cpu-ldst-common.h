/*
 * Software MMU support
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_CPU_LDST_COMMON_H
#define ACCEL_TCG_CPU_LDST_COMMON_H

#ifndef CONFIG_TCG
#error Can only include this header with TCG
#endif

#include "exec/memopidx.h"
#include "exec/vaddr.h"
#include "exec/mmu-access-type.h"
#include "qemu/int128.h"

uint8_t cpu_ldb_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
uint16_t cpu_ldw_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
uint32_t cpu_ldl_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
uint64_t cpu_ldq_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
Int128 cpu_ld16_mmu(CPUArchState *env, vaddr addr, MemOpIdx oi, uintptr_t ra);

void cpu_stb_mmu(CPUArchState *env, vaddr ptr, uint8_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stw_mmu(CPUArchState *env, vaddr ptr, uint16_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stl_mmu(CPUArchState *env, vaddr ptr, uint32_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stq_mmu(CPUArchState *env, vaddr ptr, uint64_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_st16_mmu(CPUArchState *env, vaddr addr, Int128 val,
                  MemOpIdx oi, uintptr_t ra);

uint32_t cpu_atomic_cmpxchgb_mmu(CPUArchState *env, vaddr addr,
                                 uint32_t cmpv, uint32_t newv,
                                 MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgw_le_mmu(CPUArchState *env, vaddr addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgl_le_mmu(CPUArchState *env, vaddr addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint64_t cpu_atomic_cmpxchgq_le_mmu(CPUArchState *env, vaddr addr,
                                    uint64_t cmpv, uint64_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgw_be_mmu(CPUArchState *env, vaddr addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgl_be_mmu(CPUArchState *env, vaddr addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint64_t cpu_atomic_cmpxchgq_be_mmu(CPUArchState *env, vaddr addr,
                                    uint64_t cmpv, uint64_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);

#define GEN_ATOMIC_HELPER(NAME, TYPE, SUFFIX)   \
TYPE cpu_atomic_ ## NAME ## SUFFIX ## _mmu      \
    (CPUArchState *env, vaddr addr, TYPE val,   \
     MemOpIdx oi, uintptr_t retaddr);

#ifdef CONFIG_ATOMIC64
#define GEN_ATOMIC_HELPER_ALL(NAME)          \
    GEN_ATOMIC_HELPER(NAME, uint32_t, b)     \
    GEN_ATOMIC_HELPER(NAME, uint32_t, w_le)  \
    GEN_ATOMIC_HELPER(NAME, uint32_t, w_be)  \
    GEN_ATOMIC_HELPER(NAME, uint32_t, l_le)  \
    GEN_ATOMIC_HELPER(NAME, uint32_t, l_be)  \
    GEN_ATOMIC_HELPER(NAME, uint64_t, q_le)  \
    GEN_ATOMIC_HELPER(NAME, uint64_t, q_be)
#else
#define GEN_ATOMIC_HELPER_ALL(NAME)          \
    GEN_ATOMIC_HELPER(NAME, uint32_t, b)     \
    GEN_ATOMIC_HELPER(NAME, uint32_t, w_le)  \
    GEN_ATOMIC_HELPER(NAME, uint32_t, w_be)  \
    GEN_ATOMIC_HELPER(NAME, uint32_t, l_le)  \
    GEN_ATOMIC_HELPER(NAME, uint32_t, l_be)
#endif

GEN_ATOMIC_HELPER_ALL(fetch_add)
GEN_ATOMIC_HELPER_ALL(fetch_sub)
GEN_ATOMIC_HELPER_ALL(fetch_and)
GEN_ATOMIC_HELPER_ALL(fetch_or)
GEN_ATOMIC_HELPER_ALL(fetch_xor)
GEN_ATOMIC_HELPER_ALL(fetch_smin)
GEN_ATOMIC_HELPER_ALL(fetch_umin)
GEN_ATOMIC_HELPER_ALL(fetch_smax)
GEN_ATOMIC_HELPER_ALL(fetch_umax)

GEN_ATOMIC_HELPER_ALL(add_fetch)
GEN_ATOMIC_HELPER_ALL(sub_fetch)
GEN_ATOMIC_HELPER_ALL(and_fetch)
GEN_ATOMIC_HELPER_ALL(or_fetch)
GEN_ATOMIC_HELPER_ALL(xor_fetch)
GEN_ATOMIC_HELPER_ALL(smin_fetch)
GEN_ATOMIC_HELPER_ALL(umin_fetch)
GEN_ATOMIC_HELPER_ALL(smax_fetch)
GEN_ATOMIC_HELPER_ALL(umax_fetch)

GEN_ATOMIC_HELPER_ALL(xchg)

Int128 cpu_atomic_cmpxchgo_le_mmu(CPUArchState *env, vaddr addr,
                                  Int128 cmpv, Int128 newv,
                                  MemOpIdx oi, uintptr_t retaddr);
Int128 cpu_atomic_cmpxchgo_be_mmu(CPUArchState *env, vaddr addr,
                                  Int128 cmpv, Int128 newv,
                                  MemOpIdx oi, uintptr_t retaddr);

GEN_ATOMIC_HELPER(xchg, Int128, o_le)
GEN_ATOMIC_HELPER(xchg, Int128, o_be)
GEN_ATOMIC_HELPER(fetch_and, Int128, o_le)
GEN_ATOMIC_HELPER(fetch_and, Int128, o_be)
GEN_ATOMIC_HELPER(fetch_or, Int128, o_le)
GEN_ATOMIC_HELPER(fetch_or, Int128, o_be)

#undef GEN_ATOMIC_HELPER_ALL
#undef GEN_ATOMIC_HELPER

uint8_t cpu_ldb_code_mmu(CPUArchState *env, vaddr addr,
                         MemOpIdx oi, uintptr_t ra);
uint16_t cpu_ldw_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra);
uint32_t cpu_ldl_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra);
uint64_t cpu_ldq_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra);

#endif /* ACCEL_TCG_CPU_LDST_COMMON_H */
