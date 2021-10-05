/*
 *  Software MMU support
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

/*
 * Generate inline load/store functions for all MMU modes (typically
 * at least _user and _kernel) as well as _data versions, for all data
 * sizes.
 *
 * Used by target op helpers.
 *
 * The syntax for the accessors is:
 *
 * load:  cpu_ld{sign}{size}{end}_{mmusuffix}(env, ptr)
 *        cpu_ld{sign}{size}{end}_{mmusuffix}_ra(env, ptr, retaddr)
 *        cpu_ld{sign}{size}{end}_mmuidx_ra(env, ptr, mmu_idx, retaddr)
 *        cpu_ld{sign}{size}{end}_mmu(env, ptr, oi, retaddr)
 *
 * store: cpu_st{size}{end}_{mmusuffix}(env, ptr, val)
 *        cpu_st{size}{end}_{mmusuffix}_ra(env, ptr, val, retaddr)
 *        cpu_st{size}{end}_mmuidx_ra(env, ptr, val, mmu_idx, retaddr)
 *        cpu_st{size}{end}_mmu(env, ptr, val, oi, retaddr)
 *
 * sign is:
 * (empty): for 32 and 64 bit sizes
 *   u    : unsigned
 *   s    : signed
 *
 * size is:
 *   b: 8 bits
 *   w: 16 bits
 *   l: 32 bits
 *   q: 64 bits
 *
 * end is:
 * (empty): for target native endian, or for 8 bit access
 *     _be: for forced big endian
 *     _le: for forced little endian
 *
 * mmusuffix is one of the generic suffixes "data" or "code", or "mmuidx".
 * The "mmuidx" suffix carries an extra mmu_idx argument that specifies
 * the index to use; the "data" and "code" suffixes take the index from
 * cpu_mmu_index().
 *
 * The "mmu" suffix carries the full MemOpIdx, with both mmu_idx and the
 * MemOp including alignment requirements.  The alignment will be enforced.
 */
#ifndef CPU_LDST_H
#define CPU_LDST_H

#include "exec/memopidx.h"
#include "qemu/int128.h"

#if defined(CONFIG_USER_ONLY)
/* sparc32plus has 64bit long but 32bit space address
 * this can make bad result with g2h() and h2g()
 */
#if TARGET_VIRT_ADDR_SPACE_BITS <= 32
typedef uint32_t abi_ptr;
#define TARGET_ABI_FMT_ptr "%x"
#else
typedef uint64_t abi_ptr;
#define TARGET_ABI_FMT_ptr "%"PRIx64
#endif

#ifndef TARGET_TAGGED_ADDRESSES
static inline abi_ptr cpu_untagged_addr(CPUState *cs, abi_ptr x)
{
    return x;
}
#endif

/* All direct uses of g2h and h2g need to go away for usermode softmmu.  */
static inline void *g2h_untagged(abi_ptr x)
{
    return (void *)((uintptr_t)(x) + guest_base);
}

static inline void *g2h(CPUState *cs, abi_ptr x)
{
    return g2h_untagged(cpu_untagged_addr(cs, x));
}

static inline bool guest_addr_valid_untagged(abi_ulong x)
{
    return x <= GUEST_ADDR_MAX;
}

static inline bool guest_range_valid_untagged(abi_ulong start, abi_ulong len)
{
    return len - 1 <= GUEST_ADDR_MAX && start <= GUEST_ADDR_MAX - len + 1;
}

#define h2g_valid(x) \
    (HOST_LONG_BITS <= TARGET_VIRT_ADDR_SPACE_BITS || \
     (uintptr_t)(x) - guest_base <= GUEST_ADDR_MAX)

#define h2g_nocheck(x) ({ \
    uintptr_t __ret = (uintptr_t)(x) - guest_base; \
    (abi_ptr)__ret; \
})

