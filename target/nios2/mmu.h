/*
 * Altera Nios II MMU emulation for qemu.
 *
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef NIOS2_MMU_H
#define NIOS2_MMU_H

typedef struct Nios2TLBEntry {
    target_ulong tag;
    target_ulong data;
} Nios2TLBEntry;

typedef struct Nios2MMU {
    int tlb_entry_mask;
    uint32_t pteaddr_wr;
    uint32_t tlbacc_wr;
    uint32_t tlbmisc_wr;
    Nios2TLBEntry *tlb;
} Nios2MMU;

typedef struct Nios2MMULookup {
    target_ulong vaddr;
    target_ulong paddr;
    int prot;
} Nios2MMULookup;

void mmu_flip_um(CPUNios2State *env, unsigned int um);
unsigned int mmu_translate(CPUNios2State *env,
                           Nios2MMULookup *lu,
                           target_ulong vaddr, int rw, int mmu_idx);
void mmu_read_debug(CPUNios2State *env, uint32_t rn);
void mmu_write(CPUNios2State *env, uint32_t rn, uint32_t v);
void mmu_init(CPUNios2State *env);

#endif /* NIOS2_MMU_H */
