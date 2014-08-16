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
#ifndef _WIN32
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
#include "sysemu/sysemu.h"
#include "hw/xen/xen.h"
#include "qemu/timer.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
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
#include "exec/ram_addr.h"

#include "qemu/range.h"

//#define DEBUG_SUBPAGE

#if !defined(CONFIG_USER_ONLY)
static bool in_migration;

RAMList ram_list = { .blocks = QTAILQ_HEAD_INITIALIZER(ram_list.blocks) };

static MemoryRegion *system_memory;
static MemoryRegion *system_io;

AddressSpace address_space_io;
AddressSpace address_space_memory;

MemoryRegion io_mem_rom, io_mem_notdirty;
static MemoryRegion io_mem_unassigned;

/* RAM is pre-allocated and passed into qemu_ram_alloc_from_ptr */
#define RAM_PREALLOC   (1 << 0)

/* RAM is mmap-ed with MAP_SHARED */
#define RAM_SHARED     (1 << 1)

#endif

struct CPUTailQ cpus = QTAILQ_HEAD_INITIALIZER(cpus);
/* current CPU in the current thread. It is only valid inside
   cpu_exec() */
DEFINE_TLS(CPUState *, current_cpu);
/* 0 = Do not count executed instructions.
   1 = Precise instruction counting.
   2 = Adaptive rate instruction counting.  */
int use_icount;

#if !defined(CONFIG_USER_ONLY)

typedef struct PhysPageEntry PhysPageEntry;

struct PhysPageEntry {
    /* How many bits skip to next level (in units of L2_SIZE). 0 for a leaf. */
    uint32_t skip : 6;
     /* index into phys_sections (!skip) or phys_map_nodes (skip) */
    uint32_t ptr : 26;
};

#define PHYS_MAP_NODE_NIL (((uint32_t)~0) >> 6)

/* Size of the L2 (and L3, etc) page tables.  */
#define ADDR_SPACE_BITS 64

#define P_L2_BITS 9
#define P_L2_SIZE (1 << P_L2_BITS)

#define P_L2_LEVELS (((ADDR_SPACE_BITS - TARGET_PAGE_BITS - 1) / P_L2_BITS) + 1)

typedef PhysPageEntry Node[P_L2_SIZE];

typedef struct PhysPageMap {
    unsigned sections_nb;
    unsigned sections_nb_alloc;
    unsigned nodes_nb;
    unsigned nodes_nb_alloc;
    Node *nodes;
    MemoryRegionSection *sections;
} PhysPageMap;

struct AddressSpaceDispatch {
    /* This is a multi-level map on the physical address space.
     * The bottom level has pointers to MemoryRegionSections.
     */
    PhysPageEntry phys_map;
    PhysPageMap map;
    AddressSpace *as;
};

#define SUBPAGE_IDX(addr) ((addr) & ~TARGET_PAGE_MASK)
typedef struct subpage_t {
    MemoryRegion iomem;
    AddressSpace *as;
    hwaddr base;
    uint16_t sub_section[TARGET_PAGE_SIZE];
} subpage_t;

#define PHYS_SECTION_UNASSIGNED 0
#define PHYS_SECTION_NOTDIRTY 1
#define PHYS_SECTION_ROM 2
#define PHYS_SECTION_WATCH 3

static void io_mem_init(void);
static void memory_map_init(void);
static void tcg_commit(MemoryListener *listener);

static MemoryRegion io_mem_watch;
#endif

#if !defined(CONFIG_USER_ONLY)

static void phys_map_node_reserve(PhysPageMap *map, unsigned nodes)
{
    if (map->nodes_nb + nodes > map->nodes_nb_alloc) {
        map->nodes_nb_alloc = MAX(map->nodes_nb_alloc * 2, 16);
        map->nodes_nb_alloc = MAX(map->nodes_nb_alloc, map->nodes_nb + nodes);
        map->nodes = g_renew(Node, map->nodes, map->nodes_nb_alloc);
    }
}

static uint32_t phys_map_node_alloc(PhysPageMap *map)
{
    unsigned i;
    uint32_t ret;

    ret = map->nodes_nb++;
    assert(ret != PHYS_MAP_NODE_NIL);
    assert(ret != map->nodes_nb_alloc);
    for (i = 0; i < P_L2_SIZE; ++i) {
        map->nodes[ret][i].skip = 1;
        map->nodes[ret][i].ptr = PHYS_MAP_NODE_NIL;
    }
    return ret;
}

static void phys_page_set_level(PhysPageMap *map, PhysPageEntry *lp,
                                hwaddr *index, hwaddr *nb, uint16_t leaf,
                                int level)
{
    PhysPageEntry *p;
    int i;
    hwaddr step = (hwaddr)1 << (level * P_L2_BITS);

    if (lp->skip && lp->ptr == PHYS_MAP_NODE_NIL) {
        lp->ptr = phys_map_node_alloc(map);
        p = map->nodes[lp->ptr];
        if (level == 0) {
            for (i = 0; i < P_L2_SIZE; i++) {
                p[i].skip = 0;
                p[i].ptr = PHYS_SECTION_UNASSIGNED;
            }
        }
    } else {
        p = map->nodes[lp->ptr];
    }
    lp = &p[(*index >> (level * P_L2_BITS)) & (P_L2_SIZE - 1)];

    while (*nb && lp < &p[P_L2_SIZE]) {
        if ((*index & (step - 1)) == 0 && *nb >= step) {
            lp->skip = 0;
            lp->ptr = leaf;
            *index += step;
            *nb -= step;
        } else {
            phys_page_set_level(map, lp, index, nb, leaf, level - 1);
        }
        ++lp;
    }
}

static void phys_page_set(AddressSpaceDispatch *d,
                          hwaddr index, hwaddr nb,
                          uint16_t leaf)
{
    /* Wildly overreserve - it doesn't matter much. */
    phys_map_node_reserve(&d->map, 3 * P_L2_LEVELS);

    phys_page_set_level(&d->map, &d->phys_map, &index, &nb, leaf, P_L2_LEVELS - 1);
}

/* Compact a non leaf page entry. Simply detect that the entry has a single child,
 * and update our entry so we can skip it and go directly to the destination.
 */
