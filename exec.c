/*
 *  Virtual page mapping
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
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#endif

#include "qemu-common.h"
#include "cpu.h"
#include "tcg.h"
#include "hw/hw.h"
#include "hw/qdev.h"
#include "qemu/osdep.h"
#include "sysemu/kvm.h"
#include "hw/xen/xen.h"
#include "qemu/timer.h"
#include "qemu/config-file.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "exec/address-spaces.h"
#if defined(CONFIG_USER_ONLY)
#include <qemu.h>
#else /* !CONFIG_USER_ONLY */
#include "sysemu/xen-mapcache.h"
#include "trace.h"
#endif
#include "exec/cpu-all.h"

#include "exec/cputlb.h"
#include "translate-all.h"

#include "exec/memory-internal.h"

//#define DEBUG_SUBPAGE

#if !defined(CONFIG_USER_ONLY)
int phys_ram_fd;
static int in_migration;

RAMList ram_list = { .blocks = QTAILQ_HEAD_INITIALIZER(ram_list.blocks) };

static MemoryRegion *system_memory;
static MemoryRegion *system_io;

AddressSpace address_space_io;
AddressSpace address_space_memory;

MemoryRegion io_mem_rom, io_mem_notdirty;
static MemoryRegion io_mem_unassigned;

#endif

CPUArchState *first_cpu;
/* current CPU in the current thread. It is only valid inside
   cpu_exec() */
DEFINE_TLS(CPUArchState *,cpu_single_env);
/* 0 = Do not count executed instructions.
   1 = Precise instruction counting.
   2 = Adaptive rate instruction counting.  */
int use_icount;

#if !defined(CONFIG_USER_ONLY)

typedef struct PhysPageEntry PhysPageEntry;

struct PhysPageEntry {
    uint16_t is_leaf : 1;
     /* index into phys_sections (is_leaf) or phys_map_nodes (!is_leaf) */
    uint16_t ptr : 15;
};

struct AddressSpaceDispatch {
    /* This is a multi-level map on the physical address space.
     * The bottom level has pointers to MemoryRegionSections.
     */
    PhysPageEntry phys_map;
    MemoryListener listener;
    AddressSpace *as;
};

#define SUBPAGE_IDX(addr) ((addr) & ~TARGET_PAGE_MASK)
typedef struct subpage_t {
    MemoryRegion iomem;
    AddressSpace *as;
    hwaddr base;
    uint16_t sub_section[TARGET_PAGE_SIZE];
} subpage_t;

static MemoryRegionSection *phys_sections;
static unsigned phys_sections_nb, phys_sections_nb_alloc;
static uint16_t phys_section_unassigned;
static uint16_t phys_section_notdirty;
static uint16_t phys_section_rom;
static uint16_t phys_section_watch;

/* Simple allocator for PhysPageEntry nodes */
static PhysPageEntry (*phys_map_nodes)[L2_SIZE];
static unsigned phys_map_nodes_nb, phys_map_nodes_nb_alloc;

#define PHYS_MAP_NODE_NIL (((uint16_t)~0) >> 1)

static void io_mem_init(void);
static void memory_map_init(void);
static void *qemu_safe_ram_ptr(ram_addr_t addr);

static MemoryRegion io_mem_watch;
#endif

#if !defined(CONFIG_USER_ONLY)

static void phys_map_node_reserve(unsigned nodes)
{
    if (phys_map_nodes_nb + nodes > phys_map_nodes_nb_alloc) {
        typedef PhysPageEntry Node[L2_SIZE];
        phys_map_nodes_nb_alloc = MAX(phys_map_nodes_nb_alloc * 2, 16);
        phys_map_nodes_nb_alloc = MAX(phys_map_nodes_nb_alloc,
                                      phys_map_nodes_nb + nodes);
        phys_map_nodes = g_renew(Node, phys_map_nodes,
                                 phys_map_nodes_nb_alloc);
    }
}

static uint16_t phys_map_node_alloc(void)
{
    unsigned i;
    uint16_t ret;

    ret = phys_map_nodes_nb++;
    assert(ret != PHYS_MAP_NODE_NIL);
    assert(ret != phys_map_nodes_nb_alloc);
    for (i = 0; i < L2_SIZE; ++i) {
        phys_map_nodes[ret][i].is_leaf = 0;
        phys_map_nodes[ret][i].ptr = PHYS_MAP_NODE_NIL;
    }
    return ret;
}

static void phys_map_nodes_reset(void)
{
    phys_map_nodes_nb = 0;
}


static void phys_page_set_level(PhysPageEntry *lp, hwaddr *index,
                                hwaddr *nb, uint16_t leaf,
                                int level)
{
    PhysPageEntry *p;
    int i;
    hwaddr step = (hwaddr)1 << (level * L2_BITS);

    if (!lp->is_leaf && lp->ptr == PHYS_MAP_NODE_NIL) {
        lp->ptr = phys_map_node_alloc();
        p = phys_map_nodes[lp->ptr];
        if (level == 0) {
            for (i = 0; i < L2_SIZE; i++) {
                p[i].is_leaf = 1;
                p[i].ptr = phys_section_unassigned;
            }
        }
    } else {
        p = phys_map_nodes[lp->ptr];
    }
    lp = &p[(*index >> (level * L2_BITS)) & (L2_SIZE - 1)];

    while (*nb && lp < &p[L2_SIZE]) {
        if ((*index & (step - 1)) == 0 && *nb >= step) {
            lp->is_leaf = true;
            lp->ptr = leaf;
            *index += step;
            *nb -= step;
        } else {
            phys_page_set_level(lp, index, nb, leaf, level - 1);
        }
        ++lp;
    }
}

static void phys_page_set(AddressSpaceDispatch *d,
                          hwaddr index, hwaddr nb,
                          uint16_t leaf)
{
    /* Wildly overreserve - it doesn't matter much. */
    phys_map_node_reserve(3 * P_L2_LEVELS);

    phys_page_set_level(&d->phys_map, &index, &nb, leaf, P_L2_LEVELS - 1);
}

static MemoryRegionSection *phys_page_find(AddressSpaceDispatch *d, hwaddr index)
{
    PhysPageEntry lp = d->phys_map;
    PhysPageEntry *p;
    int i;

    for (i = P_L2_LEVELS - 1; i >= 0 && !lp.is_leaf; i--) {
        if (lp.ptr == PHYS_MAP_NODE_NIL) {
            return &phys_sections[phys_section_unassigned];
        }
        p = phys_map_nodes[lp.ptr];
        lp = p[(index >> (i * L2_BITS)) & (L2_SIZE - 1)];
    }
    return &phys_sections[lp.ptr];
}

bool memory_region_is_unassigned(MemoryRegion *mr)
{
    return mr != &io_mem_rom && mr != &io_mem_notdirty && !mr->rom_device
        && mr != &io_mem_watch;
}

static MemoryRegionSection *address_space_lookup_region(AddressSpace *as,
                                                        hwaddr addr,
                                                        bool resolve_subpage)
{
    MemoryRegionSection *section;
    subpage_t *subpage;

    section = phys_page_find(as->dispatch, addr >> TARGET_PAGE_BITS);
    if (resolve_subpage && section->mr->subpage) {
        subpage = container_of(section->mr, subpage_t, iomem);
        section = &phys_sections[subpage->sub_section[SUBPAGE_IDX(addr)]];
    }
    return section;
}

static MemoryRegionSection *
address_space_translate_internal(AddressSpace *as, hwaddr addr, hwaddr *xlat,
                                 hwaddr *plen, bool resolve_subpage)
{
    MemoryRegionSection *section;
    Int128 diff;

    section = address_space_lookup_region(as, addr, resolve_subpage);
    /* Compute offset within MemoryRegionSection */
    addr -= section->offset_within_address_space;

    /* Compute offset within MemoryRegion */
    *xlat = addr + section->offset_within_region;

    diff = int128_sub(section->mr->size, int128_make64(addr));
    *plen = int128_get64(int128_min(diff, int128_make64(*plen)));
    return section;
}

MemoryRegion *address_space_translate(AddressSpace *as, hwaddr addr,
                                      hwaddr *xlat, hwaddr *plen,
                                      bool is_write)
{
    IOMMUTLBEntry iotlb;
    MemoryRegionSection *section;
    MemoryRegion *mr;
    hwaddr len = *plen;

    for (;;) {
        section = address_space_translate_internal(as, addr, &addr, plen, true);
        mr = section->mr;

        if (!mr->iommu_ops) {
            break;
        }

        iotlb = mr->iommu_ops->translate(mr, addr);
        addr = ((iotlb.translated_addr & ~iotlb.addr_mask)
                | (addr & iotlb.addr_mask));
        len = MIN(len, (addr | iotlb.addr_mask) - addr + 1);
        if (!(iotlb.perm & (1 << is_write))) {
            mr = &io_mem_unassigned;
            break;
        }

        as = iotlb.target_as;
    }

    *plen = len;
    *xlat = addr;
    return mr;
}

