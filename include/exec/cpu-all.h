/*
 * defines common to all virtual CPUs
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#ifndef CPU_ALL_H
#define CPU_ALL_H

#include "exec/page-protection.h"
#include "exec/cpu-common.h"
#include "exec/memory.h"
#include "exec/tswap.h"
#include "hw/core/cpu.h"

/* some important defines:
 *
 * HOST_BIG_ENDIAN : whether the host cpu is big endian and
 * otherwise little endian.
 *
 * TARGET_BIG_ENDIAN : same for the target cpu
 */

#if HOST_BIG_ENDIAN != TARGET_BIG_ENDIAN
#define BSWAP_NEEDED
#endif

/* Target-endianness CPU memory access functions. These fit into the
 * {ld,st}{type}{sign}{size}{endian}_p naming scheme described in bswap.h.
 */
#if TARGET_BIG_ENDIAN
#define lduw_p(p) lduw_be_p(p)
#define ldsw_p(p) ldsw_be_p(p)
#define ldl_p(p) ldl_be_p(p)
#define ldq_p(p) ldq_be_p(p)
#define stw_p(p, v) stw_be_p(p, v)
#define stl_p(p, v) stl_be_p(p, v)
#define stq_p(p, v) stq_be_p(p, v)
#define ldn_p(p, sz) ldn_be_p(p, sz)
#define stn_p(p, sz, v) stn_be_p(p, sz, v)
#else
#define lduw_p(p) lduw_le_p(p)
#define ldsw_p(p) ldsw_le_p(p)
#define ldl_p(p) ldl_le_p(p)
#define ldq_p(p) ldq_le_p(p)
#define stw_p(p, v) stw_le_p(p, v)
#define stl_p(p, v) stl_le_p(p, v)
#define stq_p(p, v) stq_le_p(p, v)
#define ldn_p(p, sz) ldn_le_p(p, sz)
#define stn_p(p, sz, v) stn_le_p(p, sz, v)
#endif

/* MMU memory access macros */

#if defined(CONFIG_USER_ONLY)
#include "user/abitypes.h"

/*
 * If non-zero, the guest virtual address space is a contiguous subset
 * of the host virtual address space, i.e. '-R reserved_va' is in effect
 * either from the command-line or by default.  The value is the last
 * byte of the guest address space e.g. UINT32_MAX.
 *
 * If zero, the host and guest virtual address spaces are intermingled.
 */
extern unsigned long reserved_va;

/*
 * Limit the guest addresses as best we can.
 *
 * When not using -R reserved_va, we cannot really limit the guest
 * to less address space than the host.  For 32-bit guests, this
 * acts as a sanity check that we're not giving the guest an address
 * that it cannot even represent.  For 64-bit guests... the address
 * might not be what the real kernel would give, but it is at least
 * representable in the guest.
 *
 * TODO: Improve address allocation to avoid this problem, and to
 * avoid setting bits at the top of guest addresses that might need
 * to be used for tags.
 */
#define GUEST_ADDR_MAX_                                                 \
    ((MIN_CONST(TARGET_VIRT_ADDR_SPACE_BITS, TARGET_ABI_BITS) <= 32) ?  \
     UINT32_MAX : ~0ul)
#define GUEST_ADDR_MAX    (reserved_va ? : GUEST_ADDR_MAX_)

#else

#include "exec/hwaddr.h"

#define SUFFIX
#define ARG1         as
#define ARG1_DECL    AddressSpace *as
#define TARGET_ENDIANNESS
#include "exec/memory_ldst.h.inc"

#define SUFFIX       _cached_slow
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#define TARGET_ENDIANNESS
#include "exec/memory_ldst.h.inc"

static inline void stl_phys_notdirty(AddressSpace *as, hwaddr addr, uint32_t val)
{
    address_space_stl_notdirty(as, addr, val,
                               MEMTXATTRS_UNSPECIFIED, NULL);
}

#define SUFFIX
#define ARG1         as
#define ARG1_DECL    AddressSpace *as
#define TARGET_ENDIANNESS
#include "exec/memory_ldst_phys.h.inc"

/* Inline fast path for direct RAM access.  */
#define ENDIANNESS
#include "exec/memory_ldst_cached.h.inc"

#define SUFFIX       _cached
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#define TARGET_ENDIANNESS
#include "exec/memory_ldst_phys.h.inc"
#endif

/* page related stuff */

#ifdef TARGET_PAGE_BITS_VARY
# include "exec/page-vary.h"
extern const TargetPageBits target_page;
# ifdef CONFIG_DEBUG_TCG
#  define TARGET_PAGE_BITS   ({ assert(target_page.decided); \
                                target_page.bits; })
#  define TARGET_PAGE_MASK   ({ assert(target_page.decided); \
                                (target_long)target_page.mask; })