static void phys_page_compact(PhysPageEntry *lp, Node *nodes, unsigned long *compacted)
{
    unsigned valid_ptr = P_L2_SIZE;
    int valid = 0;
    PhysPageEntry *p;
    int i;

    if (lp->ptr == PHYS_MAP_NODE_NIL) {
        return;
    }

    p = nodes[lp->ptr];
    for (i = 0; i < P_L2_SIZE; i++) {
        if (p[i].ptr == PHYS_MAP_NODE_NIL) {
            continue;
        }

        valid_ptr = i;
        valid++;
        if (p[i].skip) {
            phys_page_compact(&p[i], nodes, compacted);
        }
    }

    /* We can only compress if there's only one child. */
    if (valid != 1) {
        return;
    }

    assert(valid_ptr < P_L2_SIZE);

    /* Don't compress if it won't fit in the # of bits we have. */
    if (lp->skip + p[valid_ptr].skip >= (1 << 3)) {
        return;
    }

    lp->ptr = p[valid_ptr].ptr;
    if (!p[valid_ptr].skip) {
        /* If our only child is a leaf, make this a leaf. */
        /* By design, we should have made this node a leaf to begin with so we
         * should never reach here.
         * But since it's so simple to handle this, let's do it just in case we
         * change this rule.
         */
        lp->skip = 0;
    } else {
        lp->skip += p[valid_ptr].skip;
    }
}

static void phys_page_compact_all(AddressSpaceDispatch *d, int nodes_nb)
{
    DECLARE_BITMAP(compacted, nodes_nb);

    if (d->phys_map.skip) {
        phys_page_compact(&d->phys_map, d->map.nodes, compacted);
    }
}

static MemoryRegionSection *phys_page_find(PhysPageEntry lp, hwaddr addr,
                                           Node *nodes, MemoryRegionSection *sections)
{
    PhysPageEntry *p;
    hwaddr index = addr >> TARGET_PAGE_BITS;
    int i;

    for (i = P_L2_LEVELS; lp.skip && (i -= lp.skip) >= 0;) {
        if (lp.ptr == PHYS_MAP_NODE_NIL) {
            return &sections[PHYS_SECTION_UNASSIGNED];
        }
        p = nodes[lp.ptr];
        lp = p[(index >> (i * P_L2_BITS)) & (P_L2_SIZE - 1)];
    }

    if (sections[lp.ptr].size.hi ||
        range_covers_byte(sections[lp.ptr].offset_within_address_space,
                          sections[lp.ptr].size.lo, addr)) {
        return &sections[lp.ptr];
    } else {
        return &sections[PHYS_SECTION_UNASSIGNED];
    }
}

bool memory_region_is_unassigned(MemoryRegion *mr)
{
    return mr != &io_mem_rom && mr != &io_mem_notdirty && !mr->rom_device
        && mr != &io_mem_watch;
}

static MemoryRegionSection *address_space_lookup_region(AddressSpaceDispatch *d,
                                                        hwaddr addr,
                                                        bool resolve_subpage)
{
    MemoryRegionSection *section;
    subpage_t *subpage;

    section = phys_page_find(d->phys_map, addr, d->map.nodes, d->map.sections);
    if (resolve_subpage && section->mr->subpage) {
        subpage = container_of(section->mr, subpage_t, iomem);
        section = &d->map.sections[subpage->sub_section[SUBPAGE_IDX(addr)]];
    }
    return section;
}

static MemoryRegionSection *
address_space_translate_internal(AddressSpaceDispatch *d, hwaddr addr, hwaddr *xlat,
                                 hwaddr *plen, bool resolve_subpage)
{
    MemoryRegionSection *section;
    Int128 diff;

    section = address_space_lookup_region(d, addr, resolve_subpage);
    /* Compute offset within MemoryRegionSection */
    addr -= section->offset_within_address_space;

    /* Compute offset within MemoryRegion */
    *xlat = addr + section->offset_within_region;

    diff = int128_sub(section->mr->size, int128_make64(addr));
    *plen = int128_get64(int128_min(diff, int128_make64(*plen)));
    return section;
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

MemoryRegion *address_space_translate(AddressSpace *as, hwaddr addr,
                                      hwaddr *xlat, hwaddr *plen,
                                      bool is_write)
{
    IOMMUTLBEntry iotlb;
    MemoryRegionSection *section;
    MemoryRegion *mr;
    hwaddr len = *plen;

    for (;;) {
        section = address_space_translate_internal(as->dispatch, addr, &addr, plen, true);
        mr = section->mr;

        if (!mr->iommu_ops) {
            break;
        }

        iotlb = mr->iommu_ops->translate(mr, addr, is_write);
        addr = ((iotlb.translated_addr & ~iotlb.addr_mask)
                | (addr & iotlb.addr_mask));
        len = MIN(len, (addr | iotlb.addr_mask) - addr + 1);
        if (!(iotlb.perm & (1 << is_write))) {
            mr = &io_mem_unassigned;
            break;
        }

        as = iotlb.target_as;
    }

    if (xen_enabled() && memory_access_is_direct(mr, is_write)) {
        hwaddr page = ((addr & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE) - addr;
        len = MIN(page, len);
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
    section = address_space_translate_internal(as->dispatch, addr, xlat, plen, false);

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
    tlb_flush(cpu, 1);

    return 0;
}

const VMStateDescription vmstate_cpu_common = {
    .name = "cpu_common",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = cpu_common_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(halted, CPUState),
        VMSTATE_UINT32(interrupt_request, CPUState),
        VMSTATE_END_OF_LIST()
    }
};

#endif

CPUState *qemu_get_cpu(int index)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (cpu->cpu_index == index) {
            return cpu;
        }
    }

    return NULL;
}

#if !defined(CONFIG_USER_ONLY)
void tcg_cpu_address_space_init(CPUState *cpu, AddressSpace *as)
{
    /* We only support one address space per cpu at the moment.  */
    assert(cpu->as == as);

    if (cpu->tcg_as_listener) {
        memory_listener_unregister(cpu->tcg_as_listener);
    } else {
        cpu->tcg_as_listener = g_new0(MemoryListener, 1);
    }
    cpu->tcg_as_listener->commit = tcg_commit;
    memory_listener_register(cpu->tcg_as_listener, as);
}
#endif

void cpu_exec_init(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUState *some_cpu;
    int cpu_index;

#if defined(CONFIG_USER_ONLY)
    cpu_list_lock();
#endif
    cpu_index = 0;
    CPU_FOREACH(some_cpu) {
        cpu_index++;
    }
    cpu->cpu_index = cpu_index;
    cpu->numa_node = 0;
    QTAILQ_INIT(&cpu->breakpoints);
    QTAILQ_INIT(&cpu->watchpoints);
#ifndef CONFIG_USER_ONLY
    cpu->as = &address_space_memory;
    cpu->thread_id = qemu_get_thread_id();
#endif
    QTAILQ_INSERT_TAIL(&cpus, cpu, node);
#if defined(CONFIG_USER_ONLY)
    cpu_list_unlock();
#endif
    if (qdev_get_vmsd(DEVICE(cpu)) == NULL) {
        vmstate_register(NULL, cpu_index, &vmstate_cpu_common, cpu);
    }
#if defined(CPU_SAVE_VERSION) && !defined(CONFIG_USER_ONLY)
    register_savevm(NULL, "cpu", cpu_index, CPU_SAVE_VERSION,
                    cpu_save, cpu_load, env);
    assert(cc->vmsd == NULL);
    assert(qdev_get_vmsd(DEVICE(cpu)) == NULL);
#endif
    if (cc->vmsd != NULL) {
        vmstate_register(NULL, cpu_index, cc->vmsd, cpu);
    }
}