MemoryRegionSection *
address_space_translate_for_iotlb(AddressSpace *as, hwaddr addr, hwaddr *xlat,
                                  hwaddr *plen)
{
    MemoryRegionSection *section;
    section = address_space_translate_internal(as, addr, xlat, plen, false);

    assert(!section->mr->iommu_ops);
    return section;
}
#endif

void cpu_exec_init_all(void)
{
#if !defined(CONFIG_USER_ONLY)
    qemu_mutex_init(&ram_list.mutex);
    memory_map_init();
    io_mem_init();
#endif
}

#if !defined(CONFIG_USER_ONLY)

static int cpu_common_post_load(void *opaque, int version_id)
{
    CPUState *cpu = opaque;

    /* 0x01 was CPU_INTERRUPT_EXIT. This line can be removed when the
       version_id is increased. */
    cpu->interrupt_request &= ~0x01;
    tlb_flush(cpu->env_ptr, 1);

    return 0;
}

const VMStateDescription vmstate_cpu_common = {
    .name = "cpu_common",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = cpu_common_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(halted, CPUState),
        VMSTATE_UINT32(interrupt_request, CPUState),
        VMSTATE_END_OF_LIST()
    }
};

#endif

CPUState *qemu_get_cpu(int index)
{
    CPUArchState *env = first_cpu;
    CPUState *cpu = NULL;

    while (env) {
        cpu = ENV_GET_CPU(env);
        if (cpu->cpu_index == index) {
            break;
        }
        env = env->next_cpu;
    }

    return env ? cpu : NULL;
}

void qemu_for_each_cpu(void (*func)(CPUState *cpu, void *data), void *data)
{
    CPUArchState *env = first_cpu;

    while (env) {
        func(ENV_GET_CPU(env), data);
        env = env->next_cpu;
    }
}

void cpu_exec_init(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUArchState **penv;
    int cpu_index;

#if defined(CONFIG_USER_ONLY)
    cpu_list_lock();
#endif
    env->next_cpu = NULL;
    penv = &first_cpu;
    cpu_index = 0;
    while (*penv != NULL) {
        penv = &(*penv)->next_cpu;
        cpu_index++;
    }
    cpu->cpu_index = cpu_index;
    cpu->numa_node = 0;
    QTAILQ_INIT(&env->breakpoints);
    QTAILQ_INIT(&env->watchpoints);
#ifndef CONFIG_USER_ONLY
    cpu->thread_id = qemu_get_thread_id();
#endif
    *penv = env;
#if defined(CONFIG_USER_ONLY)
    cpu_list_unlock();
#endif
    vmstate_register(NULL, cpu_index, &vmstate_cpu_common, cpu);
#if defined(CPU_SAVE_VERSION) && !defined(CONFIG_USER_ONLY)
    register_savevm(NULL, "cpu", cpu_index, CPU_SAVE_VERSION,
                    cpu_save, cpu_load, env);
    assert(cc->vmsd == NULL);
#endif
    if (cc->vmsd != NULL) {
        vmstate_register(NULL, cpu_index, cc->vmsd, cpu);
    }
}

#if defined(TARGET_HAS_ICE)
#if defined(CONFIG_USER_ONLY)
static void breakpoint_invalidate(CPUArchState *env, target_ulong pc)
{
    tb_invalidate_phys_page_range(pc, pc + 1, 0);
}
#else
static void breakpoint_invalidate(CPUArchState *env, target_ulong pc)
{
    tb_invalidate_phys_addr(cpu_get_phys_page_debug(env, pc) |
            (pc & ~TARGET_PAGE_MASK));
}
#endif
#endif /* TARGET_HAS_ICE */

#if defined(CONFIG_USER_ONLY)
void cpu_watchpoint_remove_all(CPUArchState *env, int mask)

{
}

int cpu_watchpoint_insert(CPUArchState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}
#else
/* Add a watchpoint.  */
int cpu_watchpoint_insert(CPUArchState *env, target_ulong addr, target_ulong len,
                          int flags, CPUWatchpoint **watchpoint)
{
    target_ulong len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    /* sanity checks: allow power-of-2 lengths, deny unaligned watchpoints */
    if ((len & (len - 1)) || (addr & ~len_mask) ||
            len == 0 || len > TARGET_PAGE_SIZE) {
        fprintf(stderr, "qemu: tried to set invalid watchpoint at "
                TARGET_FMT_lx ", len=" TARGET_FMT_lu "\n", addr, len);
        return -EINVAL;
    }
    wp = g_malloc(sizeof(*wp));

    wp->vaddr = addr;
    wp->len_mask = len_mask;
    wp->flags = flags;

    /* keep all GDB-injected watchpoints in front */
    if (flags & BP_GDB)
        QTAILQ_INSERT_HEAD(&env->watchpoints, wp, entry);
    else
        QTAILQ_INSERT_TAIL(&env->watchpoints, wp, entry);

    tlb_flush_page(env, addr);

    if (watchpoint)
        *watchpoint = wp;
    return 0;
}

/* Remove a specific watchpoint.  */
int cpu_watchpoint_remove(CPUArchState *env, target_ulong addr, target_ulong len,
                          int flags)
{
    target_ulong len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if (addr == wp->vaddr && len_mask == wp->len_mask
                && flags == (wp->flags & ~BP_WATCHPOINT_HIT)) {
            cpu_watchpoint_remove_by_ref(env, wp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific watchpoint by reference.  */
void cpu_watchpoint_remove_by_ref(CPUArchState *env, CPUWatchpoint *watchpoint)
{
    QTAILQ_REMOVE(&env->watchpoints, watchpoint, entry);

    tlb_flush_page(env, watchpoint->vaddr);

    g_free(watchpoint);
}

/* Remove all matching watchpoints.  */
void cpu_watchpoint_remove_all(CPUArchState *env, int mask)
{
    CPUWatchpoint *wp, *next;

    QTAILQ_FOREACH_SAFE(wp, &env->watchpoints, entry, next) {
        if (wp->flags & mask)
            cpu_watchpoint_remove_by_ref(env, wp);
    }
}
#endif

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUArchState *env, target_ulong pc, int flags,
                          CPUBreakpoint **breakpoint)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    bp = g_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB)
        QTAILQ_INSERT_HEAD(&env->breakpoints, bp, entry);
    else
        QTAILQ_INSERT_TAIL(&env->breakpoints, bp, entry);

    breakpoint_invalidate(env, pc);

    if (breakpoint)
        *breakpoint = bp;
    return 0;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUArchState *env, target_ulong pc, int flags)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(env, bp);
            return 0;
        }
    }
    return -ENOENT;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUArchState *env, CPUBreakpoint *breakpoint)
{
#if defined(TARGET_HAS_ICE)
    QTAILQ_REMOVE(&env->breakpoints, breakpoint, entry);

    breakpoint_invalidate(env, breakpoint->pc);

    g_free(breakpoint);
#endif
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUArchState *env, int mask)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &env->breakpoints, entry, next) {
        if (bp->flags & mask)
            cpu_breakpoint_remove_by_ref(env, bp);
    }
#endif
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUArchState *env, int enabled)
{
#if defined(TARGET_HAS_ICE)
    if (env->singlestep_enabled != enabled) {
        env->singlestep_enabled = enabled;
        if (kvm_enabled())
            kvm_update_guest_debug(env, 0);
        else {
            /* must flush all the translated code to avoid inconsistencies */
            /* XXX: only flush what is necessary */
            tb_flush(env);
        }
    }
#endif
}

void cpu_abort(CPUArchState *env, const char *fmt, ...)
{
    CPUState *cpu = ENV_GET_CPU(env);
    va_list ap;
    va_list ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    cpu_dump_state(cpu, stderr, fprintf, CPU_DUMP_FPU | CPU_DUMP_CCOP);
    if (qemu_log_enabled()) {
        qemu_log("qemu: fatal: ");
        qemu_log_vprintf(fmt, ap2);
        qemu_log("\n");
        log_cpu_state(env, CPU_DUMP_FPU | CPU_DUMP_CCOP);
        qemu_log_flush();
        qemu_log_close();
    }
    va_end(ap2);
    va_end(ap);
#if defined(CONFIG_USER_ONLY)
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_handler = SIG_DFL;
        sigaction(SIGABRT, &act, NULL);
    }
