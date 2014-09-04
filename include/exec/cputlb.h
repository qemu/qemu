/*
 *  Common CPU TLB handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 */
#ifndef CPUTLB_H
#define CPUTLB_H

#if !defined(CONFIG_USER_ONLY)
/* cputlb.c */
void tlb_protect_code(ram_addr_t ram_addr);
void tlb_unprotect_code_phys(CPUState *cpu, ram_addr_t ram_addr,
                             target_ulong vaddr);
void tlb_reset_dirty_range(CPUTLBEntry *tlb_entry, uintptr_t start,
                           uintptr_t length);
void cpu_tlb_reset_dirty_all(ram_addr_t start1, ram_addr_t length);
void tlb_set_dirty(CPUArchState *env, target_ulong vaddr);
extern int tlb_flush_count;

/* exec.c */
void tb_flush_jmp_cache(CPUState *cpu, target_ulong addr);

MemoryRegionSection *
address_space_translate_for_iotlb(AddressSpace *as, hwaddr addr, hwaddr *xlat,
                                  hwaddr *plen);
hwaddr memory_region_section_get_iotlb(CPUState *cpu,
                                       MemoryRegionSection *section,
                                       target_ulong vaddr,
                                       hwaddr paddr, hwaddr xlat,
                                       int prot,
                                       target_ulong *address);
bool memory_region_is_unassigned(MemoryRegion *mr);

#endif
#endif