#define h2g(x) ({ \
    /* Check if given address fits target address space */ \
    assert(h2g_valid(x)); \
    h2g_nocheck(x); \
})
#else
typedef target_ulong abi_ptr;
#define TARGET_ABI_FMT_ptr TARGET_ABI_FMT_lx
#endif

uint32_t cpu_ldub_data(CPUArchState *env, abi_ptr ptr);
int cpu_ldsb_data(CPUArchState *env, abi_ptr ptr);
uint32_t cpu_lduw_be_data(CPUArchState *env, abi_ptr ptr);
int cpu_ldsw_be_data(CPUArchState *env, abi_ptr ptr);
uint32_t cpu_ldl_be_data(CPUArchState *env, abi_ptr ptr);
uint64_t cpu_ldq_be_data(CPUArchState *env, abi_ptr ptr);
uint32_t cpu_lduw_le_data(CPUArchState *env, abi_ptr ptr);
int cpu_ldsw_le_data(CPUArchState *env, abi_ptr ptr);
uint32_t cpu_ldl_le_data(CPUArchState *env, abi_ptr ptr);
uint64_t cpu_ldq_le_data(CPUArchState *env, abi_ptr ptr);

uint32_t cpu_ldub_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
int cpu_ldsb_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
uint32_t cpu_lduw_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
int cpu_ldsw_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
uint32_t cpu_ldl_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
uint64_t cpu_ldq_be_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
uint32_t cpu_lduw_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
int cpu_ldsw_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
uint32_t cpu_ldl_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);
uint64_t cpu_ldq_le_data_ra(CPUArchState *env, abi_ptr ptr, uintptr_t ra);

void cpu_stb_data(CPUArchState *env, abi_ptr ptr, uint32_t val);
void cpu_stw_be_data(CPUArchState *env, abi_ptr ptr, uint32_t val);
void cpu_stl_be_data(CPUArchState *env, abi_ptr ptr, uint32_t val);
void cpu_stq_be_data(CPUArchState *env, abi_ptr ptr, uint64_t val);
void cpu_stw_le_data(CPUArchState *env, abi_ptr ptr, uint32_t val);
void cpu_stl_le_data(CPUArchState *env, abi_ptr ptr, uint32_t val);
void cpu_stq_le_data(CPUArchState *env, abi_ptr ptr, uint64_t val);

void cpu_stb_data_ra(CPUArchState *env, abi_ptr ptr,
                     uint32_t val, uintptr_t ra);
void cpu_stw_be_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t ra);
void cpu_stl_be_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t ra);
void cpu_stq_be_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint64_t val, uintptr_t ra);
void cpu_stw_le_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t ra);
void cpu_stl_le_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint32_t val, uintptr_t ra);
void cpu_stq_le_data_ra(CPUArchState *env, abi_ptr ptr,
                        uint64_t val, uintptr_t ra);

uint32_t cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                            int mmu_idx, uintptr_t ra);
int cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                       int mmu_idx, uintptr_t ra);
uint32_t cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                               int mmu_idx, uintptr_t ra);
int cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                          int mmu_idx, uintptr_t ra);
uint32_t cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                              int mmu_idx, uintptr_t ra);
uint64_t cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                              int mmu_idx, uintptr_t ra);
uint32_t cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                               int mmu_idx, uintptr_t ra);
int cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                          int mmu_idx, uintptr_t ra);
uint32_t cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                              int mmu_idx, uintptr_t ra);
uint64_t cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr,
                              int mmu_idx, uintptr_t ra);

void cpu_stb_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint32_t val,
                       int mmu_idx, uintptr_t ra);
void cpu_stw_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint32_t val,
                          int mmu_idx, uintptr_t ra);
void cpu_stl_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint32_t val,
                          int mmu_idx, uintptr_t ra);
void cpu_stq_be_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint64_t val,
                          int mmu_idx, uintptr_t ra);
void cpu_stw_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint32_t val,
                          int mmu_idx, uintptr_t ra);
void cpu_stl_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint32_t val,
                          int mmu_idx, uintptr_t ra);