#endif
    abort();
}

CPUArchState *cpu_copy(CPUArchState *env)
{
    CPUArchState *new_env = cpu_init(env->cpu_model_str);
    CPUArchState *next_cpu = new_env->next_cpu;
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;
    CPUWatchpoint *wp;
#endif

    memcpy(new_env, env, sizeof(CPUArchState));

    /* Preserve chaining. */
    new_env->next_cpu = next_cpu;

    /* Clone all break/watchpoints.
       Note: Once we support ptrace with hw-debug register access, make sure
       BP_CPU break/watchpoints are handled correctly on clone. */
    QTAILQ_INIT(&env->breakpoints);
    QTAILQ_INIT(&env->watchpoints);
#if defined(TARGET_HAS_ICE)
    QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
        cpu_breakpoint_insert(new_env, bp->pc, bp->flags, NULL);
    }
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        cpu_watchpoint_insert(new_env, wp->vaddr, (~wp->len_mask) + 1,
                              wp->flags, NULL);
    }
#endif

    return new_env;
}

#if !defined(CONFIG_USER_ONLY)
static void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t end,
                                      uintptr_t length)
{
    uintptr_t start1;

    /* we modify the TLB cache so that the dirty bit will be set again
       when accessing the range */
    start1 = (uintptr_t)qemu_safe_ram_ptr(start);
    /* Check that we don't span multiple blocks - this breaks the
       address comparisons below.  */
    if ((uintptr_t)qemu_safe_ram_ptr(end - 1) - start1
            != (end - 1) - start) {
        abort();
    }
    cpu_tlb_reset_dirty_all(start1, length);

}

/* Note: start and end must be within the same ram block.  */
void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t end,
                                     int dirty_flags)
{
    uintptr_t length;

    start &= TARGET_PAGE_MASK;
    end = TARGET_PAGE_ALIGN(end);

    length = end - start;
    if (length == 0)
        return;
    cpu_physical_memory_mask_dirty_range(start, length, dirty_flags);

    if (tcg_enabled()) {
        tlb_reset_dirty_range_all(start, end, length);
    }
}

static int cpu_physical_memory_set_dirty_tracking(int enable)
{
    int ret = 0;
    in_migration = enable;
    return ret;
}

hwaddr memory_region_section_get_iotlb(CPUArchState *env,
                                       MemoryRegionSection *section,
                                       target_ulong vaddr,
                                       hwaddr paddr, hwaddr xlat,
                                       int prot,
                                       target_ulong *address)
{
    hwaddr iotlb;
    CPUWatchpoint *wp;

    if (memory_region_is_ram(section->mr)) {
        /* Normal RAM.  */
        iotlb = (memory_region_get_ram_addr(section->mr) & TARGET_PAGE_MASK)
            + xlat;
        if (!section->readonly) {
            iotlb |= phys_section_notdirty;
        } else {
            iotlb |= phys_section_rom;
        }
    } else {
        iotlb = section - phys_sections;
        iotlb += xlat;
    }

    /* Make accesses to pages with watchpoints go via the
       watchpoint trap routines.  */
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if (vaddr == (wp->vaddr & TARGET_PAGE_MASK)) {
            /* Avoid trapping reads of pages with a write breakpoint. */
            if ((prot & PAGE_WRITE) || (wp->flags & BP_MEM_READ)) {
                iotlb = phys_section_watch + paddr;
                *address |= TLB_MMIO;
                break;
            }
        }
    }

    return iotlb;
}
#endif /* defined(CONFIG_USER_ONLY) */

#if !defined(CONFIG_USER_ONLY)

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             uint16_t section);
static subpage_t *subpage_init(AddressSpace *as, hwaddr base);
static void destroy_page_desc(uint16_t section_index)
{
    MemoryRegionSection *section = &phys_sections[section_index];
    MemoryRegion *mr = section->mr;

    if (mr->subpage) {
        subpage_t *subpage = container_of(mr, subpage_t, iomem);
        memory_region_destroy(&subpage->iomem);
        g_free(subpage);
    }
}

static void destroy_l2_mapping(PhysPageEntry *lp, unsigned level)
{
    unsigned i;
    PhysPageEntry *p;

    if (lp->ptr == PHYS_MAP_NODE_NIL) {
        return;
    }

    p = phys_map_nodes[lp->ptr];
    for (i = 0; i < L2_SIZE; ++i) {
        if (!p[i].is_leaf) {
            destroy_l2_mapping(&p[i], level - 1);
        } else {
            destroy_page_desc(p[i].ptr);
        }
    }
    lp->is_leaf = 0;
    lp->ptr = PHYS_MAP_NODE_NIL;
}

static void destroy_all_mappings(AddressSpaceDispatch *d)
{
    destroy_l2_mapping(&d->phys_map, P_L2_LEVELS - 1);
    phys_map_nodes_reset();
}

static uint16_t phys_section_add(MemoryRegionSection *section)
{
    /* The physical section number is ORed with a page-aligned
     * pointer to produce the iotlb entries.  Thus it should
     * never overflow into the page-aligned value.
     */
    assert(phys_sections_nb < TARGET_PAGE_SIZE);

    if (phys_sections_nb == phys_sections_nb_alloc) {
        phys_sections_nb_alloc = MAX(phys_sections_nb_alloc * 2, 16);
        phys_sections = g_renew(MemoryRegionSection, phys_sections,
                                phys_sections_nb_alloc);
    }
    phys_sections[phys_sections_nb] = *section;
    return phys_sections_nb++;
}

static void phys_sections_clear(void)
{
    phys_sections_nb = 0;
}

static void register_subpage(AddressSpaceDispatch *d, MemoryRegionSection *section)
{
    subpage_t *subpage;
    hwaddr base = section->offset_within_address_space
        & TARGET_PAGE_MASK;
    MemoryRegionSection *existing = phys_page_find(d, base >> TARGET_PAGE_BITS);
    MemoryRegionSection subsection = {
        .offset_within_address_space = base,
        .size = int128_make64(TARGET_PAGE_SIZE),
    };
    hwaddr start, end;

    assert(existing->mr->subpage || existing->mr == &io_mem_unassigned);

    if (!(existing->mr->subpage)) {
        subpage = subpage_init(d->as, base);
        subsection.mr = &subpage->iomem;
        phys_page_set(d, base >> TARGET_PAGE_BITS, 1,
                      phys_section_add(&subsection));
    } else {
        subpage = container_of(existing->mr, subpage_t, iomem);
    }
    start = section->offset_within_address_space & ~TARGET_PAGE_MASK;
    end = start + int128_get64(section->size) - 1;
    subpage_register(subpage, start, end, phys_section_add(section));
}


static void register_multipage(AddressSpaceDispatch *d,
                               MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    uint16_t section_index = phys_section_add(section);
    uint64_t num_pages = int128_get64(int128_rshift(section->size,
                                                    TARGET_PAGE_BITS));

    assert(num_pages);
    phys_page_set(d, start_addr >> TARGET_PAGE_BITS, num_pages, section_index);
}

static void mem_add(MemoryListener *listener, MemoryRegionSection *section)
{
    AddressSpaceDispatch *d = container_of(listener, AddressSpaceDispatch, listener);
    MemoryRegionSection now = *section, remain = *section;
    Int128 page_size = int128_make64(TARGET_PAGE_SIZE);

    if (now.offset_within_address_space & ~TARGET_PAGE_MASK) {
        uint64_t left = TARGET_PAGE_ALIGN(now.offset_within_address_space)
                       - now.offset_within_address_space;

        now.size = int128_min(int128_make64(left), now.size);
        register_subpage(d, &now);
    } else {
        now.size = int128_zero();
    }
    while (int128_ne(remain.size, now.size)) {
        remain.size = int128_sub(remain.size, now.size);
        remain.offset_within_address_space += int128_get64(now.size);
        remain.offset_within_region += int128_get64(now.size);
        now = remain;
        if (int128_lt(remain.size, page_size)) {
            register_subpage(d, &now);
        } else if (remain.offset_within_region & ~TARGET_PAGE_MASK) {
            now.size = page_size;
            register_subpage(d, &now);
        } else {
            now.size = int128_and(now.size, int128_neg(page_size));
            register_multipage(d, &now);
        }
    }
}

void qemu_flush_coalesced_mmio_buffer(void)
{
    if (kvm_enabled())
        kvm_flush_coalesced_mmio_buffer();
}

void qemu_mutex_lock_ramlist(void)
{
    qemu_mutex_lock(&ram_list.mutex);
}

void qemu_mutex_unlock_ramlist(void)
{
    qemu_mutex_unlock(&ram_list.mutex);
}

