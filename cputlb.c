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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "exec/cpu_ldst.h"

#include "exec/cputlb.h"

#include "exec/memory-internal.h"
#include "exec/ram_addr.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"

/* DEBUG defines, enable DEBUG_TLB_LOG to log to the CPU_LOG_MMU target */
/* #define DEBUG_TLB */
/* #define DEBUG_TLB_LOG */

#ifdef DEBUG_TLB
# define DEBUG_TLB_GATE 1
# ifdef DEBUG_TLB_LOG
#  define DEBUG_TLB_LOG_GATE 1
# else
#  define DEBUG_TLB_LOG_GATE 0
# endif
#else
# define DEBUG_TLB_GATE 0
# define DEBUG_TLB_LOG_GATE 0
#endif

#define tlb_debug(fmt, ...) do { \
    if (DEBUG_TLB_LOG_GATE) { \
        qemu_log_mask(CPU_LOG_MMU, "%s: " fmt, __func__, \
                      ## __VA_ARGS__); \
    } else if (DEBUG_TLB_GATE) { \
        fprintf(stderr, "%s: " fmt, __func__, ## __VA_ARGS__); \
    } \
} while (0)

/* statistics */
int tlb_flush_count;

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
void tlb_flush(CPUState *cpu, int flush_global)
{
    CPUArchState *env = cpu->env_ptr;

    tlb_debug("(%d)\n", flush_global);

    memset(env->tlb_table, -1, sizeof(env->tlb_table));
    memset(env->tlb_v_table, -1, sizeof(env->tlb_v_table));
    memset(cpu->tb_jmp_cache, 0, sizeof(cpu->tb_jmp_cache));

    env->vtlb_index = 0;
    env->tlb_flush_addr = -1;
    env->tlb_flush_mask = 0;
    tlb_flush_count++;
}

static inline void v_tlb_flush_by_mmuidx(CPUState *cpu, va_list argp)
{
    CPUArchState *env = cpu->env_ptr;

    tlb_debug("start\n");

    for (;;) {
        int mmu_idx = va_arg(argp, int);

        if (mmu_idx < 0) {
            break;
        }

        tlb_debug("%d\n", mmu_idx);

        memset(env->tlb_table[mmu_idx], -1, sizeof(env->tlb_table[0]));
        memset(env->tlb_v_table[mmu_idx], -1, sizeof(env->tlb_v_table[0]));
    }

    memset(cpu->tb_jmp_cache, 0, sizeof(cpu->tb_jmp_cache));
}

void tlb_flush_by_mmuidx(CPUState *cpu, ...)
{
    va_list argp;
    va_start(argp, cpu);
    v_tlb_flush_by_mmuidx(cpu, argp);
    va_end(argp);
}

static inline void tlb_flush_entry(CPUTLBEntry *tlb_entry, target_ulong addr)
{
    if (addr == (tlb_entry->addr_read &
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)) ||
        addr == (tlb_entry->addr_write &
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK)) ||
        addr == (tlb_entry->addr_code &
                 (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        memset(tlb_entry, -1, sizeof(*tlb_entry));
    }
}

void tlb_flush_page(CPUState *cpu, target_ulong addr)
{
    CPUArchState *env = cpu->env_ptr;
    int i;
    int mmu_idx;

    tlb_debug("page :" TARGET_FMT_lx "\n", addr);

    /* Check if we need to flush due to large pages.  */
    if ((addr & env->tlb_flush_mask) == env->tlb_flush_addr) {
        tlb_debug("forcing full flush ("
                  TARGET_FMT_lx "/" TARGET_FMT_lx ")\n",
                  env->tlb_flush_addr, env->tlb_flush_mask);

        tlb_flush(cpu, 1);
        return;
    }

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_flush_entry(&env->tlb_table[mmu_idx][i], addr);
    }

    /* check whether there are entries that need to be flushed in the vtlb */
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        int k;
        for (k = 0; k < CPU_VTLB_SIZE; k++) {
            tlb_flush_entry(&env->tlb_v_table[mmu_idx][k], addr);
        }
    }

    tb_flush_jmp_cache(cpu, addr);
}

