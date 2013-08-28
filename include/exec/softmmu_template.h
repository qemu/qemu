/*
 *  Software MMU support
 *
 * Generate helpers used by TCG for qemu_ld/st ops and code load
 * functions.
 *
 * Included from target op helpers and exec.c.
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
#include "qemu/timer.h"
#include "exec/memory.h"

#define DATA_SIZE (1 << SHIFT)

#if DATA_SIZE == 8
#define SUFFIX q
#define USUFFIX q
#define DATA_TYPE uint64_t
#elif DATA_SIZE == 4
#define SUFFIX l
#define USUFFIX l
#define DATA_TYPE uint32_t
#elif DATA_SIZE == 2
#define SUFFIX w
#define USUFFIX uw
#define DATA_TYPE uint16_t
#elif DATA_SIZE == 1
#define SUFFIX b
#define USUFFIX ub
#define DATA_TYPE uint8_t
#else
#error unsupported data size
#endif

#ifdef SOFTMMU_CODE_ACCESS
#define READ_ACCESS_TYPE 2
#define ADDR_READ addr_code
#else
#define READ_ACCESS_TYPE 0
#define ADDR_READ addr_read
#endif

static inline DATA_TYPE glue(io_read, SUFFIX)(CPUArchState *env,
                                              hwaddr physaddr,
                                              target_ulong addr,
                                              uintptr_t retaddr)
{
    uint64_t val;
    MemoryRegion *mr = iotlb_to_region(physaddr);

    physaddr = (physaddr & TARGET_PAGE_MASK) + addr;
    env->mem_io_pc = retaddr;
    if (mr != &io_mem_rom && mr != &io_mem_notdirty && !can_do_io(env)) {
        cpu_io_recompile(env, retaddr);
    }

    env->mem_io_vaddr = addr;
    io_mem_read(mr, physaddr, &val, 1 << SHIFT);
    return val;
}

/* handle all cases except unaligned access which span two pages */
#ifdef SOFTMMU_CODE_ACCESS
static
#endif
DATA_TYPE
glue(glue(helper_ret_ld, SUFFIX), MMUSUFFIX)(CPUArchState *env,
                                             target_ulong addr, int mmu_idx,
                                             uintptr_t retaddr)
{
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].ADDR_READ;
    uintptr_t haddr;

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
         != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