#if defined(__linux__) && !defined(TARGET_S390X)

#include <sys/vfs.h>

#define HUGETLBFS_MAGIC       0x958458f6

static long gethugepagesize(const char *path)
{
    struct statfs fs;
    int ret;

    do {
        ret = statfs(path, &fs);
    } while (ret != 0 && errno == EINTR);

    if (ret != 0) {
        perror(path);
        return 0;
    }

    if (fs.f_type != HUGETLBFS_MAGIC)
        fprintf(stderr, "Warning: path not on HugeTLBFS: %s\n", path);

    return fs.f_bsize;
}

static void *file_ram_alloc(RAMBlock *block,
                            ram_addr_t memory,
                            const char *path)
{
    char *filename;
    char *sanitized_name;
    char *c;
    void *area;
    int fd;
#ifdef MAP_POPULATE
    int flags;
#endif
    unsigned long hpagesize;

    hpagesize = gethugepagesize(path);
    if (!hpagesize) {
        return NULL;
    }

    if (memory < hpagesize) {
        return NULL;
    }

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        fprintf(stderr, "host lacks kvm mmu notifiers, -mem-path unsupported\n");
        return NULL;
    }

    /* Make name safe to use with mkstemp by replacing '/' with '_'. */
    sanitized_name = g_strdup(block->mr->name);
    for (c = sanitized_name; *c != '\0'; c++) {
        if (*c == '/')
            *c = '_';
    }

    filename = g_strdup_printf("%s/qemu_back_mem.%s.XXXXXX", path,
                               sanitized_name);
    g_free(sanitized_name);

    fd = mkstemp(filename);
    if (fd < 0) {
        perror("unable to create backing store for hugepages");
        g_free(filename);
        return NULL;
    }
    unlink(filename);
    g_free(filename);

    memory = (memory+hpagesize-1) & ~(hpagesize-1);

    /*
     * ftruncate is not supported by hugetlbfs in older
     * hosts, so don't bother bailing out on errors.
     * If anything goes wrong with it under other filesystems,
     * mmap will fail.
     */
    if (ftruncate(fd, memory))
        perror("ftruncate");

#ifdef MAP_POPULATE
    /* NB: MAP_POPULATE won't exhaustively alloc all phys pages in the case
     * MAP_PRIVATE is requested.  For mem_prealloc we mmap as MAP_SHARED
     * to sidestep this quirk.
     */
    flags = mem_prealloc ? MAP_POPULATE | MAP_SHARED : MAP_PRIVATE;
    area = mmap(0, memory, PROT_READ | PROT_WRITE, flags, fd, 0);
#else
    area = mmap(0, memory, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
#endif
    if (area == MAP_FAILED) {
        perror("file_ram_alloc: can't mmap RAM pages");
        close(fd);
        return (NULL);
    }
    block->fd = fd;
    return area;
}
#endif

static ram_addr_t find_ram_offset(ram_addr_t size)
{
    RAMBlock *block, *next_block;
    ram_addr_t offset = RAM_ADDR_MAX, mingap = RAM_ADDR_MAX;

    assert(size != 0); /* it would hand out same offset multiple times */

    if (QTAILQ_EMPTY(&ram_list.blocks))
        return 0;

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        ram_addr_t end, next = RAM_ADDR_MAX;

        end = block->offset + block->length;

        QTAILQ_FOREACH(next_block, &ram_list.blocks, next) {
            if (next_block->offset >= end) {
                next = MIN(next, next_block->offset);
            }
        }
        if (next - end >= size && next - end < mingap) {
            offset = end;
            mingap = next - end;
        }
    }

    if (offset == RAM_ADDR_MAX) {
        fprintf(stderr, "Failed to find gap of requested size: %" PRIu64 "\n",
                (uint64_t)size);
        abort();
    }

    return offset;
}

ram_addr_t last_ram_offset(void)
{
    RAMBlock *block;
    ram_addr_t last = 0;

    QTAILQ_FOREACH(block, &ram_list.blocks, next)
        last = MAX(last, block->offset + block->length);

    return last;
}

static void qemu_ram_setup_dump(void *addr, ram_addr_t size)
{
    int ret;
    QemuOpts *machine_opts;

    /* Use MADV_DONTDUMP, if user doesn't want the guest memory in the core */
    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (machine_opts &&
        !qemu_opt_get_bool(machine_opts, "dump-guest-core", true)) {
        ret = qemu_madvise(addr, size, QEMU_MADV_DONTDUMP);
        if (ret) {
            perror("qemu_madvise");
            fprintf(stderr, "madvise doesn't support MADV_DONTDUMP, "
                            "but dump_guest_core=off specified\n");
        }
    }
}

void qemu_ram_set_idstr(ram_addr_t addr, const char *name, DeviceState *dev)
{
    RAMBlock *new_block, *block;

    new_block = NULL;
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (block->offset == addr) {
            new_block = block;
            break;
        }
    }
    assert(new_block);
    assert(!new_block->idstr[0]);

    if (dev) {
        char *id = qdev_get_dev_path(dev);
        if (id) {
            snprintf(new_block->idstr, sizeof(new_block->idstr), "%s/", id);
            g_free(id);
        }
    }
    pstrcat(new_block->idstr, sizeof(new_block->idstr), name);

    /* This assumes the iothread lock is taken here too.  */
    qemu_mutex_lock_ramlist();
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (block != new_block && !strcmp(block->idstr, new_block->idstr)) {
            fprintf(stderr, "RAMBlock \"%s\" already registered, abort!\n",
                    new_block->idstr);
            abort();
        }
    }
    qemu_mutex_unlock_ramlist();
}

static int memory_try_enable_merging(void *addr, size_t len)
{
    QemuOpts *opts;

    opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (opts && !qemu_opt_get_bool(opts, "mem-merge", true)) {
        /* disabled by the user */
        return 0;
    }

    return qemu_madvise(addr, len, QEMU_MADV_MERGEABLE);
}

ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr)
{
    RAMBlock *block, *new_block;

    size = TARGET_PAGE_ALIGN(size);
    new_block = g_malloc0(sizeof(*new_block));

    /* This assumes the iothread lock is taken here too.  */
    qemu_mutex_lock_ramlist();
    new_block->mr = mr;
    new_block->offset = find_ram_offset(size);
    if (host) {
        new_block->host = host;
        new_block->flags |= RAM_PREALLOC_MASK;
    } else {
        if (mem_path) {
#if defined (__linux__) && !defined(TARGET_S390X)
            new_block->host = file_ram_alloc(new_block, size, mem_path);
            if (!new_block->host) {
                new_block->host = qemu_anon_ram_alloc(size);
                memory_try_enable_merging(new_block->host, size);
            }
#else
            fprintf(stderr, "-mem-path option unsupported\n");
            exit(1);
#endif
        } else {
            if (xen_enabled()) {
                xen_ram_alloc(new_block->offset, size, mr);
            } else if (kvm_enabled()) {
                /* some s390/kvm configurations have special constraints */
                new_block->host = kvm_ram_alloc(size);
            } else {
                new_block->host = qemu_anon_ram_alloc(size);
            }
            memory_try_enable_merging(new_block->host, size);
        }
    }
    new_block->length = size;

    /* Keep the list sorted from biggest to smallest block.  */
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (block->length < new_block->length) {
            break;
        }
    }
    if (block) {
        QTAILQ_INSERT_BEFORE(block, new_block, next);
    } else {
        QTAILQ_INSERT_TAIL(&ram_list.blocks, new_block, next);
    }
    ram_list.mru_block = NULL;

    ram_list.version++;
    qemu_mutex_unlock_ramlist();

    ram_list.phys_dirty = g_realloc(ram_list.phys_dirty,
                                       last_ram_offset() >> TARGET_PAGE_BITS);
    memset(ram_list.phys_dirty + (new_block->offset >> TARGET_PAGE_BITS),
           0, size >> TARGET_PAGE_BITS);
    cpu_physical_memory_set_dirty_range(new_block->offset, size, 0xff);

    qemu_ram_setup_dump(new_block->host, size);
    qemu_madvise(new_block->host, size, QEMU_MADV_HUGEPAGE);

    if (kvm_enabled())
        kvm_setup_guest_memory(new_block->host, size);

    return new_block->offset;
}

ram_addr_t qemu_ram_alloc(ram_addr_t size, MemoryRegion *mr)
{
    return qemu_ram_alloc_from_ptr(size, NULL, mr);
}

void qemu_ram_free_from_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    /* This assumes the iothread lock is taken here too.  */
    qemu_mutex_lock_ramlist();
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (addr == block->offset) {
            QTAILQ_REMOVE(&ram_list.blocks, block, next);
            ram_list.mru_block = NULL;
            ram_list.version++;
            g_free(block);
            break;
        }
    }
    qemu_mutex_unlock_ramlist();
}