void tlb_flush_page_by_mmuidx(CPUState *cpu, target_ulong addr, ...)
{
    CPUArchState *env = cpu->env_ptr;
    int i, k;
    va_list argp;

    va_start(argp, addr);

    tlb_debug("addr "TARGET_FMT_lx"\n", addr);

    /* Check if we need to flush due to large pages.  */
    if ((addr & env->tlb_flush_mask) == env->tlb_flush_addr) {
        tlb_debug("forced full flush ("
                  TARGET_FMT_lx "/" TARGET_FMT_lx ")\n",
                  env->tlb_flush_addr, env->tlb_flush_mask);

        v_tlb_flush_by_mmuidx(cpu, argp);
        va_end(argp);
        return;
    }

    addr &= TARGET_PAGE_MASK;
    i = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);

    for (;;) {
        int mmu_idx = va_arg(argp, int);

        if (mmu_idx < 0) {
            break;
        }

        tlb_debug("idx %d\n", mmu_idx);

        tlb_flush_entry(&env->tlb_table[mmu_idx][i], addr);

        /* check whether there are vltb entries that need to be flushed */
        for (k = 0; k < CPU_VTLB_SIZE; k++) {
            tlb_flush_entry(&env->tlb_v_table[mmu_idx][k], addr);
        }
    }
    va_end(argp);

    tb_flush_jmp_cache(cpu, addr);
}

/* update the TLBs so that writes to code in the virtual page 'addr'
   can be detected */
void tlb_protect_code(ram_addr_t ram_addr)
{
    cpu_physical_memory_test_and_clear_dirty(ram_addr, TARGET_PAGE_SIZE,
                                             DIRTY_MEMORY_CODE);
}

/* update the TLB so that writes in physical page 'phys_addr' are no longer
   tested for self modifying code */
void tlb_unprotect_code(ram_addr_t ram_addr)
{
    cpu_physical_memory_set_dirty_flag(ram_addr, DIRTY_MEMORY_CODE);
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

static inline ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr)
{
    ram_addr_t ram_addr;

    if (qemu_ram_addr_from_host(ptr, &ram_addr) == NULL) {
        fprintf(stderr, "Bad ram pointer %p\n", ptr);
        abort();
    }
    return ram_addr;
}

