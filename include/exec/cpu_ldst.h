/*
 *  Software MMU support
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
 *
 * store: cpu_st{size}{end}_{mmusuffix}(env, ptr, val)
 *        cpu_st{size}{end}_{mmusuffix}_ra(env, ptr, val, retaddr)
 *        cpu_st{size}{end}_mmuidx_ra(env, ptr, val, mmu_idx, retaddr)
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
 */
#ifndef CPU_LDST_H
#define CPU_LDST_H

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

/* All direct uses of g2h and h2g need to go away for usermode softmmu.  */
#define g2h(x) ((void *)((unsigned long)(abi_ptr)(x) + guest_base))

#if HOST_LONG_BITS <= TARGET_VIRT_ADDR_SPACE_BITS
#define guest_addr_valid(x) (1)
#else
#define guest_addr_valid(x) ((x) <= GUEST_ADDR_MAX)
#endif
#define h2g_valid(x) guest_addr_valid((unsigned long)(x) - guest_base)

static inline int guest_range_valid(unsigned long start, unsigned long len)
{
    return len - 1 <= GUEST_ADDR_MAX && start <= GUEST_ADDR_MAX - len + 1;
}

#define h2g_nocheck(x) ({ \
    unsigned long __ret = (unsigned long)(x) - guest_base; \
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

/*
 * Provide the same *_mmuidx_ra interface as for softmmu.
 * The mmu_idx argument is ignored.
 */

static inline uint32_t cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                          int mmu_idx, uintptr_t ra)
{
    return cpu_ldub_data_ra(env, addr, ra);
}

static inline int cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                     int mmu_idx, uintptr_t ra)
{
    return cpu_ldsb_data_ra(env, addr, ra);
}

static inline uint32_t cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                             int mmu_idx, uintptr_t ra)
{
    return cpu_lduw_be_data_ra(env, addr, ra);
}

static inline int cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        int mmu_idx, uintptr_t ra)
{
    return cpu_ldsw_be_data_ra(env, addr, ra);
}

static inline uint32_t cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                            int mmu_idx, uintptr_t ra)
{
    return cpu_ldl_be_data_ra(env, addr, ra);
}

static inline uint64_t cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                            int mmu_idx, uintptr_t ra)
{
    return cpu_ldq_be_data_ra(env, addr, ra);
}

static inline uint32_t cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                             int mmu_idx, uintptr_t ra)
{
    return cpu_lduw_le_data_ra(env, addr, ra);
}

static inline int cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        int mmu_idx, uintptr_t ra)
{
    return cpu_ldsw_le_data_ra(env, addr, ra);
}

static inline uint32_t cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                            int mmu_idx, uintptr_t ra)
{
    return cpu_ldl_le_data_ra(env, addr, ra);
}

static inline uint64_t cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                            int mmu_idx, uintptr_t ra)
{
    return cpu_ldq_le_data_ra(env, addr, ra);
}

static inline void cpu_stb_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                     uint32_t val, int mmu_idx, uintptr_t ra)
{
    cpu_stb_data_ra(env, addr, val, ra);
}

static inline void cpu_stw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        uint32_t val, int mmu_idx,
                                        uintptr_t ra)
{
    cpu_stw_be_data_ra(env, addr, val, ra);
}

static inline void cpu_stl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        uint32_t val, int mmu_idx,
                                        uintptr_t ra)
{
    cpu_stl_be_data_ra(env, addr, val, ra);
}

static inline void cpu_stq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        uint64_t val, int mmu_idx,
                                        uintptr_t ra)
{
    cpu_stq_be_data_ra(env, addr, val, ra);
}

static inline void cpu_stw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        uint32_t val, int mmu_idx,
                                        uintptr_t ra)
{
    cpu_stw_le_data_ra(env, addr, val, ra);
}

static inline void cpu_stl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        uint32_t val, int mmu_idx,
                                        uintptr_t ra)
{
    cpu_stl_le_data_ra(env, addr, val, ra);
}

static inline void cpu_stq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                                        uint64_t val, int mmu_idx,
                                        uintptr_t ra)
{
    cpu_stq_le_data_ra(env, addr, val, ra);
}

#else

/* Needed for TCG_OVERSIZED_GUEST */
#include "tcg/tcg.h"

static inline target_ulong tlb_addr_write(const CPUTLBEntry *entry)
{
#if TCG_OVERSIZED_GUEST
    return entry->addr_write;
#else
    return atomic_read(&entry->addr_write);
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

uint32_t cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                            int mmu_idx, uintptr_t ra);
int cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                       int mmu_idx, uintptr_t ra);

uint32_t cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra);
int cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra);
uint32_t cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra);
uint64_t cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra);

uint32_t cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra);
int cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra);
uint32_t cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra);
uint64_t cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra);

void cpu_stb_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                       int mmu_idx, uintptr_t retaddr);

void cpu_stw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr);
void cpu_stl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr);
void cpu_stq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                          int mmu_idx, uintptr_t retaddr);

void cpu_stw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr);
void cpu_stl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr);
void cpu_stq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                          int mmu_idx, uintptr_t retaddr);

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
# define cpu_stw_data         cpu_stw_be_data
# define cpu_stl_data         cpu_stl_be_data
# define cpu_stq_data         cpu_stq_be_data
# define cpu_stw_data_ra      cpu_stw_be_data_ra
# define cpu_stl_data_ra      cpu_stl_be_data_ra
# define cpu_stq_data_ra      cpu_stq_be_data_ra
# define cpu_stw_mmuidx_ra    cpu_stw_be_mmuidx_ra
# define cpu_stl_mmuidx_ra    cpu_stl_be_mmuidx_ra
# define cpu_stq_mmuidx_ra    cpu_stq_be_mmuidx_ra
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
# define cpu_stw_data         cpu_stw_le_data
# define cpu_stl_data         cpu_stl_le_data
# define cpu_stq_data         cpu_stq_le_data
# define cpu_stw_data_ra      cpu_stw_le_data_ra
# define cpu_stl_data_ra      cpu_stl_le_data_ra
# define cpu_stq_data_ra      cpu_stq_le_data_ra
# define cpu_stw_mmuidx_ra    cpu_stw_le_mmuidx_ra
# define cpu_stl_mmuidx_ra    cpu_stl_le_mmuidx_ra
# define cpu_stq_mmuidx_ra    cpu_stq_le_mmuidx_ra
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
    return g2h(addr);
}
#else
void *tlb_vaddr_to_host(CPUArchState *env, abi_ptr addr,
                        MMUAccessType access_type, int mmu_idx);
#endif

#endif /* CPU_LDST_H */