#ifdef ALIGNED_ONLY
        if ((addr & (DATA_SIZE - 1)) != 0) {
            do_unaligned_access(env, addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
        }
#endif
        tlb_fill(env, addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
        tlb_addr = env->tlb_table[mmu_idx][index].ADDR_READ;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        hwaddr ioaddr;
        if ((addr & (DATA_SIZE - 1)) != 0) {
            goto do_unaligned_access;
        }
        ioaddr = env->iotlb[mmu_idx][index];
        return glue(io_read, SUFFIX)(env, ioaddr, addr, retaddr);
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (DATA_SIZE > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + DATA_SIZE - 1
                    >= TARGET_PAGE_SIZE)) {
        target_ulong addr1, addr2;
        DATA_TYPE res1, res2, res;
        unsigned shift;
    do_unaligned_access:
#ifdef ALIGNED_ONLY
        do_unaligned_access(env, addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
#endif
        addr1 = addr & ~(DATA_SIZE - 1);
        addr2 = addr1 + DATA_SIZE;
        res1 = glue(glue(helper_ret_ld, SUFFIX), MMUSUFFIX)(env, addr1,
                                                            mmu_idx, retaddr);
        res2 = glue(glue(helper_ret_ld, SUFFIX), MMUSUFFIX)(env, addr2,
                                                            mmu_idx, retaddr);
        shift = (addr & (DATA_SIZE - 1)) * 8;
#ifdef TARGET_WORDS_BIGENDIAN
        res = (res1 << shift) | (res2 >> ((DATA_SIZE * 8) - shift));
#else
        res = (res1 >> shift) | (res2 << ((DATA_SIZE * 8) - shift));
#endif
        return res;
    }

    /* Handle aligned access or unaligned access in the same page.  */
#ifdef ALIGNED_ONLY
    if ((addr & (DATA_SIZE - 1)) != 0) {
        do_unaligned_access(env, addr, READ_ACCESS_TYPE, mmu_idx, retaddr);
    }
#endif

    haddr = addr + env->tlb_table[mmu_idx][index].addend;
    return glue(glue(ld, USUFFIX), _raw)((uint8_t *)haddr);
}

DATA_TYPE
glue(glue(helper_ld, SUFFIX), MMUSUFFIX)(CPUArchState *env, target_ulong addr,
                                         int mmu_idx)
{
    return glue(glue(helper_ret_ld, SUFFIX), MMUSUFFIX)(env, addr, mmu_idx,
                                                        GETPC_EXT());
}

#ifndef SOFTMMU_CODE_ACCESS

static inline void glue(io_write, SUFFIX)(CPUArchState *env,
                                          hwaddr physaddr,
                                          DATA_TYPE val,
                                          target_ulong addr,
                                          uintptr_t retaddr)
{
    MemoryRegion *mr = iotlb_to_region(physaddr);

    physaddr = (physaddr & TARGET_PAGE_MASK) + addr;
    if (mr != &io_mem_rom && mr != &io_mem_notdirty && !can_do_io(env)) {
        cpu_io_recompile(env, retaddr);
    }

    env->mem_io_vaddr = addr;
    env->mem_io_pc = retaddr;
    io_mem_write(mr, physaddr, val, 1 << SHIFT);
}

void
glue(glue(helper_ret_st, SUFFIX), MMUSUFFIX)(CPUArchState *env,
                                             target_ulong addr, DATA_TYPE val,
                                             int mmu_idx, uintptr_t retaddr)
{
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    target_ulong tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    uintptr_t haddr;

    /* If the TLB entry is for a different page, reload and try again.  */
    if ((addr & TARGET_PAGE_MASK)
        != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
#ifdef ALIGNED_ONLY
        if ((addr & (DATA_SIZE - 1)) != 0) {
            do_unaligned_access(env, addr, 1, mmu_idx, retaddr);
        }
#endif
        tlb_fill(env, addr, 1, mmu_idx, retaddr);
        tlb_addr = env->tlb_table[mmu_idx][index].addr_write;
    }

    /* Handle an IO access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        hwaddr ioaddr;
        if ((addr & (DATA_SIZE - 1)) != 0) {
            goto do_unaligned_access;
        }
        ioaddr = env->iotlb[mmu_idx][index];
        glue(io_write, SUFFIX)(env, ioaddr, val, addr, retaddr);
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (DATA_SIZE > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + DATA_SIZE - 1
                     >= TARGET_PAGE_SIZE)) {
        int i;
    do_unaligned_access:
#ifdef ALIGNED_ONLY
        do_unaligned_access(env, addr, 1, mmu_idx, retaddr);
#endif
        /* XXX: not efficient, but simple */
        /* Note: relies on the fact that tlb_fill() does not remove the
         * previous page from the TLB cache.  */
        for (i = DATA_SIZE - 1; i >= 0; i--) {
#ifdef TARGET_WORDS_BIGENDIAN
            uint8_t val8 = val >> (((DATA_SIZE - 1) * 8) - (i * 8));
#else
            uint8_t val8 = val >> (i * 8);
#endif
            glue(helper_ret_stb, MMUSUFFIX)(env, addr + i, val8,
                                            mmu_idx, retaddr);
        }
        return;
    }

    /* Handle aligned access or unaligned access in the same page.  */
#ifdef ALIGNED_ONLY
    if ((addr & (DATA_SIZE - 1)) != 0) {
        do_unaligned_access(env, addr, 1, mmu_idx, retaddr);
    }
#endif

    haddr = addr + env->tlb_table[mmu_idx][index].addend;
    glue(glue(st, SUFFIX), _raw)((uint8_t *)haddr, val);
}

void
glue(glue(helper_st, SUFFIX), MMUSUFFIX)(CPUArchState *env, target_ulong addr,
                                         DATA_TYPE val, int mmu_idx)
{
    glue(glue(helper_ret_st, SUFFIX), MMUSUFFIX)(env, addr, val, mmu_idx,
                                                 GETPC_EXT());
}

#endif /* !defined(SOFTMMU_CODE_ACCESS) */

#undef READ_ACCESS_TYPE
#undef SHIFT
#undef DATA_TYPE
#undef SUFFIX
#undef USUFFIX
#undef DATA_SIZE
#undef ADDR_READ