void cpu_stq_le_mmuidx_ra(CPUArchState *env, abi_ptr ptr, uint64_t val,
                          int mmu_idx, uintptr_t ra);

uint8_t cpu_ldb_mmu(CPUArchState *env, abi_ptr ptr, MemOpIdx oi, uintptr_t ra);
uint16_t cpu_ldw_be_mmu(CPUArchState *env, abi_ptr ptr,
                        MemOpIdx oi, uintptr_t ra);
uint32_t cpu_ldl_be_mmu(CPUArchState *env, abi_ptr ptr,
                        MemOpIdx oi, uintptr_t ra);
uint64_t cpu_ldq_be_mmu(CPUArchState *env, abi_ptr ptr,
                        MemOpIdx oi, uintptr_t ra);
uint16_t cpu_ldw_le_mmu(CPUArchState *env, abi_ptr ptr,
                        MemOpIdx oi, uintptr_t ra);
uint32_t cpu_ldl_le_mmu(CPUArchState *env, abi_ptr ptr,
                        MemOpIdx oi, uintptr_t ra);
uint64_t cpu_ldq_le_mmu(CPUArchState *env, abi_ptr ptr,
                        MemOpIdx oi, uintptr_t ra);

void cpu_stb_mmu(CPUArchState *env, abi_ptr ptr, uint8_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stw_be_mmu(CPUArchState *env, abi_ptr ptr, uint16_t val,
                    MemOpIdx oi, uintptr_t ra);
void cpu_stl_be_mmu(CPUArchState *env, abi_ptr ptr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra);
void cpu_stq_be_mmu(CPUArchState *env, abi_ptr ptr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra);
void cpu_stw_le_mmu(CPUArchState *env, abi_ptr ptr, uint16_t val,
                    MemOpIdx oi, uintptr_t ra);
void cpu_stl_le_mmu(CPUArchState *env, abi_ptr ptr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra);
void cpu_stq_le_mmu(CPUArchState *env, abi_ptr ptr, uint64_t val,
                    MemOpIdx oi, uintptr_t ra);

uint32_t cpu_atomic_cmpxchgb_mmu(CPUArchState *env, target_ulong addr,
                                 uint32_t cmpv, uint32_t newv,
                                 MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgw_le_mmu(CPUArchState *env, target_ulong addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgl_le_mmu(CPUArchState *env, target_ulong addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint64_t cpu_atomic_cmpxchgq_le_mmu(CPUArchState *env, target_ulong addr,
                                    uint64_t cmpv, uint64_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgw_be_mmu(CPUArchState *env, target_ulong addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint32_t cpu_atomic_cmpxchgl_be_mmu(CPUArchState *env, target_ulong addr,
                                    uint32_t cmpv, uint32_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);
uint64_t cpu_atomic_cmpxchgq_be_mmu(CPUArchState *env, target_ulong addr,
                                    uint64_t cmpv, uint64_t newv,
                                    MemOpIdx oi, uintptr_t retaddr);

#define GEN_ATOMIC_HELPER(NAME, TYPE, SUFFIX)         \
TYPE cpu_atomic_ ## NAME ## SUFFIX ## _mmu            \
    (CPUArchState *env, target_ulong addr, TYPE val,  \
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

#undef GEN_ATOMIC_HELPER_ALL
#undef GEN_ATOMIC_HELPER

Int128 cpu_atomic_cmpxchgo_le_mmu(CPUArchState *env, target_ulong addr,
                                  Int128 cmpv, Int128 newv,
                                  MemOpIdx oi, uintptr_t retaddr);
Int128 cpu_atomic_cmpxchgo_be_mmu(CPUArchState *env, target_ulong addr,
                                  Int128 cmpv, Int128 newv,
                                  MemOpIdx oi, uintptr_t retaddr);

Int128 cpu_atomic_ldo_le_mmu(CPUArchState *env, target_ulong addr,
                             MemOpIdx oi, uintptr_t retaddr);
Int128 cpu_atomic_ldo_be_mmu(CPUArchState *env, target_ulong addr,
                             MemOpIdx oi, uintptr_t retaddr);
void cpu_atomic_sto_le_mmu(CPUArchState *env, target_ulong addr, Int128 val,
                           MemOpIdx oi, uintptr_t retaddr);
void cpu_atomic_sto_be_mmu(CPUArchState *env, target_ulong addr, Int128 val,
                           MemOpIdx oi, uintptr_t retaddr);

#if defined(CONFIG_USER_ONLY)

extern __thread uintptr_t helper_retaddr;

static inline void set_helper_retaddr(uintptr_t ra)
{
    helper_retaddr = ra;
    /*
     * Ensure that this write is visible to the SIGSEGV handler that
     * may be invoked due to a subsequent invalid memory operation.
     */
    signal_barrier();
}

static inline void clear_helper_retaddr(void)
{
    /*
     * Ensure that previous memory operations have succeeded before
     * removing the data visible to the signal handler.
     */
    signal_barrier();
    helper_retaddr = 0;
}

#else

/* Needed for TCG_OVERSIZED_GUEST */
#include "tcg/tcg.h"

static inline target_ulong tlb_addr_write(const CPUTLBEntry *entry)
{
#if TCG_OVERSIZED_GUEST
    return entry->addr_write;
#else
    return qatomic_read(&entry->addr_write);
#endif
}

/* Find the TLB index corresponding to the mmu_idx + address pair.  */
static inline uintptr_t tlb_index(CPUArchState *env, uintptr_t mmu_idx,
                                  target_ulong addr)
{
    uintptr_t size_mask = env_tlb(env)->f[mmu_idx].mask >> CPU_TLB_ENTRY_BITS;

    return (addr >> TARGET_PAGE_BITS) & size_mask;
}

/* Find the TLB entry corresponding to the mmu_idx + address pair.  */
static inline CPUTLBEntry *tlb_entry(CPUArchState *env, uintptr_t mmu_idx,
                                     target_ulong addr)
{
    return &env_tlb(env)->f[mmu_idx].table[tlb_index(env, mmu_idx, addr)];
}

#endif /* defined(CONFIG_USER_ONLY) */

#ifdef TARGET_WORDS_BIGENDIAN
# define cpu_lduw_data        cpu_lduw_be_data
# define cpu_ldsw_data        cpu_ldsw_be_data
# define cpu_ldl_data         cpu_ldl_be_data
# define cpu_ldq_data         cpu_ldq_be_data
# define cpu_lduw_data_ra     cpu_lduw_be_data_ra
# define cpu_ldsw_data_ra     cpu_ldsw_be_data_ra
# define cpu_ldl_data_ra      cpu_ldl_be_data_ra
# define cpu_ldq_data_ra      cpu_ldq_be_data_ra
# define cpu_lduw_mmuidx_ra   cpu_lduw_be_mmuidx_ra
# define cpu_ldsw_mmuidx_ra   cpu_ldsw_be_mmuidx_ra
# define cpu_ldl_mmuidx_ra    cpu_ldl_be_mmuidx_ra
# define cpu_ldq_mmuidx_ra    cpu_ldq_be_mmuidx_ra
# define cpu_ldw_mmu          cpu_ldw_be_mmu
# define cpu_ldl_mmu          cpu_ldl_be_mmu
# define cpu_ldq_mmu          cpu_ldq_be_mmu
# define cpu_stw_data         cpu_stw_be_data
# define cpu_stl_data         cpu_stl_be_data
# define cpu_stq_data         cpu_stq_be_data
# define cpu_stw_data_ra      cpu_stw_be_data_ra
# define cpu_stl_data_ra      cpu_stl_be_data_ra
# define cpu_stq_data_ra      cpu_stq_be_data_ra
# define cpu_stw_mmuidx_ra    cpu_stw_be_mmuidx_ra
# define cpu_stl_mmuidx_ra    cpu_stl_be_mmuidx_ra
# define cpu_stq_mmuidx_ra    cpu_stq_be_mmuidx_ra
# define cpu_stw_mmu          cpu_stw_be_mmu
# define cpu_stl_mmu          cpu_stl_be_mmu
# define cpu_stq_mmu          cpu_stq_be_mmu
#else
# define cpu_lduw_data        cpu_lduw_le_data
# define cpu_ldsw_data        cpu_ldsw_le_data
# define cpu_ldl_data         cpu_ldl_le_data
# define cpu_ldq_data         cpu_ldq_le_data
# define cpu_lduw_data_ra     cpu_lduw_le_data_ra
# define cpu_ldsw_data_ra     cpu_ldsw_le_data_ra
# define cpu_ldl_data_ra      cpu_ldl_le_data_ra
# define cpu_ldq_data_ra      cpu_ldq_le_data_ra
# define cpu_lduw_mmuidx_ra   cpu_lduw_le_mmuidx_ra
# define cpu_ldsw_mmuidx_ra   cpu_ldsw_le_mmuidx_ra
# define cpu_ldl_mmuidx_ra    cpu_ldl_le_mmuidx_ra
# define cpu_ldq_mmuidx_ra    cpu_ldq_le_mmuidx_ra
# define cpu_ldw_mmu          cpu_ldw_le_mmu
# define cpu_ldl_mmu          cpu_ldl_le_mmu
# define cpu_ldq_mmu          cpu_ldq_le_mmu
# define cpu_stw_data         cpu_stw_le_data
# define cpu_stl_data         cpu_stl_le_data
# define cpu_stq_data         cpu_stq_le_data
# define cpu_stw_data_ra      cpu_stw_le_data_ra
# define cpu_stl_data_ra      cpu_stl_le_data_ra
# define cpu_stq_data_ra      cpu_stq_le_data_ra
# define cpu_stw_mmuidx_ra    cpu_stw_le_mmuidx_ra
# define cpu_stl_mmuidx_ra    cpu_stl_le_mmuidx_ra
# define cpu_stq_mmuidx_ra    cpu_stq_le_mmuidx_ra
# define cpu_stw_mmu          cpu_stw_le_mmu
# define cpu_stl_mmu          cpu_stl_le_mmu
# define cpu_stq_mmu          cpu_stq_le_mmu
#endif

uint32_t cpu_ldub_code(CPUArchState *env, abi_ptr addr);
uint32_t cpu_lduw_code(CPUArchState *env, abi_ptr addr);
uint32_t cpu_ldl_code(CPUArchState *env, abi_ptr addr);
uint64_t cpu_ldq_code(CPUArchState *env, abi_ptr addr);

static inline int cpu_ldsb_code(CPUArchState *env, abi_ptr addr)
{
    return (int8_t)cpu_ldub_code(env, addr);
}

static inline int cpu_ldsw_code(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_code(env, addr);
}

/**
 * tlb_vaddr_to_host:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @access_type: 0 for read, 1 for write, 2 for execute
 * @mmu_idx: MMU index to use for lookup
 *
 * Look up the specified guest virtual index in the TCG softmmu TLB.
 * If we can translate a host virtual address suitable for direct RAM
 * access, without causing a guest exception, then return it.
 * Otherwise (TLB entry is for an I/O access, guest software
 * TLB fill required, etc) return NULL.
 */
#ifdef CONFIG_USER_ONLY
static inline void *tlb_vaddr_to_host(CPUArchState *env, abi_ptr addr,
                                      MMUAccessType access_type, int mmu_idx)
{
    return g2h(env_cpu(env), addr);
}
#else
void *tlb_vaddr_to_host(CPUArchState *env, abi_ptr addr,
                        MMUAccessType access_type, int mmu_idx);
#endif

#endif /* CPU_LDST_H */