#if defined(TARGET_HAS_ICE)
#if defined(CONFIG_USER_ONLY)
static void breakpoint_invalidate(CPUState *cpu, target_ulong pc)
{
    tb_invalidate_phys_page_range(pc, pc + 1, 0);
}
#else
static void breakpoint_invalidate(CPUState *cpu, target_ulong pc)
{
    hwaddr phys = cpu_get_phys_page_debug(cpu, pc);
    if (phys != -1) {
        tb_invalidate_phys_addr(cpu->as,
                                phys | (pc & ~TARGET_PAGE_MASK));
    }
}
#endif
#endif /* TARGET_HAS_ICE */

#if defined(CONFIG_USER_ONLY)
void cpu_watchpoint_remove_all(CPUState *cpu, int mask)

{
}

int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint)
{
    return -ENOSYS;
}
#else
/* Add a watchpoint.  */
int cpu_watchpoint_insert(CPUState *cpu, vaddr addr, vaddr len,
                          int flags, CPUWatchpoint **watchpoint)
{
    vaddr len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    /* sanity checks: allow power-of-2 lengths, deny unaligned watchpoints */
    if ((len & (len - 1)) || (addr & ~len_mask) ||
            len == 0 || len > TARGET_PAGE_SIZE) {
        error_report("tried to set invalid watchpoint at %"
                     VADDR_PRIx ", len=%" VADDR_PRIu, addr, len);
        return -EINVAL;
    }
    wp = g_malloc(sizeof(*wp));

    wp->vaddr = addr;
    wp->len_mask = len_mask;
    wp->flags = flags;

    /* keep all GDB-injected watchpoints in front */
    if (flags & BP_GDB) {
        QTAILQ_INSERT_HEAD(&cpu->watchpoints, wp, entry);
    } else {
        QTAILQ_INSERT_TAIL(&cpu->watchpoints, wp, entry);
    }

    tlb_flush_page(cpu, addr);

    if (watchpoint)
        *watchpoint = wp;
    return 0;
}

/* Remove a specific watchpoint.  */
int cpu_watchpoint_remove(CPUState *cpu, vaddr addr, vaddr len,
                          int flags)
{
    vaddr len_mask = ~(len - 1);
    CPUWatchpoint *wp;

    QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
        if (addr == wp->vaddr && len_mask == wp->len_mask
                && flags == (wp->flags & ~BP_WATCHPOINT_HIT)) {
            cpu_watchpoint_remove_by_ref(cpu, wp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific watchpoint by reference.  */
void cpu_watchpoint_remove_by_ref(CPUState *cpu, CPUWatchpoint *watchpoint)
{
    QTAILQ_REMOVE(&cpu->watchpoints, watchpoint, entry);

    tlb_flush_page(cpu, watchpoint->vaddr);

    g_free(watchpoint);
}

/* Remove all matching watchpoints.  */
void cpu_watchpoint_remove_all(CPUState *cpu, int mask)
{
    CPUWatchpoint *wp, *next;

    QTAILQ_FOREACH_SAFE(wp, &cpu->watchpoints, entry, next) {
        if (wp->flags & mask) {
            cpu_watchpoint_remove_by_ref(cpu, wp);
        }
    }
}
#endif

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUState *cpu, vaddr pc, int flags,
                          CPUBreakpoint **breakpoint)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    bp = g_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB) {
        QTAILQ_INSERT_HEAD(&cpu->breakpoints, bp, entry);
    } else {
        QTAILQ_INSERT_TAIL(&cpu->breakpoints, bp, entry);
    }

    breakpoint_invalidate(cpu, pc);

    if (breakpoint) {
        *breakpoint = bp;
    }
    return 0;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUState *cpu, vaddr pc, int flags)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp;

    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(cpu, bp);
            return 0;
        }
    }
    return -ENOENT;
#else
    return -ENOSYS;
#endif
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUState *cpu, CPUBreakpoint *breakpoint)
{
#if defined(TARGET_HAS_ICE)
    QTAILQ_REMOVE(&cpu->breakpoints, breakpoint, entry);

    breakpoint_invalidate(cpu, breakpoint->pc);

    g_free(breakpoint);
#endif
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUState *cpu, int mask)
{
#if defined(TARGET_HAS_ICE)
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &cpu->breakpoints, entry, next) {
        if (bp->flags & mask) {
            cpu_breakpoint_remove_by_ref(cpu, bp);
        }
    }
#endif
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *cpu, int enabled)
{
#if defined(TARGET_HAS_ICE)
    if (cpu->singlestep_enabled != enabled) {
        cpu->singlestep_enabled = enabled;
        if (kvm_enabled()) {
            kvm_update_guest_debug(cpu, 0);
        } else {
            /* must flush all the translated code to avoid inconsistencies */
            /* XXX: only flush what is necessary */
            CPUArchState *env = cpu->env_ptr;
            tb_flush(env);
        }
    }
#endif
}

void cpu_abort(CPUState *cpu, const char *fmt, ...)
{
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
        log_cpu_state(cpu, CPU_DUMP_FPU | CPU_DUMP_CCOP);
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

#if !defined(CONFIG_USER_ONLY)
static RAMBlock *qemu_get_ram_block(ram_addr_t addr)
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
    return block;
}

static void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t length)
{
    ram_addr_t start1;
    RAMBlock *block;
    ram_addr_t end;

    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;

    block = qemu_get_ram_block(start);
    assert(block == qemu_get_ram_block(end - 1));
    start1 = (uintptr_t)block->host + (start - block->offset);
    cpu_tlb_reset_dirty_all(start1, length);
}

/* Note: start and end must be within the same ram block.  */
void cpu_physical_memory_reset_dirty(ram_addr_t start, ram_addr_t length,
                                     unsigned client)
{
    if (length == 0)
        return;
    cpu_physical_memory_clear_dirty_range(start, length, client);

    if (tcg_enabled()) {
        tlb_reset_dirty_range_all(start, length);
    }
}

static void cpu_physical_memory_set_dirty_tracking(bool enable)
{
    in_migration = enable;
}

