/*
 *  Common CPU TLB handling
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

#ifndef CPUTLB_H
#define CPUTLB_H

#include "exec/cpu-common.h"
#include "exec/hwaddr.h"
#include "exec/memattrs.h"
#include "exec/vaddr.h"

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
void tlb_protect_code(ram_addr_t ram_addr);
void tlb_unprotect_code(ram_addr_t ram_addr);
#endif

#ifndef CONFIG_USER_ONLY
void tlb_reset_dirty(CPUState *cpu, uintptr_t start, uintptr_t length);
void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t length);
#endif

/**
 * tlb_set_page_full:
 * @cpu: CPU context
 * @mmu_idx: mmu index of the tlb to modify
 * @addr: virtual address of the entry to add
 * @full: the details of the tlb entry
 *
 * Add an entry to @cpu tlb index @mmu_idx.  All of the fields of
 * @full must be filled, except for xlat_section, and constitute
 * the complete description of the translated page.
 *
 * This is generally called by the target tlb_fill function after
 * having performed a successful page table walk to find the physical
 * address and attributes for the translation.
 *
 * At most one entry for a given virtual address is permitted. Only a
 * single TARGET_PAGE_SIZE region is mapped; @full->lg_page_size is only
 * used by tlb_flush_page.
 */
void tlb_set_page_full(CPUState *cpu, int mmu_idx, vaddr addr,
                       CPUTLBEntryFull *full);

/**
 * tlb_set_page_with_attrs:
 * @cpu: CPU to add this TLB entry for
 * @addr: virtual address of page to add entry for
 * @paddr: physical address of the page
 * @attrs: memory transaction attributes
 * @prot: access permissions (PAGE_READ/PAGE_WRITE/PAGE_EXEC bits)
 * @mmu_idx: MMU index to insert TLB entry for
 * @size: size of the page in bytes
 *
 * Add an entry to this CPU's TLB (a mapping from virtual address
 * @addr to physical address @paddr) with the specified memory
 * transaction attributes. This is generally called by the target CPU
 * specific code after it has been called through the tlb_fill()
 * entry point and performed a successful page table walk to find
 * the physical address and attributes for the virtual address
 * which provoked the TLB miss.
 *
 * At most one entry for a given virtual address is permitted. Only a
 * single TARGET_PAGE_SIZE region is mapped; the supplied @size is only
 * used by tlb_flush_page.
 */
void tlb_set_page_with_attrs(CPUState *cpu, vaddr addr,
                             hwaddr paddr, MemTxAttrs attrs,
                             int prot, int mmu_idx, vaddr size);

/**
 * tlb_set_page:
 *
 * This function is equivalent to calling tlb_set_page_with_attrs()
 * with an @attrs argument of MEMTXATTRS_UNSPECIFIED. It's provided
 * as a convenience for CPUs which don't use memory transaction attributes.
 */
void tlb_set_page(CPUState *cpu, vaddr addr,
                  hwaddr paddr, int prot,
                  int mmu_idx, vaddr size);

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
/**
 * tlb_flush_page:
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of page to be flushed
 *
 * Flush one page from the TLB of the specified CPU, for all
 * MMU indexes.
 */
void tlb_flush_page(CPUState *cpu, vaddr addr);

/**
 * tlb_flush_page_all_cpus_synced:
 * @cpu: src CPU of the flush
 * @addr: virtual address of page to be flushed
 *
 * Flush one page from the TLB of all CPUs, for all
 * MMU indexes.
 *
 * When this function returns, no CPUs will subsequently perform
 * translations using the flushed TLBs.
 */
void tlb_flush_page_all_cpus_synced(CPUState *src, vaddr addr);

/**
 * tlb_flush:
 * @cpu: CPU whose TLB should be flushed
 *
 * Flush the entire TLB for the specified CPU. Most CPU architectures
 * allow the implementation to drop entries from the TLB at any time
 * so this is generally safe. If more selective flushing is required
 * use one of the other functions for efficiency.
 */
void tlb_flush(CPUState *cpu);

/**
 * tlb_flush_all_cpus_synced:
 * @cpu: src CPU of the flush
 *
 * Flush the entire TLB for all CPUs, for all MMU indexes.
 *
 * When this function returns, no CPUs will subsequently perform
 * translations using the flushed TLBs.
 */
void tlb_flush_all_cpus_synced(CPUState *src_cpu);