void qemu_ram_free(ram_addr_t addr)
{
    RAMBlock *block;

    /* This assumes the iothread lock is taken here too.  */
    qemu_mutex_lock_ramlist();
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (addr == block->offset) {
            QTAILQ_REMOVE(&ram_list.blocks, block, next);
            ram_list.mru_block = NULL;
            ram_list.version++;
            if (block->flags & RAM_PREALLOC_MASK) {
                ;
            } else if (mem_path) {
#if defined (__linux__) && !defined(TARGET_S390X)
                if (block->fd) {
                    munmap(block->host, block->length);
                    close(block->fd);
                } else {
                    qemu_anon_ram_free(block->host, block->length);
                }
#else
                abort();
#endif
            } else {
                if (xen_enabled()) {
                    xen_invalidate_map_cache_entry(block->host);
                } else {
                    qemu_anon_ram_free(block->host, block->length);
                }
            }
            g_free(block);
            break;
        }
    }
    qemu_mutex_unlock_ramlist();

}

#ifndef _WIN32
void qemu_ram_remap(ram_addr_t addr, ram_addr_t length)
{
    RAMBlock *block;
    ram_addr_t offset;
    int flags;
    void *area, *vaddr;

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        offset = addr - block->offset;
        if (offset < block->length) {
            vaddr = block->host + offset;
            if (block->flags & RAM_PREALLOC_MASK) {
                ;
            } else {
                flags = MAP_FIXED;
                munmap(vaddr, length);
                if (mem_path) {
#if defined(__linux__) && !defined(TARGET_S390X)
                    if (block->fd) {
#ifdef MAP_POPULATE
                        flags |= mem_prealloc ? MAP_POPULATE | MAP_SHARED :
                            MAP_PRIVATE;
#else
                        flags |= MAP_PRIVATE;
#endif
                        area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                    flags, block->fd, offset);
                    } else {
                        flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                        area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                    flags, -1, 0);
                    }
#else
                    abort();
#endif
                } else {
#if defined(TARGET_S390X) && defined(CONFIG_KVM)
                    flags |= MAP_SHARED | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_EXEC|PROT_READ|PROT_WRITE,
                                flags, -1, 0);
#else
                    flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, -1, 0);
#endif
                }
                if (area != vaddr) {
                    fprintf(stderr, "Could not remap addr: "
                            RAM_ADDR_FMT "@" RAM_ADDR_FMT "\n",
                            length, addr);
                    exit(1);
                }
                memory_try_enable_merging(vaddr, length);
                qemu_ram_setup_dump(vaddr, length);
            }
            return;
        }
    }
}
#endif /* !_WIN32 */

/* Return a host pointer to ram allocated with qemu_ram_alloc.
   With the exception of the softmmu code in this file, this should
   only be used for local memory (e.g. video ram) that the device owns,
   and knows it isn't going to access beyond the end of the block.

   It should not be used for general purpose DMA.
   Use cpu_physical_memory_map/cpu_physical_memory_rw instead.
 */
void *qemu_get_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    /* The list is protected by the iothread lock here.  */
    block = ram_list.mru_block;
    if (block && addr - block->offset < block->length) {
        goto found;
    }
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->length) {
            goto found;
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

found:
    ram_list.mru_block = block;
    if (xen_enabled()) {
        /* We need to check if the requested address is in the RAM
         * because we don't want to map the entire memory in QEMU.
         * In that case just map until the end of the page.
         */
        if (block->offset == 0) {
            return xen_map_cache(addr, 0, 0);
        } else if (block->host == NULL) {
            block->host =
                xen_map_cache(block->offset, block->length, 1);
        }
    }
    return block->host + (addr - block->offset);
}

/* Return a host pointer to ram allocated with qemu_ram_alloc.  Same as
 * qemu_get_ram_ptr but do not touch ram_list.mru_block.
 *
 * ??? Is this still necessary?
 */
static void *qemu_safe_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block;

    /* The list is protected by the iothread lock here.  */
    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (addr - block->offset < block->length) {
            if (xen_enabled()) {
                /* We need to check if the requested address is in the RAM
                 * because we don't want to map the entire memory in QEMU.
                 * In that case just map until the end of the page.
                 */
                if (block->offset == 0) {
                    return xen_map_cache(addr, 0, 0);
                } else if (block->host == NULL) {
                    block->host =
                        xen_map_cache(block->offset, block->length, 1);
                }
            }
            return block->host + (addr - block->offset);
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

    return NULL;
}

/* Return a host pointer to guest's ram. Similar to qemu_get_ram_ptr
 * but takes a size argument */
static void *qemu_ram_ptr_length(ram_addr_t addr, ram_addr_t *size)
{
    if (*size == 0) {
        return NULL;
    }
    if (xen_enabled()) {
        return xen_map_cache(addr, *size, 1);
    } else {
        RAMBlock *block;

        QTAILQ_FOREACH(block, &ram_list.blocks, next) {
            if (addr - block->offset < block->length) {
                if (addr - block->offset + *size > block->length)
                    *size = block->length - addr + block->offset;
                return block->host + (addr - block->offset);
            }
        }

        fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
        abort();
    }
}

int qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr)
{
    RAMBlock *block;
    uint8_t *host = ptr;

    if (xen_enabled()) {
        *ram_addr = xen_ram_addr_from_mapcache(ptr);
        return 0;
    }

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        /* This case append when the block is not mapped. */
        if (block->host == NULL) {
            continue;
        }
        if (host - block->host < block->length) {
            *ram_addr = block->offset + (host - block->host);
            return 0;
        }
    }

    return -1;
}

/* Some of the softmmu routines need to translate from a host pointer
   (typically a TLB entry) back to a ram offset.  */
ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr)
{
    ram_addr_t ram_addr;

    if (qemu_ram_addr_from_host(ptr, &ram_addr)) {
        fprintf(stderr, "Bad ram pointer %p\n", ptr);
        abort();
    }
    return ram_addr;
}

static void notdirty_mem_write(void *opaque, hwaddr ram_addr,
                               uint64_t val, unsigned size)
{
    int dirty_flags;
    dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    if (!(dirty_flags & CODE_DIRTY_FLAG)) {
        tb_invalidate_phys_page_fast(ram_addr, size);
        dirty_flags = cpu_physical_memory_get_dirty_flags(ram_addr);
    }
    switch (size) {
    case 1:
        stb_p(qemu_get_ram_ptr(ram_addr), val);
        break;
    case 2:
        stw_p(qemu_get_ram_ptr(ram_addr), val);
        break;
    case 4:
        stl_p(qemu_get_ram_ptr(ram_addr), val);
        break;
    default:
        abort();
    }
    dirty_flags |= (0xff & ~CODE_DIRTY_FLAG);
    cpu_physical_memory_set_dirty_flags(ram_addr, dirty_flags);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (dirty_flags == 0xff)
        tlb_set_dirty(cpu_single_env, cpu_single_env->mem_io_vaddr);
}

static bool notdirty_mem_accepts(void *opaque, hwaddr addr,
                                 unsigned size, bool is_write)
{
    return is_write;
}

static const MemoryRegionOps notdirty_mem_ops = {
    .write = notdirty_mem_write,
    .valid.accepts = notdirty_mem_accepts,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Generate a debug exception if a watchpoint has been hit.  */
static void check_watchpoint(int offset, int len_mask, int flags)
{
    CPUArchState *env = cpu_single_env;
    target_ulong pc, cs_base;
    target_ulong vaddr;
    CPUWatchpoint *wp;
    int cpu_flags;

    if (env->watchpoint_hit) {
        /* We re-entered the check after replacing the TB. Now raise
         * the debug interrupt so that is will trigger after the
         * current instruction. */
        cpu_interrupt(ENV_GET_CPU(env), CPU_INTERRUPT_DEBUG);
        return;
    }
    vaddr = (env->mem_io_vaddr & TARGET_PAGE_MASK) + offset;
    QTAILQ_FOREACH(wp, &env->watchpoints, entry) {
        if ((vaddr == (wp->vaddr & len_mask) ||
             (vaddr & wp->len_mask) == wp->vaddr) && (wp->flags & flags)) {
            wp->flags |= BP_WATCHPOINT_HIT;
            if (!env->watchpoint_hit) {
                env->watchpoint_hit = wp;
                tb_check_watchpoint(env);
                if (wp->flags & BP_STOP_BEFORE_ACCESS) {
                    env->exception_index = EXCP_DEBUG;
                    cpu_loop_exit(env);
                } else {
                    cpu_get_tb_cpu_state(env, &pc, &cs_base, &cpu_flags);
                    tb_gen_code(env, pc, cs_base, cpu_flags, 1);
                    cpu_resume_from_signal(env, NULL);
                }
            }
        } else {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }
}

/* Watchpoint access routines.  Watchpoints are inserted using TLB tricks,
   so these check for a hit then pass through to the normal out-of-line
   phys routines.  */
static uint64_t watch_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~(size - 1), BP_MEM_READ);
    switch (size) {
    case 1: return ldub_phys(addr);
    case 2: return lduw_phys(addr);
    case 4: return ldl_phys(addr);
    default: abort();
    }
}