# else
#  define TARGET_PAGE_BITS   target_page.bits
#  define TARGET_PAGE_MASK   ((target_long)target_page.mask)
# endif
# define TARGET_PAGE_SIZE    (-(int)TARGET_PAGE_MASK)
#else
# define TARGET_PAGE_BITS_MIN TARGET_PAGE_BITS
# define TARGET_PAGE_SIZE    (1 << TARGET_PAGE_BITS)
# define TARGET_PAGE_MASK    ((target_long)-1 << TARGET_PAGE_BITS)
#endif

#define TARGET_PAGE_ALIGN(addr) ROUND_UP((addr), TARGET_PAGE_SIZE)

#if defined(CONFIG_USER_ONLY)
void page_dump(FILE *f);

typedef int (*walk_memory_regions_fn)(void *, target_ulong,
                                      target_ulong, unsigned long);
int walk_memory_regions(void *, walk_memory_regions_fn);

int page_get_flags(target_ulong address);

/**
 * page_set_flags:
 * @start: first byte of range
 * @last: last byte of range
 * @flags: flags to set
 * Context: holding mmap lock
 *
 * Modify the flags of a page and invalidate the code if necessary.
 * The flag PAGE_WRITE_ORG is positioned automatically depending
 * on PAGE_WRITE.  The mmap_lock should already be held.
 */
void page_set_flags(target_ulong start, target_ulong last, int flags);

void page_reset_target_data(target_ulong start, target_ulong last);

/**
 * page_check_range
 * @start: first byte of range
 * @len: length of range
 * @flags: flags required for each page
 *
 * Return true if every page in [@start, @start+@len) has @flags set.
 * Return false if any page is unmapped.  Thus testing flags == 0 is
 * equivalent to testing for flags == PAGE_VALID.
 */
bool page_check_range(target_ulong start, target_ulong last, int flags);

/**
 * page_check_range_empty:
 * @start: first byte of range
 * @last: last byte of range
 * Context: holding mmap lock
 *
 * Return true if the entire range [@start, @last] is unmapped.
 * The memory lock must be held so that the caller will can ensure
 * the result stays true until a new mapping can be installed.
 */
bool page_check_range_empty(target_ulong start, target_ulong last);

/**
 * page_find_range_empty
 * @min: first byte of search range
 * @max: last byte of search range
 * @len: size of the hole required
 * @align: alignment of the hole required (power of 2)
 *
 * If there is a range [x, x+@len) within [@min, @max] such that
 * x % @align == 0, then return x.  Otherwise return -1.
 * The memory lock must be held, as the caller will want to ensure
 * the returned range stays empty until a new mapping can be installed.
 */
target_ulong page_find_range_empty(target_ulong min, target_ulong max,
                                   target_ulong len, target_ulong align);

/**
 * page_get_target_data(address)
 * @address: guest virtual address
 *
 * Return TARGET_PAGE_DATA_SIZE bytes of out-of-band data to associate
 * with the guest page at @address, allocating it if necessary.  The
 * caller should already have verified that the address is valid.
 *
 * The memory will be freed when the guest page is deallocated,
 * e.g. with the munmap system call.
 */
void *page_get_target_data(target_ulong address)
    __attribute__((returns_nonnull));
#endif

CPUArchState *cpu_copy(CPUArchState *env);

/* Flags for use in ENV->INTERRUPT_PENDING.

   The numbers assigned here are non-sequential in order to preserve
   binary compatibility with the vmstate dump.  Bit 0 (0x0001) was
   previously used for CPU_INTERRUPT_EXIT, and is cleared when loading
   the vmstate dump.  */

/* External hardware interrupt pending.  This is typically used for
   interrupts from devices.  */
#define CPU_INTERRUPT_HARD        0x0002

/* Exit the current TB.  This is typically used when some system-level device
   makes some change to the memory mapping.  E.g. the a20 line change.  */
#define CPU_INTERRUPT_EXITTB      0x0004

/* Halt the CPU.  */
#define CPU_INTERRUPT_HALT        0x0020

/* Debug event pending.  */
#define CPU_INTERRUPT_DEBUG       0x0080

/* Reset signal.  */
#define CPU_INTERRUPT_RESET       0x0400

/* Several target-specific external hardware interrupts.  Each target/cpu.h
   should define proper names based on these defines.  */
#define CPU_INTERRUPT_TGT_EXT_0   0x0008
#define CPU_INTERRUPT_TGT_EXT_1   0x0010
#define CPU_INTERRUPT_TGT_EXT_2   0x0040
#define CPU_INTERRUPT_TGT_EXT_3   0x0200
#define CPU_INTERRUPT_TGT_EXT_4   0x1000

/* Several target-specific internal interrupts.  These differ from the
   preceding target-specific interrupts in that they are intended to
   originate from within the cpu itself, typically in response to some
   instruction being executed.  These, therefore, are not masked while
   single-stepping within the debugger.  */