hwaddr memory_region_section_get_iotlb(CPUState *cpu,
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
            iotlb |= PHYS_SECTION_NOTDIRTY;
        } else {
            iotlb |= PHYS_SECTION_ROM;
        }
    } else {
        iotlb = section - section->address_space->dispatch->map.sections;
        iotlb += xlat;
    }

    /* Make accesses to pages with watchpoints go via the
       watchpoint trap routines.  */
    QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
        if (vaddr == (wp->vaddr & TARGET_PAGE_MASK)) {
            /* Avoid trapping reads of pages with a write breakpoint. */
            if ((prot & PAGE_WRITE) || (wp->flags & BP_MEM_READ)) {
                iotlb = PHYS_SECTION_WATCH + paddr;
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

static void *(*phys_mem_alloc)(size_t size) = qemu_anon_ram_alloc;

/*
 * Set a custom physical guest memory alloator.
 * Accelerators with unusual needs may need this.  Hopefully, we can
 * get rid of it eventually.
 */
void phys_mem_set_alloc(void *(*alloc)(size_t))
{
    phys_mem_alloc = alloc;
}

static uint16_t phys_section_add(PhysPageMap *map,
                                 MemoryRegionSection *section)
{
    /* The physical section number is ORed with a page-aligned
     * pointer to produce the iotlb entries.  Thus it should
     * never overflow into the page-aligned value.
     */
    assert(map->sections_nb < TARGET_PAGE_SIZE);

    if (map->sections_nb == map->sections_nb_alloc) {
        map->sections_nb_alloc = MAX(map->sections_nb_alloc * 2, 16);
        map->sections = g_renew(MemoryRegionSection, map->sections,
                                map->sections_nb_alloc);
    }
    map->sections[map->sections_nb] = *section;
    memory_region_ref(section->mr);
    return map->sections_nb++;
}

static void phys_section_destroy(MemoryRegion *mr)
{
    memory_region_unref(mr);

    if (mr->subpage) {
        subpage_t *subpage = container_of(mr, subpage_t, iomem);
        object_unref(OBJECT(&subpage->iomem));
        g_free(subpage);
    }
}

static void phys_sections_free(PhysPageMap *map)
{
    while (map->sections_nb > 0) {
        MemoryRegionSection *section = &map->sections[--map->sections_nb];
        phys_section_destroy(section->mr);
    }
    g_free(map->sections);
    g_free(map->nodes);
}

static void register_subpage(AddressSpaceDispatch *d, MemoryRegionSection *section)
{
    subpage_t *subpage;
    hwaddr base = section->offset_within_address_space
        & TARGET_PAGE_MASK;
    MemoryRegionSection *existing = phys_page_find(d->phys_map, base,
                                                   d->map.nodes, d->map.sections);
    MemoryRegionSection subsection = {
        .offset_within_address_space = base,
        .size = int128_make64(TARGET_PAGE_SIZE),
    };
    hwaddr start, end;

    assert(existing->mr->subpage || existing->mr == &io_mem_unassigned);

    if (!(existing->mr->subpage)) {
        subpage = subpage_init(d->as, base);
        subsection.address_space = d->as;
        subsection.mr = &subpage->iomem;
        phys_page_set(d, base >> TARGET_PAGE_BITS, 1,
                      phys_section_add(&d->map, &subsection));
    } else {
        subpage = container_of(existing->mr, subpage_t, iomem);
    }
    start = section->offset_within_address_space & ~TARGET_PAGE_MASK;
    end = start + int128_get64(section->size) - 1;
    subpage_register(subpage, start, end,
                     phys_section_add(&d->map, section));
}


static void register_multipage(AddressSpaceDispatch *d,
                               MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    uint16_t section_index = phys_section_add(&d->map, section);
    uint64_t num_pages = int128_get64(int128_rshift(section->size,
                                                    TARGET_PAGE_BITS));

    assert(num_pages);
    phys_page_set(d, start_addr >> TARGET_PAGE_BITS, num_pages, section_index);
}

static void mem_add(MemoryListener *listener, MemoryRegionSection *section)
{
    AddressSpace *as = container_of(listener, AddressSpace, dispatch_listener);
    AddressSpaceDispatch *d = as->next_dispatch;
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
        } else if (remain.offset_within_address_space & ~TARGET_PAGE_MASK) {
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

#ifdef __linux__

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
                            const char *path,
                            Error **errp)
{
    char *filename;
    char *sanitized_name;
    char *c;
    void *area;
    int fd;
    unsigned long hpagesize;

    hpagesize = gethugepagesize(path);
    if (!hpagesize) {
        goto error;
    }

    if (memory < hpagesize) {
        return NULL;
    }

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        error_setg(errp,
                   "host lacks kvm mmu notifiers, -mem-path unsupported");
        goto error;
    }

    /* Make name safe to use with mkstemp by replacing '/' with '_'. */
    sanitized_name = g_strdup(memory_region_name(block->mr));
    for (c = sanitized_name; *c != '\0'; c++) {
        if (*c == '/')
            *c = '_';
    }

    filename = g_strdup_printf("%s/qemu_back_mem.%s.XXXXXX", path,
                               sanitized_name);
    g_free(sanitized_name);

    fd = mkstemp(filename);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "unable to create backing store for hugepages");
        g_free(filename);
        goto error;
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
    if (ftruncate(fd, memory)) {
        perror("ftruncate");
    }

    area = mmap(0, memory, PROT_READ | PROT_WRITE,
                (block->flags & RAM_SHARED ? MAP_SHARED : MAP_PRIVATE),
                fd, 0);
    if (area == MAP_FAILED) {
        error_setg_errno(errp, errno,
                         "unable to map backing store for hugepages");
        close(fd);
        goto error;
    }

    if (mem_prealloc) {
        os_mem_prealloc(fd, area, memory);
    }

    block->fd = fd;
    return area;

error:
    if (mem_prealloc) {
        exit(1);
    }
    return NULL;
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

    /* Use MADV_DONTDUMP, if user doesn't want the guest memory in the core */
    if (!qemu_opt_get_bool(qemu_get_machine_opts(),
                           "dump-guest-core", true)) {
        ret = qemu_madvise(addr, size, QEMU_MADV_DONTDUMP);
        if (ret) {
            perror("qemu_madvise");
            fprintf(stderr, "madvise doesn't support MADV_DONTDUMP, "
                            "but dump_guest_core=off specified\n");
        }
    }
}

static RAMBlock *find_ram_block(ram_addr_t addr)
{
    RAMBlock *block;

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        if (block->offset == addr) {
            return block;
        }
    }

    return NULL;
}