void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length)
{
    CPUArchState *env;

    int mmu_idx;

    env = cpu->env_ptr;
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        unsigned int i;

        for (i = 0; i < CPU_TLB_SIZE; i++) {
            tlb_reset_dirty_range(&env->tlb_table[mmu_idx][i],
                                  start1, length);
        }

        for (i = 0; i < CPU_VTLB_SIZE; i++) {
            tlb_reset_dirty_range(&env->tlb_v_table[mmu_idx][i],
                                  start1, length);
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
void tlb_set_dirty(CPUState *cpu, target_ulong vaddr)
{
    CPUArchState *env = cpu->env_ptr;
    int i;
    int mmu_idx;

    vaddr &= TARGET_PAGE_MASK;
    i = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_set_dirty1(&env->tlb_table[mmu_idx][i], vaddr);
    }

    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        int k;
        for (k = 0; k < CPU_VTLB_SIZE; k++) {
            tlb_set_dirty1(&env->tlb_v_table[mmu_idx][k], vaddr);
        }
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
 * is permitted. Only a single TARGET_PAGE_SIZE region is mapped, the
 * supplied size is only used by tlb_flush_page.
 *
 * Called from TCG-generated code, which is under an RCU read-side
 * critical section.
 */
void tlb_set_page_with_attrs(CPUState *cpu, target_ulong vaddr,
                             hwaddr paddr, MemTxAttrs attrs, int prot,
                             int mmu_idx, target_ulong size)
{
    CPUArchState *env = cpu->env_ptr;
    MemoryRegionSection *section;
    unsigned int index;
    target_ulong address;
    target_ulong code_address;
    uintptr_t addend;
    CPUTLBEntry *te;
    hwaddr iotlb, xlat, sz;
    unsigned vidx = env->vtlb_index++ % CPU_VTLB_SIZE;
    int asidx = cpu_asidx_from_attrs(cpu, attrs);

    assert(size >= TARGET_PAGE_SIZE);
    if (size != TARGET_PAGE_SIZE) {
        tlb_add_large_page(env, vaddr, size);
    }

    sz = size;
    section = address_space_translate_for_iotlb(cpu, asidx, paddr, &xlat, &sz);
    assert(sz >= TARGET_PAGE_SIZE);

    tlb_debug("vaddr=" TARGET_FMT_lx " paddr=0x" TARGET_FMT_plx
              " prot=%x idx=%d\n",
              vaddr, paddr, prot, mmu_idx);

    address = vaddr;
    if (!memory_region_is_ram(section->mr) && !memory_region_is_romd(section->mr)) {
        /* IO memory case */
        address |= TLB_MMIO;
        addend = 0;
    } else {
        /* TLB_MMIO for rom/romd handled below */
        addend = (uintptr_t)memory_region_get_ram_ptr(section->mr) + xlat;
    }

    code_address = address;
    iotlb = memory_region_section_get_iotlb(cpu, section, vaddr, paddr, xlat,
                                            prot, &address);

    index = (vaddr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    te = &env->tlb_table[mmu_idx][index];

    /* do not discard the translation in te, evict it into a victim tlb */
    env->tlb_v_table[mmu_idx][vidx] = *te;
    env->iotlb_v[mmu_idx][vidx] = env->iotlb[mmu_idx][index];

    /* refill the tlb */
    env->iotlb[mmu_idx][index].addr = iotlb - vaddr;
    env->iotlb[mmu_idx][index].attrs = attrs;
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
                   && cpu_physical_memory_is_clean(
                        memory_region_get_ram_addr(section->mr) + xlat)) {
            te->addr_write = address | TLB_NOTDIRTY;
        } else {
            te->addr_write = address;
        }
    } else {
        te->addr_write = -1;
    }
}

/* Add a new TLB entry, but without specifying the memory
 * transaction attributes to be used.
 */
void tlb_set_page(CPUState *cpu, target_ulong vaddr,
                  hwaddr paddr, int prot,
                  int mmu_idx, target_ulong size)
{
    tlb_set_page_with_attrs(cpu, vaddr, paddr, MEMTXATTRS_UNSPECIFIED,
                            prot, mmu_idx, size);
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
    CPUState *cpu = ENV_GET_CPU(env1);
    CPUIOTLBEntry *iotlbentry;

    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = cpu_mmu_index(env1, true);
    if (unlikely(env1->tlb_table[mmu_idx][page_index].addr_code !=
                 (addr & TARGET_PAGE_MASK))) {
        cpu_ldub_code(env1, addr);
    }
    iotlbentry = &env1->iotlb[mmu_idx][page_index];
    pd = iotlbentry->addr & ~TARGET_PAGE_MASK;
    mr = iotlb_to_region(cpu, pd, iotlbentry->attrs);
    if (memory_region_is_unassigned(mr)) {
        CPUClass *cc = CPU_GET_CLASS(cpu);

        if (cc->do_unassigned_access) {
            cc->do_unassigned_access(cpu, addr, false, true, 0, 4);
        } else {
            cpu_abort(cpu, "Trying to execute code outside RAM or ROM at 0x"
                      TARGET_FMT_lx "\n", addr);
        }
    }
    p = (void *)((uintptr_t)addr + env1->tlb_table[mmu_idx][page_index].addend);
    return qemu_ram_addr_from_host_nofail(p);
}

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"
#undef MMUSUFFIX

#define MMUSUFFIX _cmmu
#undef GETPC_ADJ
#define GETPC_ADJ 0
#undef GETRA
#define GETRA() ((uintptr_t)0)
#define SOFTMMU_CODE_ACCESS

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"