#define CPU_INTERRUPT_TGT_INT_0   0x0100
#define CPU_INTERRUPT_TGT_INT_1   0x0800
#define CPU_INTERRUPT_TGT_INT_2   0x2000

/* First unused bit: 0x4000.  */

/* The set of all bits that should be masked when single-stepping.  */
#define CPU_INTERRUPT_SSTEP_MASK \
    (CPU_INTERRUPT_HARD          \
     | CPU_INTERRUPT_TGT_EXT_0   \
     | CPU_INTERRUPT_TGT_EXT_1   \
     | CPU_INTERRUPT_TGT_EXT_2   \
     | CPU_INTERRUPT_TGT_EXT_3   \
     | CPU_INTERRUPT_TGT_EXT_4)

#ifdef CONFIG_USER_ONLY

/*
 * Allow some level of source compatibility with softmmu.  We do not
 * support any of the more exotic features, so only invalid pages may
 * be signaled by probe_access_flags().
 */
#define TLB_INVALID_MASK    (1 << (TARGET_PAGE_BITS_MIN - 1))
#define TLB_MMIO            (1 << (TARGET_PAGE_BITS_MIN - 2))
#define TLB_WATCHPOINT      0

static inline int cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return MMU_USER_IDX;
}
#else

/*
 * Flags stored in the low bits of the TLB virtual address.
 * These are defined so that fast path ram access is all zeros.
 * The flags all must be between TARGET_PAGE_BITS and
 * maximum address alignment bit.
 *
 * Use TARGET_PAGE_BITS_MIN so that these bits are constant
 * when TARGET_PAGE_BITS_VARY is in effect.
 *
 * The count, if not the placement of these bits is known
 * to tcg/tcg-op-ldst.c, check_max_alignment().
 */
/* Zero if TLB entry is valid.  */
#define TLB_INVALID_MASK    (1 << (TARGET_PAGE_BITS_MIN - 1))
/* Set if TLB entry references a clean RAM page.  The iotlb entry will
   contain the page physical address.  */
#define TLB_NOTDIRTY        (1 << (TARGET_PAGE_BITS_MIN - 2))
/* Set if TLB entry is an IO callback.  */
#define TLB_MMIO            (1 << (TARGET_PAGE_BITS_MIN - 3))
/* Set if TLB entry writes ignored.  */
#define TLB_DISCARD_WRITE   (1 << (TARGET_PAGE_BITS_MIN - 4))
/* Set if the slow path must be used; more flags in CPUTLBEntryFull. */
#define TLB_FORCE_SLOW      (1 << (TARGET_PAGE_BITS_MIN - 5))

/*
 * Use this mask to check interception with an alignment mask
 * in a TCG backend.
 */
#define TLB_FLAGS_MASK \
    (TLB_INVALID_MASK | TLB_NOTDIRTY | TLB_MMIO \
    | TLB_FORCE_SLOW | TLB_DISCARD_WRITE)

/*
 * Flags stored in CPUTLBEntryFull.slow_flags[x].
 * TLB_FORCE_SLOW must be set in CPUTLBEntry.addr_idx[x].
 */
/* Set if TLB entry requires byte swap.  */
#define TLB_BSWAP            (1 << 0)
/* Set if TLB entry contains a watchpoint.  */
#define TLB_WATCHPOINT       (1 << 1)
/* Set if TLB entry requires aligned accesses.  */
#define TLB_CHECK_ALIGNED    (1 << 2)

#define TLB_SLOW_FLAGS_MASK  (TLB_BSWAP | TLB_WATCHPOINT | TLB_CHECK_ALIGNED)

/* The two sets of flags must not overlap. */
QEMU_BUILD_BUG_ON(TLB_FLAGS_MASK & TLB_SLOW_FLAGS_MASK);

/**
 * tlb_hit_page: return true if page aligned @addr is a hit against the
 * TLB entry @tlb_addr
 *
 * @addr: virtual address to test (must be page aligned)
 * @tlb_addr: TLB entry address (a CPUTLBEntry addr_read/write/code value)
 */
static inline bool tlb_hit_page(uint64_t tlb_addr, vaddr addr)
{
    return addr == (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK));
}

/**
 * tlb_hit: return true if @addr is a hit against the TLB entry @tlb_addr
 *
 * @addr: virtual address to test (need not be page aligned)
 * @tlb_addr: TLB entry address (a CPUTLBEntry addr_read/write/code value)
 */
static inline bool tlb_hit(uint64_t tlb_addr, vaddr addr)
{
    return tlb_hit_page(tlb_addr, addr & TARGET_PAGE_MASK);
}

#endif /* !CONFIG_USER_ONLY */

/* Validate correct placement of CPUArchState. */
#include "cpu.h"
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

#endif /* CPU_ALL_H */