void qemu_ram_set_idstr(ram_addr_t addr, const char *name, DeviceState *dev)
{
    RAMBlock *new_block = find_ram_block(addr);
    RAMBlock *block;

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

void qemu_ram_unset_idstr(ram_addr_t addr)
{
    RAMBlock *block = find_ram_block(addr);

    if (block) {
        memset(block->idstr, 0, sizeof(block->idstr));
    }
}

static int memory_try_enable_merging(void *addr, size_t len)
{
    if (!qemu_opt_get_bool(qemu_get_machine_opts(), "mem-merge", true)) {
        /* disabled by the user */
        return 0;
    }

    return qemu_madvise(addr, len, QEMU_MADV_MERGEABLE);
}

static ram_addr_t ram_block_add(RAMBlock *new_block)
{
    RAMBlock *block;
    ram_addr_t old_ram_size, new_ram_size;

    old_ram_size = last_ram_offset() >> TARGET_PAGE_BITS;

    /* This assumes the iothread lock is taken here too.  */
    qemu_mutex_lock_ramlist();
    new_block->offset = find_ram_offset(new_block->length);

    if (!new_block->host) {
        if (xen_enabled()) {
            xen_ram_alloc(new_block->offset, new_block->length, new_block->mr);
        } else {
            new_block->host = phys_mem_alloc(new_block->length);
            if (!new_block->host) {
                fprintf(stderr, "Cannot set up guest memory '%s': %s\n",
                        memory_region_name(new_block->mr), strerror(errno));
                exit(1);
            }
            memory_try_enable_merging(new_block->host, new_block->length);
        }
    }

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

    new_ram_size = last_ram_offset() >> TARGET_PAGE_BITS;

    if (new_ram_size > old_ram_size) {
        int i;
        for (i = 0; i < DIRTY_MEMORY_NUM; i++) {
            ram_list.dirty_memory[i] =
                bitmap_zero_extend(ram_list.dirty_memory[i],
                                   old_ram_size, new_ram_size);
       }
    }
    cpu_physical_memory_set_dirty_range(new_block->offset, new_block->length);

    qemu_ram_setup_dump(new_block->host, new_block->length);
    qemu_madvise(new_block->host, new_block->length, QEMU_MADV_HUGEPAGE);
    qemu_madvise(new_block->host, new_block->length, QEMU_MADV_DONTFORK);

    if (kvm_enabled()) {
        kvm_setup_guest_memory(new_block->host, new_block->length);
    }

    return new_block->offset;
}

#ifdef __linux__
ram_addr_t qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                    bool share, const char *mem_path,
                                    Error **errp)
{
    RAMBlock *new_block;

    if (xen_enabled()) {
        error_setg(errp, "-mem-path not supported with Xen");
        return -1;
    }

    if (phys_mem_alloc != qemu_anon_ram_alloc) {
        /*
         * file_ram_alloc() needs to allocate just like
         * phys_mem_alloc, but we haven't bothered to provide
         * a hook there.
         */
        error_setg(errp,
                   "-mem-path not supported with this accelerator");
        return -1;
    }

    size = TARGET_PAGE_ALIGN(size);
    new_block = g_malloc0(sizeof(*new_block));
    new_block->mr = mr;
    new_block->length = size;
    new_block->flags = share ? RAM_SHARED : 0;
    new_block->host = file_ram_alloc(new_block, size,
                                     mem_path, errp);
    if (!new_block->host) {
        g_free(new_block);
        return -1;
    }

    return ram_block_add(new_block);
}
#endif

