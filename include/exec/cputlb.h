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
#include "exec/vaddr.h"

#ifdef CONFIG_TCG

#if !defined(CONFIG_USER_ONLY)
/* cputlb.c */
void tlb_protect_code(ram_addr_t ram_addr);
void tlb_unprotect_code(ram_addr_t ram_addr);
#endif

#endif /* CONFIG_TCG */

#ifndef CONFIG_USER_ONLY

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length);
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

#endif
