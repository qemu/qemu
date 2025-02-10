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

#if !defined(CONFIG_USER_ONLY)

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
#include "exec/cpu-defs.h"
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

#include "cpu.h"

#ifdef CONFIG_USER_ONLY

static inline int cpu_mmu_index(CPUState *cs, bool ifetch);

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
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

#endif /* CPU_ALL_H */
