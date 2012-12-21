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

#include "config.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

#include "exec/cputlb.h"

#include "exec/memory-internal.h"

//#define DEBUG_TLB
//#define DEBUG_TLB_CHECK

/* statistics */
int tlb_flush_count;

static const CPUTLBEntry s_cputlb_empty_entry = {
    .addr_read  = -1,
    .addr_write = -1,
    .addr_code  = -1,
    .addend     = -1,
};

/* NOTE:
 * If flush_global is true (the usual case), flush all tlb entries.
 * If flush_global is false, flush (at least) all tlb entries not
 * marked global.
 *
 * Since QEMU doesn't currently implement a global/not-global flag
 * for tlb entries, at the moment tlb_flush() will also flush all
 * tlb entries in the flush_global == false case. This is OK because
 * CPU architectures generally permit an implementation to drop
 * entries from the TLB at any time, so flushing more entries than
 * required is only an efficiency issue, not a correctness issue.
 */
void tlb_flush(CPUArchState *env, int flush_global)
{
    int i;

#if defined(DEBUG_TLB)
    printf("tlb_flush:\n");
#endif
    /* must reset current TB so that interrupts cannot modify the
       links while we are modifying them */
    env->current_tb = NULL;

    for (i = 0; i < CPU_TLB_SIZE; i++) {
        int mmu_idx;

        for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
            env->tlb_table[mmu_idx][i] = s_cputlb_empty_entry;
        }
    }

    memset(env->tb_jmp_cache, 0, TB_JMP_CACHE_SIZE * sizeof (void *));

    env->tlb_flush_addr = -1;
    env->tlb_flush_mask = 0;
    tlb_flush_count++;
}

static inline void tlb_flush_entry(CPUTLBEntry *tlb_entry, target_ulong addr)
{
    if (addr == (tlb_entry->addr_read &
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)) ||
        addr == (tlb_entry->addr_write &
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)) ||
        addr == (tlb_entry->addr_code &
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        *tlb_entry = s_cputlb_empty_entry;
    }
}

void tlb_flush_page(CPUArchState *env, target_ulong addr)
{
    int i;
    int mmu_idx;

#if defined(DEBUG_TLB)
    printf("tlb_flush_page: " TARGET_FMT_lx "\n", addr);
#endif
    /* Check if we need to flush due to large pages.  */
    if ((addr & env->tlb_flush_mask) == env->tlb_flush_addr) {
#if defined(DEBUG_TLB)
        printf("tlb_flush_page: forced full flush ("
               TARGET_FMT_lx "/" TARGET_FMT_lx ")\n",
               env->tlb_flush_addr, env->tlb_flush_mask);
#endif
        tlb_flush(env, 1);
        return;
    }
    /* must reset current TB so that interrupts cannot modify the
       links while we are modifying them */
    env->current_tb = NULL;

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_flush_entry(&env->tlb_table[mmu_idx][i], addr);
    }

    tb_flush_jmp_cache(env, addr);
}

/* update the TLBs so that writes to code in the virtual page 'addr'
   can be detected */
void tlb_protect_code(ram_addr_t ram_addr)
{
    cpu_physical_memory_reset_dirty(ram_addr,
                                    ram_addr + TARGET_PAGE_SIZE,
                                    CODE_DIRTY_FLAG);
}

/* update the TLB so that writes in physical page 'phys_addr' are no longer
   tested for self modifying code */
void tlb_unprotect_code_phys(CPUArchState *env, ram_addr_t ram_addr,
                             target_ulong vaddr)
{
    cpu_physical_memory_set_dirty_flags(ram_addr, CODE_DIRTY_FLAG);
}

static bool tlb_is_dirty_ram(CPUTLBEntry *tlbe)
{
    return (tlbe->addr_write & (TLB_INVALID_MASK|TLB_MMIO|TLB_NOTDIRTY)) == 0;
}

void tlb_reset_dirty_range(CPUTLBEntry *tlb_entry, uintptr_t start,
                           uintptr_t length)
{
    uintptr_t addr;

    if (tlb_is_dirty_ram(tlb_entry)) {
        addr = (tlb_entry->addr_write & TARGET_PAGE_MASK) + tlb_entry->addend;
        if ((addr - start) < length) {
            tlb_entry->addr_write |= TLB_NOTDIRTY;
        }
    }
}

static inline void tlb_update_dirty(CPUTLBEntry *tlb_entry)
{
    ram_addr_t ram_addr;
    void *p;

    if (tlb_is_dirty_ram(tlb_entry)) {
        p = (void *)(uintptr_t)((tlb_entry->addr_write & TARGET_PAGE_MASK)
            + tlb_entry->addend);
        ram_addr = qemu_ram_addr_from_host_nofail(p);
        if (!cpu_physical_memory_is_dirty(ram_addr)) {
            tlb_entry->addr_write |= TLB_NOTDIRTY;
        }
    }
}

void cpu_tlb_reset_dirty_all(ram_addr_t start1, ram_addr_t length)
{
    CPUArchState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        int mmu_idx;

        for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
            unsigned int i;

            for (i = 0; i < CPU_TLB_SIZE; i++) {
                tlb_reset_dirty_range(&env->tlb_table[mmu_idx][i],
                                      start1, length);
            }
        }
    }
}

static inline void tlb_set_dirty1(CPUTLBEntry *tlb_entry, target_ulong vaddr)
{
    if (tlb_entry->addr_write == (vaddr | TLB_NOTDIRTY)) {
        tlb_entry->addr_write = vaddr;
    }
}

/* update the TLB corresponding to virtual page vaddr
   so that it is no longer dirty */