static void watch_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~(size - 1), BP_MEM_WRITE);
    switch (size) {
    case 1:
        stb_phys(addr, val);
        break;
    case 2:
        stw_phys(addr, val);
        break;
    case 4:
        stl_phys(addr, val);
        break;
    default: abort();
    }
}

static const MemoryRegionOps watch_mem_ops = {
    .read = watch_mem_read,
    .write = watch_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t subpage_read(void *opaque, hwaddr addr,
                             unsigned len)
{
    subpage_t *subpage = opaque;
    uint8_t buf[4];

#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %d addr " TARGET_FMT_plx "\n", __func__,
           subpage, len, addr);
#endif
    address_space_read(subpage->as, addr + subpage->base, buf, len);
    switch (len) {
    case 1:
        return ldub_p(buf);
    case 2:
        return lduw_p(buf);
    case 4:
        return ldl_p(buf);
    default:
        abort();
    }
}

static void subpage_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned len)
{
    subpage_t *subpage = opaque;
    uint8_t buf[4];

#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %d addr " TARGET_FMT_plx
           " value %"PRIx64"\n",
           __func__, subpage, len, addr, value);
#endif
    switch (len) {
    case 1:
        stb_p(buf, value);
        break;
    case 2:
        stw_p(buf, value);
        break;
    case 4:
        stl_p(buf, value);
        break;
    default:
        abort();
    }
    address_space_write(subpage->as, addr + subpage->base, buf, len);
}

static bool subpage_accepts(void *opaque, hwaddr addr,
                            unsigned size, bool is_write)
{
    subpage_t *subpage = opaque;
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p %c len %d addr " TARGET_FMT_plx "\n",
           __func__, subpage, is_write ? 'w' : 'r', len, addr);
#endif

    return address_space_access_valid(subpage->as, addr + subpage->base,
                                      size, is_write);
}

static const MemoryRegionOps subpage_ops = {
    .read = subpage_read,
    .write = subpage_write,
    .valid.accepts = subpage_accepts,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int subpage_register (subpage_t *mmio, uint32_t start, uint32_t end,
                             uint16_t section)
{
    int idx, eidx;

    if (start >= TARGET_PAGE_SIZE || end >= TARGET_PAGE_SIZE)
        return -1;
    idx = SUBPAGE_IDX(start);
    eidx = SUBPAGE_IDX(end);
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p start %08x end %08x idx %08x eidx %08x mem %ld\n", __func__,
           mmio, start, end, idx, eidx, memory);
#endif
    for (; idx <= eidx; idx++) {
        mmio->sub_section[idx] = section;
    }

    return 0;
}

static subpage_t *subpage_init(AddressSpace *as, hwaddr base)
{
    subpage_t *mmio;

    mmio = g_malloc0(sizeof(subpage_t));

    mmio->as = as;
    mmio->base = base;
    memory_region_init_io(&mmio->iomem, &subpage_ops, mmio,
                          "subpage", TARGET_PAGE_SIZE);
    mmio->iomem.subpage = true;
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p base " TARGET_FMT_plx " len %08x %d\n", __func__,
           mmio, base, TARGET_PAGE_SIZE, subpage_memory);
#endif
    subpage_register(mmio, 0, TARGET_PAGE_SIZE-1, phys_section_unassigned);

    return mmio;
}

static uint16_t dummy_section(MemoryRegion *mr)
{
    MemoryRegionSection section = {
        .mr = mr,
        .offset_within_address_space = 0,
        .offset_within_region = 0,
        .size = int128_2_64(),
    };

    return phys_section_add(&section);
}

MemoryRegion *iotlb_to_region(hwaddr index)
{
    return phys_sections[index & ~TARGET_PAGE_MASK].mr;
}

static void io_mem_init(void)
{
    memory_region_init_io(&io_mem_rom, &unassigned_mem_ops, NULL, "rom", UINT64_MAX);
    memory_region_init_io(&io_mem_unassigned, &unassigned_mem_ops, NULL,
                          "unassigned", UINT64_MAX);
    memory_region_init_io(&io_mem_notdirty, &notdirty_mem_ops, NULL,
                          "notdirty", UINT64_MAX);
    memory_region_init_io(&io_mem_watch, &watch_mem_ops, NULL,
                          "watch", UINT64_MAX);
}

static void mem_begin(MemoryListener *listener)
{
    AddressSpaceDispatch *d = container_of(listener, AddressSpaceDispatch, listener);

    destroy_all_mappings(d);
    d->phys_map.ptr = PHYS_MAP_NODE_NIL;
}

static void core_begin(MemoryListener *listener)
{
    phys_sections_clear();
    phys_section_unassigned = dummy_section(&io_mem_unassigned);
    phys_section_notdirty = dummy_section(&io_mem_notdirty);
    phys_section_rom = dummy_section(&io_mem_rom);
    phys_section_watch = dummy_section(&io_mem_watch);
}

static void tcg_commit(MemoryListener *listener)
{
    CPUArchState *env;

    /* since each CPU stores ram addresses in its TLB cache, we must
       reset the modified entries */
    /* XXX: slow ! */
    for(env = first_cpu; env != NULL; env = env->next_cpu) {
        tlb_flush(env, 1);
    }
}

static void core_log_global_start(MemoryListener *listener)
{
    cpu_physical_memory_set_dirty_tracking(1);
}

static void core_log_global_stop(MemoryListener *listener)
{
    cpu_physical_memory_set_dirty_tracking(0);
}

static void io_region_add(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    MemoryRegionIORange *mrio = g_new(MemoryRegionIORange, 1);

    mrio->mr = section->mr;
    mrio->offset = section->offset_within_region;
    iorange_init(&mrio->iorange, &memory_region_iorange_ops,
                 section->offset_within_address_space,
                 int128_get64(section->size));
    ioport_register(&mrio->iorange);
}

static void io_region_del(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    isa_unassign_ioport(section->offset_within_address_space,
                        int128_get64(section->size));
}

static MemoryListener core_memory_listener = {
    .begin = core_begin,
    .log_global_start = core_log_global_start,
    .log_global_stop = core_log_global_stop,
    .priority = 1,
};

static MemoryListener io_memory_listener = {
    .region_add = io_region_add,
    .region_del = io_region_del,
    .priority = 0,
};

static MemoryListener tcg_memory_listener = {
    .commit = tcg_commit,
};

void address_space_init_dispatch(AddressSpace *as)
{
    AddressSpaceDispatch *d = g_new(AddressSpaceDispatch, 1);

    d->phys_map  = (PhysPageEntry) { .ptr = PHYS_MAP_NODE_NIL, .is_leaf = 0 };
    d->listener = (MemoryListener) {
        .begin = mem_begin,
        .region_add = mem_add,
        .region_nop = mem_add,
        .priority = 0,
    };
    d->as = as;
    as->dispatch = d;
    memory_listener_register(&d->listener, as);
}

void address_space_destroy_dispatch(AddressSpace *as)
{
    AddressSpaceDispatch *d = as->dispatch;

    memory_listener_unregister(&d->listener);
    destroy_l2_mapping(&d->phys_map, P_L2_LEVELS - 1);
    g_free(d);
    as->dispatch = NULL;
}

static void memory_map_init(void)
{
    system_memory = g_malloc(sizeof(*system_memory));
    memory_region_init(system_memory, "system", INT64_MAX);
    address_space_init(&address_space_memory, system_memory, "memory");

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init(system_io, "io", 65536);
    address_space_init(&address_space_io, system_io, "I/O");

    memory_listener_register(&core_memory_listener, &address_space_memory);
    memory_listener_register(&io_memory_listener, &address_space_io);
    memory_listener_register(&tcg_memory_listener, &address_space_memory);
}

MemoryRegion *get_system_memory(void)
{
    return system_memory;
}

MemoryRegion *get_system_io(void)
{
    return system_io;
}

#endif /* !defined(CONFIG_USER_ONLY) */