/**
 * tlb_flush_page_by_mmuidx:
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush one page from the TLB of the specified CPU, for the specified
 * MMU indexes.
 */
void tlb_flush_page_by_mmuidx(CPUState *cpu, vaddr addr,
                              MMUIdxMap idxmap);

/**
 * tlb_flush_page_by_mmuidx_all_cpus_synced:
 * @cpu: Originating CPU of the flush
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush one page from the TLB of all CPUs, for the specified
 * MMU indexes.
 *
 * When this function returns, no CPUs will subsequently perform
 * translations using the flushed TLBs.
 */
void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                              MMUIdxMap idxmap);

/**
 * tlb_flush_by_mmuidx:
 * @cpu: CPU whose TLB should be flushed
 * @wait: If true ensure synchronisation by exiting the cpu_loop
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush all entries from the TLB of the specified CPU, for the specified
 * MMU indexes.
 */
void tlb_flush_by_mmuidx(CPUState *cpu, MMUIdxMap idxmap);

/**
 * tlb_flush_by_mmuidx_all_cpus_synced:
 * @cpu: Originating CPU of the flush
 * @idxmap: bitmap of MMU indexes to flush
 *
 * Flush all entries from the TLB of all CPUs, for the specified
 * MMU indexes.
 *
 * When this function returns, no CPUs will subsequently perform
 * translations using the flushed TLBs.
 */
void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *cpu, MMUIdxMap idxmap);

/**
 * tlb_flush_page_bits_by_mmuidx
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of page to be flushed
 * @idxmap: bitmap of mmu indexes to flush
 * @bits: number of significant bits in address
 *
 * Similar to tlb_flush_page_mask, but with a bitmap of indexes.
 */
void tlb_flush_page_bits_by_mmuidx(CPUState *cpu, vaddr addr,
                                   MMUIdxMap idxmap, unsigned bits);

/* Similarly, with broadcast and syncing. */
void tlb_flush_page_bits_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                                   MMUIdxMap idxmap,
                                                   unsigned bits);

/**
 * tlb_flush_range_by_mmuidx
 * @cpu: CPU whose TLB should be flushed
 * @addr: virtual address of the start of the range to be flushed
 * @len: length of range to be flushed
 * @idxmap: bitmap of mmu indexes to flush
 * @bits: number of significant bits in address
 *
 * For each mmuidx in @idxmap, flush all pages within [@addr,@addr+@len),
 * comparing only the low @bits worth of each virtual page.
 */
void tlb_flush_range_by_mmuidx(CPUState *cpu, vaddr addr,
                               vaddr len, MMUIdxMap idxmap,
                               unsigned bits);

/* Similarly, with broadcast and syncing. */
void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                               vaddr addr,
                                               vaddr len,
                                               MMUIdxMap idxmap,
                                               unsigned bits);
#else
static inline void tlb_flush_page(CPUState *cpu, vaddr addr)
{
}
static inline void tlb_flush_page_all_cpus_synced(CPUState *src, vaddr addr)
{
}
static inline void tlb_flush(CPUState *cpu)
{
}
static inline void tlb_flush_all_cpus_synced(CPUState *src_cpu)
{
}
static inline void tlb_flush_page_by_mmuidx(CPUState *cpu,
                                            vaddr addr, MMUIdxMap idxmap)
{
}

static inline void tlb_flush_by_mmuidx(CPUState *cpu, MMUIdxMap idxmap)
{
}
static inline void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                            vaddr addr,
                                                            MMUIdxMap idxmap)
{
}
static inline void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                       MMUIdxMap idxmap)
{
}
static inline void tlb_flush_page_bits_by_mmuidx(CPUState *cpu,
                                                 vaddr addr,
                                                 MMUIdxMap idxmap,
                                                 unsigned bits)
{
}
static inline void
tlb_flush_page_bits_by_mmuidx_all_cpus_synced(CPUState *cpu, vaddr addr,
                                              MMUIdxMap idxmap, unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx(CPUState *cpu, vaddr addr,
                                             vaddr len, MMUIdxMap idxmap,
                                             unsigned bits)
{
}
static inline void tlb_flush_range_by_mmuidx_all_cpus_synced(CPUState *cpu,
                                                             vaddr addr,
                                                             vaddr len,
                                                             MMUIdxMap idxmap,
                                                             unsigned bits)
{
}
#endif /* CONFIG_TCG && !CONFIG_USER_ONLY */
#endif /* CPUTLB_H */
