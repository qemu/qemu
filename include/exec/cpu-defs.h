/*
 * common defines for all CPUs
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#ifndef CPU_DEFS_H
#define CPU_DEFS_H

#ifndef NEED_CPU_H
#error cpu.h included from common code
#endif

#include "qemu/host-utils.h"
#include "qemu/thread.h"
#ifndef CONFIG_USER_ONLY
#include "exec/hwaddr.h"
#endif
#include "exec/memattrs.h"
#include "hw/core/cpu.h"

#include "cpu-param.h"

#ifndef TARGET_LONG_BITS
# error TARGET_LONG_BITS must be defined in cpu-param.h
#endif
#ifndef TARGET_PHYS_ADDR_SPACE_BITS
# error TARGET_PHYS_ADDR_SPACE_BITS must be defined in cpu-param.h
#endif
#ifndef TARGET_VIRT_ADDR_SPACE_BITS
# error TARGET_VIRT_ADDR_SPACE_BITS must be defined in cpu-param.h
#endif
#ifndef TARGET_PAGE_BITS
# ifdef TARGET_PAGE_BITS_VARY
#  ifndef TARGET_PAGE_BITS_MIN
#   error TARGET_PAGE_BITS_MIN must be defined in cpu-param.h
#  endif
# else
#  error TARGET_PAGE_BITS must be defined in cpu-param.h
# endif
#endif

#include "exec/target_long.h"

/*
 * Fix the number of mmu modes to 16, which is also the maximum
 * supported by the softmmu tlb api.
 */
#define NB_MMU_MODES 16

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_TCG)

/* use a fully associative victim tlb of 8 entries */
#define CPU_VTLB_SIZE 8

#if HOST_LONG_BITS == 32 && TARGET_LONG_BITS == 32
#define CPU_TLB_ENTRY_BITS 4
#else
#define CPU_TLB_ENTRY_BITS 5
#endif

#define CPU_TLB_DYN_MIN_BITS 6
#define CPU_TLB_DYN_DEFAULT_BITS 8

# if HOST_LONG_BITS == 32
/* Make sure we do not require a double-word shift for the TLB load */
#  define CPU_TLB_DYN_MAX_BITS (32 - TARGET_PAGE_BITS)
# else /* HOST_LONG_BITS == 64 */
/*
 * Assuming TARGET_PAGE_BITS==12, with 2**22 entries we can cover 2**(22+12) ==
 * 2**34 == 16G of address space. This is roughly what one would expect a
 * TLB to cover in a modern (as of 2018) x86_64 CPU. For instance, Intel
 * Skylake's Level-2 STLB has 16 1G entries.
 * Also, make sure we do not size the TLB past the guest's address space.
 */
#  ifdef TARGET_PAGE_BITS_VARY
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  else
#   define CPU_TLB_DYN_MAX_BITS                                  \
    MIN_CONST(22, TARGET_VIRT_ADDR_SPACE_BITS - TARGET_PAGE_BITS)
#  endif
# endif

/* Minimalized TLB entry for use by TCG fast path. */
typedef struct CPUTLBEntry {
    /* bit TARGET_LONG_BITS to TARGET_PAGE_BITS : virtual address
       bit TARGET_PAGE_BITS-1..4  : Nonzero for accesses that should not
                                    go directly to ram.
       bit 3                      : indicates that the entry is invalid
       bit 2..0                   : zero
    */
    union {
        struct {
            target_ulong addr_read;
            target_ulong addr_write;
            target_ulong addr_code;
            /* Addend to virtual address to get host address.  IO accesses
               use the corresponding iotlb value.  */
            uintptr_t addend;
        };
        /*
         * Padding to get a power of two size, as well as index
         * access to addr_{read,write,code}.
         */
        target_ulong addr_idx[(1 << CPU_TLB_ENTRY_BITS) / TARGET_LONG_SIZE];
    };
} CPUTLBEntry;

QEMU_BUILD_BUG_ON(sizeof(CPUTLBEntry) != (1 << CPU_TLB_ENTRY_BITS));


#endif  /* !CONFIG_USER_ONLY && CONFIG_TCG */

#if !defined(CONFIG_USER_ONLY)
/*
 * The full TLB entry, which is not accessed by generated TCG code,
 * so the layout is not as critical as that of CPUTLBEntry. This is
 * also why we don't want to combine the two structs.
 */