/* physical memory access (slow version, mainly for debug) */
#if defined(CONFIG_USER_ONLY)
int cpu_memory_rw_debug(CPUArchState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l, flags;
    target_ulong page;
    void * p;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return -1;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_WRITE, addr, l, 0)))
                return -1;
            memcpy(p, buf, l);
            unlock_user(p, addr, l);
        } else {
            if (!(flags & PAGE_READ))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_READ, addr, l, 1)))
                return -1;
            memcpy(buf, p, l);
            unlock_user(p, addr, 0);
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}

#else

static void invalidate_and_set_dirty(hwaddr addr,
                                     hwaddr length)
{
    if (!cpu_physical_memory_is_dirty(addr)) {
        /* invalidate code */
        tb_invalidate_phys_page_range(addr, addr + length, 0);
        /* set dirty bit */
        cpu_physical_memory_set_dirty_flags(addr, (0xff & ~CODE_DIRTY_FLAG));
    }
    xen_modified_memory(addr, length);
}

static inline bool memory_access_is_direct(MemoryRegion *mr, bool is_write)
{
    if (memory_region_is_ram(mr)) {
        return !(is_write && mr->readonly);
    }
    if (memory_region_is_romd(mr)) {
        return !is_write;
    }

    return false;
}

static inline int memory_access_size(MemoryRegion *mr, int l, hwaddr addr)
{
    if (l >= 4 && (((addr & 3) == 0 || mr->ops->impl.unaligned))) {
        return 4;
    }
    if (l >= 2 && (((addr & 1) == 0) || mr->ops->impl.unaligned)) {
        return 2;
    }
    return 1;
}