ram_addr_t qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr)
{
    RAMBlock *new_block;

    size = TARGET_PAGE_ALIGN(size);
    new_block = g_malloc0(sizeof(*new_block));
    new_block->mr = mr;
    new_block->length = size;
    new_block->fd = -1;
    new_block->host = host;
    if (host) {
        new_block->flags |= RAM_PREALLOC;
    }
    return ram_block_add(new_block);
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
            if (block->flags & RAM_PREALLOC) {
                ;
            } else if (xen_enabled()) {
                xen_invalidate_map_cache_entry(block->host);
#ifndef _WIN32
            } else if (block->fd >= 0) {
                munmap(block->host, block->length);
                close(block->fd);
#endif
            } else {
                qemu_anon_ram_free(block->host, block->length);
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
            if (block->flags & RAM_PREALLOC) {
                ;
            } else if (xen_enabled()) {
                abort();
            } else {
                flags = MAP_FIXED;
                munmap(vaddr, length);
                if (block->fd >= 0) {
                    flags |= (block->flags & RAM_SHARED ?
                              MAP_SHARED : MAP_PRIVATE);
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, block->fd, offset);
                } else {
                    /*
                     * Remap needs to match alloc.  Accelerators that
                     * set phys_mem_alloc never remap.  If they did,
                     * we'd need a remap hook here.
                     */
                    assert(phys_mem_alloc == qemu_anon_ram_alloc);

                    flags |= MAP_PRIVATE | MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, -1, 0);
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

int qemu_get_ram_fd(ram_addr_t addr)
{
    RAMBlock *block = qemu_get_ram_block(addr);

    return block->fd;
}

void *qemu_get_ram_block_host_ptr(ram_addr_t addr)
{
    RAMBlock *block = qemu_get_ram_block(addr);

    return block->host;
}

/* Return a host pointer to ram allocated with qemu_ram_alloc.
   With the exception of the softmmu code in this file, this should
   only be used for local memory (e.g. video ram) that the device owns,
   and knows it isn't going to access beyond the end of the block.

   It should not be used for general purpose DMA.
   Use cpu_physical_memory_map/cpu_physical_memory_rw instead.
 */
void *qemu_get_ram_ptr(ram_addr_t addr)
{
    RAMBlock *block = qemu_get_ram_block(addr);

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

/* Return a host pointer to guest's ram. Similar to qemu_get_ram_ptr
 * but takes a size argument */
static void *qemu_ram_ptr_length(ram_addr_t addr, hwaddr *size)
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

/* Some of the softmmu routines need to translate from a host pointer
   (typically a TLB entry) back to a ram offset.  */
MemoryRegion *qemu_ram_addr_from_host(void *ptr, ram_addr_t *ram_addr)
{
    RAMBlock *block;
    uint8_t *host = ptr;

    if (xen_enabled()) {
        *ram_addr = xen_ram_addr_from_mapcache(ptr);
        return qemu_get_ram_block(*ram_addr)->mr;
    }

    block = ram_list.mru_block;
    if (block && block->host && host - block->host < block->length) {
        goto found;
    }

    QTAILQ_FOREACH(block, &ram_list.blocks, next) {
        /* This case append when the block is not mapped. */
        if (block->host == NULL) {
            continue;
        }
        if (host - block->host < block->length) {
            goto found;
        }
    }

    return NULL;

found:
    *ram_addr = block->offset + (host - block->host);
    return block->mr;
}

static void notdirty_mem_write(void *opaque, hwaddr ram_addr,
                               uint64_t val, unsigned size)
{
    if (!cpu_physical_memory_get_dirty_flag(ram_addr, DIRTY_MEMORY_CODE)) {
        tb_invalidate_phys_page_fast(ram_addr, size);
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
    cpu_physical_memory_set_dirty_range_nocode(ram_addr, size);
    /* we remove the notdirty callback only if the code has been
       flushed */
    if (!cpu_physical_memory_is_clean(ram_addr)) {
        CPUArchState *env = current_cpu->env_ptr;
        tlb_set_dirty(env, current_cpu->mem_io_vaddr);
    }
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
    CPUState *cpu = current_cpu;
    CPUArchState *env = cpu->env_ptr;
    target_ulong pc, cs_base;
    target_ulong vaddr;
    CPUWatchpoint *wp;
    int cpu_flags;

    if (cpu->watchpoint_hit) {
        /* We re-entered the check after replacing the TB. Now raise
         * the debug interrupt so that is will trigger after the
         * current instruction. */
        cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);
        return;
    }
    vaddr = (cpu->mem_io_vaddr & TARGET_PAGE_MASK) + offset;
    QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
        if ((vaddr == (wp->vaddr & len_mask) ||
             (vaddr & wp->len_mask) == wp->vaddr) && (wp->flags & flags)) {
            wp->flags |= BP_WATCHPOINT_HIT;
            if (!cpu->watchpoint_hit) {
                cpu->watchpoint_hit = wp;
                tb_check_watchpoint(cpu);
                if (wp->flags & BP_STOP_BEFORE_ACCESS) {
                    cpu->exception_index = EXCP_DEBUG;
                    cpu_loop_exit(cpu);
                } else {
                    cpu_get_tb_cpu_state(env, &pc, &cs_base, &cpu_flags);
                    tb_gen_code(cpu, pc, cs_base, cpu_flags, 1);
                    cpu_resume_from_signal(cpu, NULL);
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
    case 1: return ldub_phys(&address_space_memory, addr);
    case 2: return lduw_phys(&address_space_memory, addr);
    case 4: return ldl_phys(&address_space_memory, addr);
    default: abort();
    }
}

static void watch_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    check_watchpoint(addr & ~TARGET_PAGE_MASK, ~(size - 1), BP_MEM_WRITE);
    switch (size) {
    case 1:
        stb_phys(&address_space_memory, addr, val);
        break;
    case 2:
        stw_phys(&address_space_memory, addr, val);
        break;
    case 4:
        stl_phys(&address_space_memory, addr, val);
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
    printf("%s: subpage %p len %u addr " TARGET_FMT_plx "\n", __func__,
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
    printf("%s: subpage %p len %u addr " TARGET_FMT_plx
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
                            unsigned len, bool is_write)
{
    subpage_t *subpage = opaque;
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p %c len %u addr " TARGET_FMT_plx "\n",
           __func__, subpage, is_write ? 'w' : 'r', len, addr);
#endif

    return address_space_access_valid(subpage->as, addr + subpage->base,
                                      len, is_write);
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
    printf("%s: %p start %08x end %08x idx %08x eidx %08x section %d\n",
           __func__, mmio, start, end, idx, eidx, section);
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
    memory_region_init_io(&mmio->iomem, NULL, &subpage_ops, mmio,
                          NULL, TARGET_PAGE_SIZE);
    mmio->iomem.subpage = true;
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p base " TARGET_FMT_plx " len %08x\n", __func__,
           mmio, base, TARGET_PAGE_SIZE);
#endif
    subpage_register(mmio, 0, TARGET_PAGE_SIZE-1, PHYS_SECTION_UNASSIGNED);

    return mmio;
}

static uint16_t dummy_section(PhysPageMap *map, AddressSpace *as,
                              MemoryRegion *mr)
{
    assert(as);
    MemoryRegionSection section = {
        .address_space = as,
        .mr = mr,
        .offset_within_address_space = 0,
        .offset_within_region = 0,
        .size = int128_2_64(),
    };

    return phys_section_add(map, &section);
}

MemoryRegion *iotlb_to_region(AddressSpace *as, hwaddr index)
{
    return as->dispatch->map.sections[index & ~TARGET_PAGE_MASK].mr;
}

static void io_mem_init(void)
{
    memory_region_init_io(&io_mem_rom, NULL, &unassigned_mem_ops, NULL, NULL, UINT64_MAX);
    memory_region_init_io(&io_mem_unassigned, NULL, &unassigned_mem_ops, NULL,
                          NULL, UINT64_MAX);
    memory_region_init_io(&io_mem_notdirty, NULL, &notdirty_mem_ops, NULL,
                          NULL, UINT64_MAX);
    memory_region_init_io(&io_mem_watch, NULL, &watch_mem_ops, NULL,
                          NULL, UINT64_MAX);
}

static void mem_begin(MemoryListener *listener)
{
    AddressSpace *as = container_of(listener, AddressSpace, dispatch_listener);
    AddressSpaceDispatch *d = g_new0(AddressSpaceDispatch, 1);
    uint16_t n;

    n = dummy_section(&d->map, as, &io_mem_unassigned);
    assert(n == PHYS_SECTION_UNASSIGNED);
    n = dummy_section(&d->map, as, &io_mem_notdirty);
    assert(n == PHYS_SECTION_NOTDIRTY);
    n = dummy_section(&d->map, as, &io_mem_rom);
    assert(n == PHYS_SECTION_ROM);
    n = dummy_section(&d->map, as, &io_mem_watch);
    assert(n == PHYS_SECTION_WATCH);

    d->phys_map  = (PhysPageEntry) { .ptr = PHYS_MAP_NODE_NIL, .skip = 1 };
    d->as = as;
    as->next_dispatch = d;
}

static void mem_commit(MemoryListener *listener)
{
    AddressSpace *as = container_of(listener, AddressSpace, dispatch_listener);
    AddressSpaceDispatch *cur = as->dispatch;
    AddressSpaceDispatch *next = as->next_dispatch;

    phys_page_compact_all(next, next->map.nodes_nb);

    as->dispatch = next;

    if (cur) {
        phys_sections_free(&cur->map);
        g_free(cur);
    }
}

static void tcg_commit(MemoryListener *listener)
{
    CPUState *cpu;

    /* since each CPU stores ram addresses in its TLB cache, we must
       reset the modified entries */
    /* XXX: slow ! */
    CPU_FOREACH(cpu) {
        /* FIXME: Disentangle the cpu.h circular files deps so we can
           directly get the right CPU from listener.  */
        if (cpu->tcg_as_listener != listener) {
            continue;
        }
        tlb_flush(cpu, 1);
    }
}

static void core_log_global_start(MemoryListener *listener)
{
    cpu_physical_memory_set_dirty_tracking(true);
}

static void core_log_global_stop(MemoryListener *listener)
{
    cpu_physical_memory_set_dirty_tracking(false);
}

static MemoryListener core_memory_listener = {
    .log_global_start = core_log_global_start,
    .log_global_stop = core_log_global_stop,
    .priority = 1,
};

void address_space_init_dispatch(AddressSpace *as)
{
    as->dispatch = NULL;
    as->dispatch_listener = (MemoryListener) {
        .begin = mem_begin,
        .commit = mem_commit,
        .region_add = mem_add,
        .region_nop = mem_add,
        .priority = 0,
    };
    memory_listener_register(&as->dispatch_listener, as);
}

void address_space_destroy_dispatch(AddressSpace *as)
{
    AddressSpaceDispatch *d = as->dispatch;

    memory_listener_unregister(&as->dispatch_listener);
    g_free(d);
    as->dispatch = NULL;
}

static void memory_map_init(void)
{
    system_memory = g_malloc(sizeof(*system_memory));

    memory_region_init(system_memory, NULL, "system", UINT64_MAX);
    address_space_init(&address_space_memory, system_memory, "memory");

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init_io(system_io, NULL, &unassigned_io_ops, NULL, "io",
                          65536);
    address_space_init(&address_space_io, system_io, "I/O");

    memory_listener_register(&core_memory_listener, &address_space_memory);
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
int cpu_memory_rw_debug(CPUState *cpu, target_ulong addr,
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
    if (cpu_physical_memory_is_clean(addr)) {
        /* invalidate code */
        tb_invalidate_phys_page_range(addr, addr + length, 0);
        /* set dirty bit */
        cpu_physical_memory_set_dirty_range_nocode(addr, length);
    }
    xen_modified_memory(addr, length);
}

static int memory_access_size(MemoryRegion *mr, unsigned l, hwaddr addr)
{
    unsigned access_size_max = mr->ops->valid.max_access_size;

    /* Regions are assumed to support 1-4 byte accesses unless
       otherwise specified.  */
    if (access_size_max == 0) {
        access_size_max = 4;
    }

    /* Bound the maximum access by the alignment of the address.  */
    if (!mr->ops->impl.unaligned) {
        unsigned align_size_max = addr & -addr;
        if (align_size_max != 0 && align_size_max < access_size_max) {
            access_size_max = align_size_max;
        }
    }

    /* Don't attempt accesses larger than the maximum.  */
    if (l > access_size_max) {
        l = access_size_max;
    }
    if (l & (l - 1)) {
        l = 1 << (qemu_fls(l) - 1);
    }

    return l;
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
                /* XXX: could force current_cpu to NULL to avoid
                   potential bugs */
                switch (l) {
                case 8:
                    /* 64 bit write access */
                    val = ldq_p(buf);
                    error |= io_mem_write(mr, addr1, val, 8);
                    break;
                case 4:
                    /* 32 bit write access */
                    val = ldl_p(buf);
                    error |= io_mem_write(mr, addr1, val, 4);
                    break;
                case 2:
                    /* 16 bit write access */
                    val = lduw_p(buf);
                    error |= io_mem_write(mr, addr1, val, 2);
                    break;
                case 1:
                    /* 8 bit write access */
                    val = ldub_p(buf);
                    error |= io_mem_write(mr, addr1, val, 1);
                    break;
                default:
                    abort();
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
                switch (l) {
                case 8:
                    /* 64 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 8);
                    stq_p(buf, val);
                    break;
                case 4:
                    /* 32 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 4);
                    stl_p(buf, val);
                    break;
                case 2:
                    /* 16 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 2);
                    stw_p(buf, val);
                    break;
                case 1:
                    /* 8 bit read access */
                    error |= io_mem_read(mr, addr1, &val, 1);
                    stb_p(buf, val);
                    break;
                default:
                    abort();
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

enum write_rom_type {
    WRITE_DATA,
    FLUSH_CACHE,
};

static inline void cpu_physical_memory_write_rom_internal(AddressSpace *as,
    hwaddr addr, const uint8_t *buf, int len, enum write_rom_type type)
{
    hwaddr l;
    uint8_t *ptr;
    hwaddr addr1;
    MemoryRegion *mr;

    while (len > 0) {
        l = len;
        mr = address_space_translate(as, addr, &addr1, &l, true);

        if (!(memory_region_is_ram(mr) ||
              memory_region_is_romd(mr))) {
            /* do nothing */
        } else {
            addr1 += memory_region_get_ram_addr(mr);
            /* ROM/RAM case */
            ptr = qemu_get_ram_ptr(addr1);
            switch (type) {
            case WRITE_DATA:
                memcpy(ptr, buf, l);
                invalidate_and_set_dirty(addr1, l);
                break;
            case FLUSH_CACHE:
                flush_icache_range((uintptr_t)ptr, (uintptr_t)ptr + l);
                break;
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
}

/* used for ROM loading : can write in RAM and ROM */
void cpu_physical_memory_write_rom(AddressSpace *as, hwaddr addr,
                                   const uint8_t *buf, int len)
{
    cpu_physical_memory_write_rom_internal(as, addr, buf, len, WRITE_DATA);
}

void cpu_flush_icache_range(hwaddr start, int len)
{
    /*
     * This function should do the same thing as an icache flush that was
     * triggered from within the guest. For TCG we are always cache coherent,
     * so there is no need to flush anything. For KVM / Xen we need to flush
     * the host's instruction cache at least.
     */
    if (tcg_enabled()) {
        return;
    }

    cpu_physical_memory_write_rom_internal(&address_space_memory,
                                           start, NULL, len, FLUSH_CACHE);
}

typedef struct {
    MemoryRegion *mr;
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
    hwaddr done = 0;
    hwaddr l, xlat, base;
    MemoryRegion *mr, *this_mr;
    ram_addr_t raddr;

    if (len == 0) {
        return NULL;
    }

    l = len;
    mr = address_space_translate(as, addr, &xlat, &l, is_write);
    if (!memory_access_is_direct(mr, is_write)) {
        if (bounce.buffer) {
            return NULL;
        }
        /* Avoid unbounded allocations */
        l = MIN(l, TARGET_PAGE_SIZE);
        bounce.buffer = qemu_memalign(TARGET_PAGE_SIZE, l);
        bounce.addr = addr;
        bounce.len = l;

        memory_region_ref(mr);
        bounce.mr = mr;
        if (!is_write) {
            address_space_read(as, addr, bounce.buffer, l);
        }

        *plen = l;
        return bounce.buffer;
    }

    base = xlat;
    raddr = memory_region_get_ram_addr(mr);

    for (;;) {
        len -= l;
        addr += l;
        done += l;
        if (len == 0) {
            break;
        }

        l = len;
        this_mr = address_space_translate(as, addr, &xlat, &l, is_write);
        if (this_mr != mr || xlat != base + done) {
            break;
        }
    }

    memory_region_ref(mr);
    *plen = done;
    return qemu_ram_ptr_length(raddr + base, plen);
}

/* Unmaps a memory region previously mapped by address_space_map().
 * Will also mark the memory as dirty if is_write == 1.  access_len gives
 * the amount of memory that was actually read or written by the caller.
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         int is_write, hwaddr access_len)
{
    if (buffer != bounce.buffer) {
        MemoryRegion *mr;
        ram_addr_t addr1;

        mr = qemu_ram_addr_from_host(buffer, &addr1);
        assert(mr != NULL);
        if (is_write) {
            invalidate_and_set_dirty(addr1, access_len);
        }
        if (xen_enabled()) {
            xen_invalidate_map_cache_entry(buffer);
        }
        memory_region_unref(mr);
        return;
    }
    if (is_write) {
        address_space_write(as, bounce.addr, bounce.buffer, access_len);
    }
    qemu_vfree(bounce.buffer);
    bounce.buffer = NULL;
    memory_region_unref(bounce.mr);
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
static inline uint32_t ldl_phys_internal(AddressSpace *as, hwaddr addr,
                                         enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;

    mr = address_space_translate(as, addr, &addr1, &l, false);
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

uint32_t ldl_phys(AddressSpace *as, hwaddr addr)
{
    return ldl_phys_internal(as, addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t ldl_le_phys(AddressSpace *as, hwaddr addr)
{
    return ldl_phys_internal(as, addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t ldl_be_phys(AddressSpace *as, hwaddr addr)
{
    return ldl_phys_internal(as, addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned */
static inline uint64_t ldq_phys_internal(AddressSpace *as, hwaddr addr,
                                         enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 8;
    hwaddr addr1;

    mr = address_space_translate(as, addr, &addr1, &l,
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

uint64_t ldq_phys(AddressSpace *as, hwaddr addr)
{
    return ldq_phys_internal(as, addr, DEVICE_NATIVE_ENDIAN);
}

uint64_t ldq_le_phys(AddressSpace *as, hwaddr addr)
{
    return ldq_phys_internal(as, addr, DEVICE_LITTLE_ENDIAN);
}

uint64_t ldq_be_phys(AddressSpace *as, hwaddr addr)
{
    return ldq_phys_internal(as, addr, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
uint32_t ldub_phys(AddressSpace *as, hwaddr addr)
{
    uint8_t val;
    address_space_rw(as, addr, &val, 1, 0);
    return val;
}

/* warning: addr must be aligned */
static inline uint32_t lduw_phys_internal(AddressSpace *as, hwaddr addr,
                                          enum device_endian endian)
{
    uint8_t *ptr;
    uint64_t val;
    MemoryRegion *mr;
    hwaddr l = 2;
    hwaddr addr1;

    mr = address_space_translate(as, addr, &addr1, &l,
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

uint32_t lduw_phys(AddressSpace *as, hwaddr addr)
{
    return lduw_phys_internal(as, addr, DEVICE_NATIVE_ENDIAN);
}

uint32_t lduw_le_phys(AddressSpace *as, hwaddr addr)
{
    return lduw_phys_internal(as, addr, DEVICE_LITTLE_ENDIAN);
}

uint32_t lduw_be_phys(AddressSpace *as, hwaddr addr)
{
    return lduw_phys_internal(as, addr, DEVICE_BIG_ENDIAN);
}

/* warning: addr must be aligned. The ram page is not masked as dirty
   and the code inside is not invalidated. It is useful if the dirty
   bits are used to track modified PTEs */
void stl_phys_notdirty(AddressSpace *as, hwaddr addr, uint32_t val)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;

    mr = address_space_translate(as, addr, &addr1, &l,
                                 true);
    if (l < 4 || !memory_access_is_direct(mr, true)) {
        io_mem_write(mr, addr1, val, 4);
    } else {
        addr1 += memory_region_get_ram_addr(mr) & TARGET_PAGE_MASK;
        ptr = qemu_get_ram_ptr(addr1);
        stl_p(ptr, val);

        if (unlikely(in_migration)) {
            if (cpu_physical_memory_is_clean(addr1)) {
                /* invalidate code */
                tb_invalidate_phys_page_range(addr1, addr1 + 4, 0);
                /* set dirty bit */
                cpu_physical_memory_set_dirty_range_nocode(addr1, 4);
            }
        }
    }
}

/* warning: addr must be aligned */
static inline void stl_phys_internal(AddressSpace *as,
                                     hwaddr addr, uint32_t val,
                                     enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 4;
    hwaddr addr1;

    mr = address_space_translate(as, addr, &addr1, &l,
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

void stl_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    stl_phys_internal(as, addr, val, DEVICE_NATIVE_ENDIAN);
}

void stl_le_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    stl_phys_internal(as, addr, val, DEVICE_LITTLE_ENDIAN);
}

void stl_be_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    stl_phys_internal(as, addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stb_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    uint8_t v = val;
    address_space_rw(as, addr, &v, 1, 1);
}

/* warning: addr must be aligned */
static inline void stw_phys_internal(AddressSpace *as,
                                     hwaddr addr, uint32_t val,
                                     enum device_endian endian)
{
    uint8_t *ptr;
    MemoryRegion *mr;
    hwaddr l = 2;
    hwaddr addr1;

    mr = address_space_translate(as, addr, &addr1, &l, true);
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

void stw_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    stw_phys_internal(as, addr, val, DEVICE_NATIVE_ENDIAN);
}

void stw_le_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    stw_phys_internal(as, addr, val, DEVICE_LITTLE_ENDIAN);
}

void stw_be_phys(AddressSpace *as, hwaddr addr, uint32_t val)
{
    stw_phys_internal(as, addr, val, DEVICE_BIG_ENDIAN);
}

/* XXX: optimize */
void stq_phys(AddressSpace *as, hwaddr addr, uint64_t val)
{
    val = tswap64(val);
    address_space_rw(as, addr, (void *) &val, 8, 1);
}

void stq_le_phys(AddressSpace *as, hwaddr addr, uint64_t val)
{
    val = cpu_to_le64(val);
    address_space_rw(as, addr, (void *) &val, 8, 1);
}

void stq_be_phys(AddressSpace *as, hwaddr addr, uint64_t val)
{
    val = cpu_to_be64(val);
    address_space_rw(as, addr, (void *) &val, 8, 1);
}

/* virtual memory access for debug (includes writing to ROM) */
int cpu_memory_rw_debug(CPUState *cpu, target_ulong addr,
                        uint8_t *buf, int len, int is_write)
{
    int l;
    hwaddr phys_addr;
    target_ulong page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_debug(cpu, page);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        phys_addr += (addr & ~TARGET_PAGE_MASK);
        if (is_write) {
            cpu_physical_memory_write_rom(cpu->as, phys_addr, buf, l);
        } else {
            address_space_rw(cpu->as, phys_addr, buf, l, 0);
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}
#endif

/*
 * A helper function for the _utterly broken_ virtio device model to find out if
 * it's running on a big endian machine. Don't do this at home kids!
 */
bool target_words_bigendian(void);
bool target_words_bigendian(void)
{
#if defined(TARGET_WORDS_BIGENDIAN)
    return true;
#else
    return false;
#endif
}

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