typedef struct CPUTLBEntryFull {
    /*
     * @xlat_section contains:
     *  - in the lower TARGET_PAGE_BITS, a physical section number
     *  - with the lower TARGET_PAGE_BITS masked off, an offset which
     *    must be added to the virtual address to obtain:
     *     + the ram_addr_t of the target RAM (if the physical section
     *       number is PHYS_SECTION_NOTDIRTY or PHYS_SECTION_ROM)
     *     + the offset within the target MemoryRegion (otherwise)
     */
    hwaddr xlat_section;

    /*
     * @phys_addr contains the physical address in the address space
     * given by cpu_asidx_from_attrs(cpu, @attrs).
     */
    hwaddr phys_addr;

    /* @attrs contains the memory transaction attributes for the page. */
    MemTxAttrs attrs;

    /* @prot contains the complete protections for the page. */
    uint8_t prot;

    /* @lg_page_size contains the log2 of the page size. */
    uint8_t lg_page_size;

    /*
     * Allow target-specific additions to this structure.
     * This may be used to cache items from the guest cpu
     * page tables for later use by the implementation.
     */
#ifdef TARGET_PAGE_ENTRY_EXTRA
    TARGET_PAGE_ENTRY_EXTRA
#endif
} CPUTLBEntryFull;
#endif  /* !CONFIG_USER_ONLY */

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_TCG)
/*
 * Data elements that are per MMU mode, minus the bits accessed by
 * the TCG fast path.
 */
typedef struct CPUTLBDesc {
    /*
     * Describe a region covering all of the large pages allocated
     * into the tlb.  When any page within this region is flushed,
     * we must flush the entire tlb.  The region is matched if
     * (addr & large_page_mask) == large_page_addr.
     */
    target_ulong large_page_addr;
    target_ulong large_page_mask;
    /* host time (in ns) at the beginning of the time window */
    int64_t window_begin_ns;
    /* maximum number of entries observed in the window */
    size_t window_max_entries;
    size_t n_used_entries;
    /* The next index to use in the tlb victim table.  */
    size_t vindex;
    /* The tlb victim table, in two parts.  */
    CPUTLBEntry vtable[CPU_VTLB_SIZE];
    CPUTLBEntryFull vfulltlb[CPU_VTLB_SIZE];
    CPUTLBEntryFull *fulltlb;
} CPUTLBDesc;

/*
 * Data elements that are per MMU mode, accessed by the fast path.
 * The structure is aligned to aid loading the pair with one insn.
 */
typedef struct CPUTLBDescFast {
    /* Contains (n_entries - 1) << CPU_TLB_ENTRY_BITS */
    uintptr_t mask;
    /* The array of tlb entries itself. */
    CPUTLBEntry *table;
} CPUTLBDescFast QEMU_ALIGNED(2 * sizeof(void *));

/*
 * Data elements that are shared between all MMU modes.
 */
typedef struct CPUTLBCommon {
    /* Serialize updates to f.table and d.vtable, and others as noted. */
    QemuSpin lock;
    /*
     * Within dirty, for each bit N, modifications have been made to
     * mmu_idx N since the last time that mmu_idx was flushed.
     * Protected by tlb_c.lock.
     */
    uint16_t dirty;
    /*
     * Statistics.  These are not lock protected, but are read and
     * written atomically.  This allows the monitor to print a snapshot
     * of the stats without interfering with the cpu.
     */
    size_t full_flush_count;
    size_t part_flush_count;
    size_t elide_flush_count;
} CPUTLBCommon;

/*
 * The entire softmmu tlb, for all MMU modes.
 * The meaning of each of the MMU modes is defined in the target code.
 * Since this is placed within CPUNegativeOffsetState, the smallest
 * negative offsets are at the end of the struct.
 */

typedef struct CPUTLB {
    CPUTLBCommon c;
    CPUTLBDesc d[NB_MMU_MODES];
    CPUTLBDescFast f[NB_MMU_MODES];
} CPUTLB;

/* This will be used by TCG backends to compute offsets.  */
#define TLB_MASK_TABLE_OFS(IDX) \
    ((int)offsetof(ArchCPU, neg.tlb.f[IDX]) - (int)offsetof(ArchCPU, env))

#else

typedef struct CPUTLB { } CPUTLB;

#endif  /* !CONFIG_USER_ONLY && CONFIG_TCG */

/*
 * This structure must be placed in ArchCPU immediately
 * before CPUArchState, as a field named "neg".
 */
typedef struct CPUNegativeOffsetState {
    CPUTLB tlb;
    IcountDecr icount_decr;
} CPUNegativeOffsetState;

#endif