bool address_space_rw(AddressSpace *as, hwaddr addr, uint8_t *buf,
                      int len, bool is_write)
{
    hwaddr l;
    uint8_t *ptr;
    uint64_t val;
    hwaddr addr1;
    MemoryRegion *mr;
    bool error = false;

    while (len > 0) {
        l = len;
        mr = address_space_translate(as, addr, &addr1, &l, is_write);

        if (is_write) {
            if (!memory_access_is_direct(mr, is_write)) {
                l = memory_access_size(mr, l, addr1);
                /* XXX: could force cpu_single_env to NULL to avoid
                   potential bugs */
                if (l == 4) {
                    /* 32 bit write access */
                    val = ldl_p(buf);
                    error |= io_mem_write(mr, addr1, val, 4);
                } else if (l == 2) {
                    /* 16 bit write access */
                    val = lduw_p(buf);
                    error |= io_mem_write(mr, addr1, val, 2);
                } else {
                    /* 8 bit write access */
                    val = ldub_p(buf);
                    error |= io_mem_write(mr, addr1, val, 1);
                }
            } else {
                addr1 += memory_region_get_ram_addr(mr);
                /* RAM case */
                ptr = qemu_get_ram_ptr(addr1);
                memcpy(ptr, buf, l);
                invalidate_and_set_dirty(addr1, l);
            }
        } else {
            if (!memory_access_is_direct(mr, is_write)) {
                /* I/O case */
                l = memory_access_size(mr, l, addr1);
                if (l == 4) {
                    /* 32 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 4);
                    stl_p(buf, val);
                } else if (l == 2) {
                    /* 16 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 2);
                    stw_p(buf, val);
                } else {
                    /* 8 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 1);
                    stb_p(buf, val);
                }
            } else {
                /* RAM case */
                ptr = qemu_get_ram_ptr(mr->ram_addr + addr1);
                memcpy(buf, ptr, l);
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }

    return error;
}

bool address_space_write(AddressSpace *as, hwaddr addr,
                         const uint8_t *buf, int len)
{
    return address_space_rw(as, addr, (uint8_t *)buf, len, true);
}

bool address_space_read(AddressSpace *as, hwaddr addr, uint8_t *buf, int len)
{
    return address_space_rw(as, addr, buf, len, false);
}


void cpu_physical_memory_rw(hwaddr addr, uint8_t *buf,
                            int len, int is_write)
{
    address_space_rw(&address_space_memory, addr, buf, len, is_write);
}

/* used for ROM loading : can write in RAM and ROM */
void cpu_physical_memory_write_rom(hwaddr addr,
                                   const uint8_t *buf, int len)
{
    hwaddr l;
    uint8_t *ptr;
    hwaddr addr1;
    MemoryRegion *mr;

    while (len > 0) {
        l = len;
        mr = address_space_translate(&address_space_memory,
                                     addr, &addr1, &l, true);

        if (!(memory_region_is_ram(mr) ||
              memory_region_is_romd(mr))) {
            /* do nothing */
        } else {
            addr1 += memory_region_get_ram_addr(mr);
            /* ROM/RAM case */
            ptr = qemu_get_ram_ptr(addr1);
            memcpy(ptr, buf, l);
            invalidate_and_set_dirty(addr1, l);
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

typedef struct {
    void *buffer;
    hwaddr addr;
    hwaddr len;
} BounceBuffer;

static BounceBuffer bounce;

typedef struct MapClient {
    void *opaque;
    void (*callback)(void *opaque);
    QLIST_ENTRY(MapClient) link;
} MapClient;

static QLIST_HEAD(map_client_list, MapClient) map_client_list
    = QLIST_HEAD_INITIALIZER(map_client_list);

void *cpu_register_map_client(void *opaque, void (*callback)(void *opaque))
{
    MapClient *client = g_malloc(sizeof(*client));

    client->opaque = opaque;
    client->callback = callback;
    QLIST_INSERT_HEAD(&map_client_list, client, link);
    return client;
}

static void cpu_unregister_map_client(void *_client)
{
    MapClient *client = (MapClient *)_client;

    QLIST_REMOVE(client, link);
    g_free(client);
}

static void cpu_notify_map_clients(void)
{
    MapClient *client;

    while (!QLIST_EMPTY(&map_client_list)) {
        client = QLIST_FIRST(&map_client_list);
        client->callback(client->opaque);
        cpu_unregister_map_client(client);
    }
}

bool address_space_access_valid(AddressSpace *as, hwaddr addr, int len, bool is_write)
{
    MemoryRegion *mr;
    hwaddr l, xlat;

    while (len > 0) {
        l = len;
        mr = address_space_translate(as, addr, &xlat, &l, is_write);
        if (!memory_access_is_direct(mr, is_write)) {
            l = memory_access_size(mr, l, addr);
            if (!memory_region_access_valid(mr, xlat, l, is_write)) {
                return false;
            }
        }

        len -= l;
        addr += l;
    }
    return true;
}

/* Map a physical memory region into a host virtual address.
 * May map a subset of the requested range, given by and returned in *plen.
 * May return NULL if resources needed to perform the mapping are exhausted.
 * Use only for reads OR writes - not for read-modify-write operations.
 * Use cpu_register_map_client() to know when retrying the map operation is
 * likely to succeed.
 */
void *address_space_map(AddressSpace *as,
                        hwaddr addr,
                        hwaddr *plen,
                        bool is_write)
{
    hwaddr len = *plen;
    hwaddr todo = 0;
    hwaddr l, xlat;
    MemoryRegion *mr;
    ram_addr_t raddr = RAM_ADDR_MAX;
    ram_addr_t rlen;
    void *ret;

    while (len > 0) {
        l = len;
        mr = address_space_translate(as, addr, &xlat, &l, is_write);

        if (!memory_access_is_direct(mr, is_write)) {
            if (todo || bounce.buffer) {
                break;
            }
            bounce.buffer = qemu_memalign(TARGET_PAGE_SIZE, TARGET_PAGE_SIZE);
            bounce.addr = addr;
            bounce.len = l;
            if (!is_write) {
                address_space_read(as, addr, bounce.buffer, l);
            }

            *plen = l;
            return bounce.buffer;
        }
        if (!todo) {
            raddr = memory_region_get_ram_addr(mr) + xlat;
        } else {
            if (memory_region_get_ram_addr(mr) + xlat != raddr + todo) {
                break;
            }
        }

        len -= l;
        addr += l;
        todo += l;
    }
    rlen = todo;
    ret = qemu_ram_ptr_length(raddr, &rlen);
    *plen = rlen;
    return ret;
}

/* Unmaps a memory region previously mapped by address_space_map().
 * Will also mark the memory as dirty if is_write == 1.  access_len gives
 * the amount of memory that was actually read or written by the caller.
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         int is_write, hwaddr access_len)
{
    if (buffer != bounce.buffer) {
        if (is_write) {
            ram_addr_t addr1 = qemu_ram_addr_from_host_nofail(buffer);
            while (access_len) {
                unsigned l;
                l = TARGET_PAGE_SIZE;
                if (l > access_len)
                    l = access_len;
                invalidate_and_set_dirty(addr1, l);
                addr1 += l;
                access_len -= l;
            }
        }
        if (xen_enabled()) {
            xen_invalidate_map_cache_entry(buffer);
        }
        return;
    }
    if (is_write) {
        address_space_write(as, bounce.addr, bounce.buffer, access_len);
    }
    qemu_vfree(bounce.buffer);
    bounce.buffer = NULL;
    cpu_notify_map_clients();
}

void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              int is_write)
{
    return address_space_map(&address_space_memory, addr, plen, is_write);
}

void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               int is_write, hwaddr access_len)
{
    return address_space_unmap(&address_space_memory, buffer, len, is_write, access_len);
}

/* warning: addr must be aligned */
static inline uint32_t ldl_phys_internal(hwaddr addr,
                                         enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;

    mr = address_space_translate(&address_space_memory, addr, &addr1, &l,
                                 false);
    if (l < 4 || !memory_access_is_direct(mr, false)) {
        /* I/O case */
        io_mem_read(mr, addr1, &val, 4);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(mr)
                                & TARGET_PAGE_MASK)
                               + addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldl_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldl_be_p(ptr);
            break;
        default:
            val = ldl_p(ptr);
            break;
        }
    }
    return val;
}

uint32_t ldl_phys(hwaddr addr)
{
    return ldl_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t ldl_le_phys(hwaddr addr)
{
    return ldl_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t ldl_be_phys(hwaddr addr)
{
    return ldl_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned */
static inline uint64_t ldq_phys_internal(hwaddr addr,
                                         enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 8;
    hwaddr addr1;

    mr = address_space_translate(&address_space_memory, addr, &addr1, &l,
                                 false);
    if (l < 8 || !memory_access_is_direct(mr, false)) {
        /* I/O case */
        io_mem_read(mr, addr1, &val, 8);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap64(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap64(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(mr)
                                & TARGET_PAGE_MASK)
                               + addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = ldq_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = ldq_be_p(ptr);
            break;
        default:
            val = ldq_p(ptr);
            break;
        }
    }
    return val;
}

uint64_t ldq_phys(hwaddr addr)
{
    return ldq_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint64_t ldq_le_phys(hwaddr addr)
{
    return ldq_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint64_t ldq_be_phys(hwaddr addr)
{
    return ldq_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
uint32_t ldub_phys(hwaddr addr)
{
    uint8_t val;
    cpu_physical_memory_read(addr, &val, 1);
    return val;
}

/* warning: addr must be aligned */
static inline uint32_t lduw_phys_internal(hwaddr addr,
                                          enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 2;
    hwaddr addr1;

    mr = address_space_translate(&address_space_memory, addr, &addr1, &l,
                                 false);
    if (l < 2 || !memory_access_is_direct(mr, false)) {
        /* I/O case */
        io_mem_read(mr, addr1, &val, 2);
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap16(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap16(val);
        }
#endif
    } else {
        /* RAM case */
        ptr = qemu_get_ram_ptr((memory_region_get_ram_addr(mr)
                                & TARGET_PAGE_MASK)
                               + addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            val = lduw_le_p(ptr);
            break;
        case DEVICE_BIG_ENDIAN:
            val = lduw_be_p(ptr);
            break;
        default:
            val = lduw_p(ptr);
            break;
        }
    }
    return val;
}

uint32_t lduw_phys(hwaddr addr)
{
    return lduw_phys_internal(addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t lduw_le_phys(hwaddr addr)
{
    return lduw_phys_internal(addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t lduw_be_phys(hwaddr addr)
{
    return lduw_phys_internal(addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned. The ram page is not masked as dirty
   and the code inside is not invalidated. It is useful if the dirty
   bits are used to track modified PTEs */
void stl_phys_notdirty(hwaddr addr, uint32_t val)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;

    mr = address_space_translate(&address_space_memory, addr, &addr1, &l,
                                 true);
    if (l < 4 || !memory_access_is_direct(mr, true)) {
        io_mem_write(mr, addr1, val, 4);
    } else {
        addr1 += memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK;
        ptr = qemu_get_ram_ptr(addr1);
        stl_p(ptr, val);

        if (unlikely(in_migration)) {
            if (!cpu_physical_memory_is_dirty(addr1)) {
                /* invalidate code */
                tb_invalidate_phys_page_range(addr1, addr1 + 4, 0);
                /* set dirty bit */
                cpu_physical_memory_set_dirty_flags(
                    addr1, (0xff & ~CODE_DIRTY_FLAG));
            }
        }
    }
}

/* warning: addr must be aligned */
static inline void stl_phys_internal(hwaddr addr, uint32_t val,
                                     enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;

    mr = address_space_translate(&address_space_memory, addr, &addr1, &l,
                                 true);
    if (l < 4 || !memory_access_is_direct(mr, true)) {
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap32(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap32(val);
        }
#endif
        io_mem_write(mr, addr1, val, 4);
    } else {
        /* RAM case */
        addr1 += memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK;
        ptr = qemu_get_ram_ptr(addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stl_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stl_be_p(ptr, val);
            break;
        default:
            stl_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(addr1, 4);
    }
}

void stl_phys(hwaddr addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_NATIVE_ENDIAN);
}

void stl_le_phys(hwaddr addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_LITTLE_ENDIAN);
}

void stl_be_phys(hwaddr addr, uint32_t val)
{
    stl_phys_internal(addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stb_phys(hwaddr addr, uint32_t val)
{
    uint8_t v = val;
    cpu_physical_memory_write(addr, &v, 1);
}

/* warning: addr must be aligned */
static inline void stw_phys_internal(hwaddr addr, uint32_t val,
                                     enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 2;
    hwaddr addr1;

    mr = address_space_translate(&address_space_memory, addr, &addr1, &l,
                                 true);
    if (l < 2 || !memory_access_is_direct(mr, true)) {
#if defined(TARGET_WORDS_BIGENDIAN)
        if (endian == DEVICE_LITTLE_ENDIAN) {
            val = bswap16(val);
        }
#else
        if (endian == DEVICE_BIG_ENDIAN) {
            val = bswap16(val);
        }
#endif
        io_mem_write(mr, addr1, val, 2);
    } else {
        /* RAM case */
        addr1 += memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK;
        ptr = qemu_get_ram_ptr(addr1);
        switch (endian) {
        case DEVICE_LITTLE_ENDIAN:
            stw_le_p(ptr, val);
            break;
        case DEVICE_BIG_ENDIAN:
            stw_be_p(ptr, val);
            break;
        default:
            stw_p(ptr, val);
            break;
        }
        invalidate_and_set_dirty(addr1, 2);
    }
}

void stw_phys(hwaddr addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_NATIVE_ENDIAN);
}

void stw_le_phys(hwaddr addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_LITTLE_ENDIAN);
}

void stw_be_phys(hwaddr addr, uint32_t val)
{
    stw_phys_internal(addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stq_phys(hwaddr addr, uint64_t val)
{
    val = tswap64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

void stq_le_phys(hwaddr addr, uint64_t val)
{
    val = cpu_to_le64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

void stq_be_phys(hwaddr addr, uint64_t val)
{
    val = cpu_to_be64(val);
    cpu_physical_memory_write(addr, &val, 8);
}

/* virtual memory access for debug (includes writing to ROM) */
int cpu_memory_rw_debug(CPUArchState *env, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l;
    hwaddr phys_addr;
    target_ulong page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_debug(env, page);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        phys_addr += (addr & ~TARGET_PAGE_MASK);
        if (is_write)
            cpu_physical_memory_write_rom(phys_addr, buf, l);
        else
            cpu_physical_memory_rw(phys_addr, buf, l, is_write);
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}
#endif

#if !defined(CONFIG_USER_ONLY)

/*
 * A helper function for the _utterly broken_ virtio device model to find out if
 * it's running on a big endian machine. Don't do this at home kids!
 */
bool virtio_is_big_endian(void);
bool virtio_is_big_endian(void)
{
#if defined(TARGET_WORDS_BIGENDIAN)
    return true;
#else
    return false;
#endif
}

#endif

#ifndef CONFIG_USER_ONLY
bool cpu_physical_memory_is_io(hwaddr phys_addr)
{
    MemoryRegion*mr;
    hwaddr l = 1;

    mr = address_space_translate(&address_space_memory,
                                 phys_addr, &phys_addr, &l, false);

    return !(memory_region_is_ram(mr) ||
             memory_region_is_romd(mr));
}

void qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque)
{
    RAMBlock *block;

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        func(block->host, block->offset, block->length, opaque);
    }
}
#endif
