/*
 *  Microblaze MMU emulation for qemu.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias
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

#ifndef TARGET_MICROBLAZE_MMU_H
#define TARGET_MICROBLAZE_MMU_H

#include "cpu.h"

#define MMU_R_PID    0
#define MMU_R_ZPR    1
#define MMU_R_TLBX   2
#define MMU_R_TLBLO  3
#define MMU_R_TLBHI  4
#define MMU_R_TLBSX  5

#define RAM_DATA     1
#define RAM_TAG      0

/* Tag portion */
#define TLB_EPN_MASK          MAKE_64BIT_MASK(10, 64 - 10)
#define TLB_PAGESZ_MASK       0x00000380
#define TLB_PAGESZ(x)         (((x) & 0x7) << 7)
#define PAGESZ_1K             0
#define PAGESZ_4K             1
#define PAGESZ_16K            2
#define PAGESZ_64K            3
#define PAGESZ_256K           4
#define PAGESZ_1M             5
#define PAGESZ_4M             6
#define PAGESZ_16M            7
#define TLB_VALID             0x00000040 /* Entry is valid */

/* Data portion */
#define TLB_RPN_MASK          MAKE_64BIT_MASK(10, 64 - 10)
#define TLB_PERM_MASK         0x00000300
#define TLB_EX                0x00000200 /* Instruction execution allowed */
#define TLB_WR                0x00000100 /* Writes permitted */
#define TLB_ZSEL_MASK         0x000000F0
#define TLB_ZSEL(x)           (((x) & 0xF) << 4)
#define TLB_ATTR_MASK         0x0000000F
#define TLB_W                 0x00000008 /* Caching is write-through */
#define TLB_I                 0x00000004 /* Caching is inhibited */
#define TLB_M                 0x00000002 /* Memory is coherent */
#define TLB_G                 0x00000001 /* Memory is guarded from prefetch */

/* TLBX  */
#define R_TBLX_MISS_SHIFT 31
#define R_TBLX_MISS_MASK (1U << R_TBLX_MISS_SHIFT)

#define TLB_ENTRIES    64

typedef struct {
    /* Data and tag brams.  */
    uint64_t rams[2][TLB_ENTRIES];
    /* We keep a separate ram for the tids to avoid the 48 bit tag width.  */
    uint8_t tids[TLB_ENTRIES];
    /* Control flops.  */
    uint32_t regs[3];
} MicroBlazeMMU;

typedef struct {
    uint32_t paddr;
    uint32_t vaddr;
    unsigned int size;
    unsigned int idx;
    int prot;
    enum {
        ERR_PROT, ERR_MISS, ERR_HIT
    } err;
} MicroBlazeMMULookup;

unsigned int mmu_translate(MicroBlazeCPU *cpu, MicroBlazeMMULookup *lu,
                           vaddr vaddr, MMUAccessType rw, int mmu_idx);
uint32_t mmu_read(CPUMBState *env, bool ea, uint32_t rn);
void mmu_write(CPUMBState *env, bool ea, uint32_t rn, uint32_t v);
void mmu_init(MicroBlazeMMU *mmu);

#endif