void tlb_set_dirty(CPUArchState *env, target_ulong vaddr)
{
    int i;
    int mmu_idx;

    vaddr &= TARGET_PAGE_MASK;
    i = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_set_dirty1(&env->tlb_table[mmu_idx][i], vaddr);
    }
}

/* Our TLB does not support large pages, so remember the area covered by
   large pages and trigger a full TLB flush if these are invalidated.  */
static void tlb_add_large_page(CPUArchState *env, target_ulong vaddr,
                               target_ulong size)
{
    target_ulong mask = ~(size - 1);

    if (env->tlb_flush_addr == (target_ulong)-1) {
        env->tlb_flush_addr = vaddr & mask;
        env->tlb_flush_mask = mask;
        return;
    }
    /* Extend the existing region to include the new page.
       This is a compromise between unnecessary flushes and the cost
       of maintaining a full variable size TLB.  */
    mask &= env->tlb_flush_mask;
    while (((env->tlb_flush_addr ^ vaddr) & mask) != 0) {
        mask <<= 1;
    }
    env->tlb_flush_addr &= mask;
    env->tlb_flush_mask = mask;
}

/* Add a new TLB entry. At most one entry for a given virtual address
   is permitted. Only a single TARGET_PAGE_SIZE region is mapped, the
   supplied size is only used by tlb_flush_page.  */
void tlb_set_page(CPUArchState *env, target_ulong vaddr,
                  hwaddr paddr, int prot,
                  int mmu_idx, target_ulong size)
{
    MemoryRegionSection *section;
    unsigned int index;
    target_ulong address;
    target_ulong code_address;
    uintptr_t addend;
    CPUTLBEntry *te;
    hwaddr iotlb;

    assert(size >= TARGET_PAGE_SIZE);
    if (size != TARGET_PAGE_SIZE) {
        tlb_add_large_page(env, vaddr, size);
    }
    section = phys_page_find(address_space_memory.dispatch, paddr >> TARGET_PAGE_BITS);
#if defined(DEBUG_TLB)
    printf("tlb_set_page: vaddr=" TARGET_FMT_lx " paddr=0x" TARGET_FMT_plx
           " prot=%x idx=%d pd=0x%08lx\n",
           vaddr, paddr, prot, mmu_idx, pd);
#endif

    address = vaddr;
    if (!(memory_region_is_ram(section->mr) ||
          memory_region_is_romd(section->mr))) {
        /* IO memory case (romd handled later) */
        address |= TLB_MMIO;
    }
    if (memory_region_is_ram(section->mr) ||
        memory_region_is_romd(section->mr)) {
        addend = (uintptr_t)memory_region_get_ram_ptr(section->mr)
        + memory_region_section_addr(section, paddr);
    } else {
        addend = 0;
    }

    code_address = address;
    iotlb = memory_region_section_get_iotlb(env, section, vaddr, paddr, prot,
                                            &address);

    index = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    env->iotlb[mmu_idx][index] = iotlb - vaddr;
    te = &env->tlb_table[mmu_idx][index];
    te->addend = addend - vaddr;
    if (prot & PAGE_READ) {
        te->addr_read = address;
    } else {
        te->addr_read = -1;
    }

    if (prot & PAGE_EXEC) {
        te->addr_code = code_address;
    } else {
        te->addr_code = -1;
    }
    if (prot & PAGE_WRITE) {
        if ((memory_region_is_ram(section->mr) && section->readonly)
            || memory_region_is_romd(section->mr)) {
            /* Write access calls the I/O callback.  */
            te->addr_write = address | TLB_MMIO;
        } else if (memory_region_is_ram(section->mr)
                   && !cpu_physical_memory_is_dirty(
                           section->mr->ram_addr
                           + memory_region_section_addr(section, paddr))) {
            te->addr_write = address | TLB_NOTDIRTY;
        } else {
            te->addr_write = address;
        }
    } else {
        te->addr_write = -1;
    }
}

/* NOTE: this function can trigger an exception */
/* NOTE2: the returned address is not exactly the physical address: it
 * is actually a ram_addr_t (in system mode; the user mode emulation
 * version of this function returns a guest virtual address).
 */
tb_page_addr_t get_page_addr_code(CPUArchState *env1, target_ulong addr)
{
    int mmu_idx, page_index, pd;
    void *p;
    MemoryRegion *mr;

    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = cpu_mmu_index(env1);
    if (unlikely(env1->tlb_table[mmu_idx][page_index].addr_code !=
                 (addr & TARGET_PAGE_MASK))) {
        cpu_ldub_code(env1, addr);
    }
    pd = env1->iotlb[mmu_idx][page_index] & ~TARGET_PAGE_MASK;
    mr = iotlb_to_region(pd);
    if (memory_region_is_unassigned(mr)) {
#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_SPARC)
        cpu_unassigned_access(env1, addr, 0, 1, 0, 4);
#else
        cpu_abort(env1, "Trying to execute code outside RAM or ROM at 0x"
                  TARGET_FMT_lx "\n", addr);
#endif
    }
    p = (void *)((uintptr_t)addr + env1->tlb_table[mmu_idx][page_index].addend);
    return qemu_ram_addr_from_host_nofail(p);
}

#define MMUSUFFIX _cmmu
#undef GETPC
#define GETPC() ((uintptr_t)0)
#define SOFTMMU_CODE_ACCESS

#define SHIFT 0
#include "exec/softmmu_template.h"

#define SHIFT 1
#include "exec/softmmu_template.h"

#define SHIFT 2
#include "exec/softmmu_template.h"

#define SHIFT 3
#include "exec/softmmu_template.h"

#undef env
