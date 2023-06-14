/*
 * RAM allocation and memory access
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

#include "qemu/osdep.h"
#include "exec/page-vary.h"
#include "qapi/error.h"

#include "qemu/cutils.h"
#include "qemu/cacheflush.h"
#include "qemu/hbitmap.h"
#include "qemu/madvise.h"

#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */

#include "exec/exec-all.h"
#include "exec/target_page.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "hw/xen/xen.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "sysemu/qtest.h"
#include "qemu/timer.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "qemu/log.h"
#include "qemu/memalign.h"
#include "exec/memory.h"
#include "exec/ioport.h"
#include "sysemu/dma.h"
#include "sysemu/hostmem.h"
#include "sysemu/hw_accel.h"
#include "sysemu/xen-mapcache.h"
#include "trace/trace-root.h"

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
#include <linux/falloc.h>
#endif

#include "qemu/rcu_queue.h"
#include "qemu/main-loop.h"
#include "exec/translate-all.h"
#include "sysemu/replay.h"

#include "exec/memory-internal.h"
#include "exec/ram_addr.h"

#include "qemu/pmem.h"

#include "migration/vmstate.h"

#include "qemu/range.h"
#ifndef _WIN32
#include "qemu/mmap-alloc.h"
#endif

#include "monitor/monitor.h"

#ifdef CONFIG_LIBDAXCTL
#include <daxctl/libdaxctl.h>
#endif

//#define DEBUG_SUBPAGE

/* ram_list is read under rcu_read_lock()/rcu_read_unlock().  Writes
 * are protected by the ramlist lock.
 */
RAMList ram_list = { .blocks = QLIST_HEAD_INITIALIZER(ram_list.blocks) };

static MemoryRegion *system_memory;
static MemoryRegion *system_io;

AddressSpace address_space_io;
AddressSpace address_space_memory;

static MemoryRegion io_mem_unassigned;

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
    struct rcu_head rcu;

    unsigned sections_nb;
    unsigned sections_nb_alloc;
    unsigned nodes_nb;
    unsigned nodes_nb_alloc;
    Node *nodes;
    MemoryRegionSection *sections;
} PhysPageMap;

struct AddressSpaceDispatch {
    MemoryRegionSection *mru_section;
    /* This is a multi-level map on the physical address space.
     * The bottom level has pointers to MemoryRegionSections.
     */
    PhysPageEntry phys_map;
    PhysPageMap map;
};

#define SUBPAGE_IDX(addr) ((addr) & ~TARGET_PAGE_MASK)
typedef struct subpage_t {
    MemoryRegion iomem;
    FlatView *fv;
    hwaddr base;
    uint16_t sub_section[];
} subpage_t;

#define PHYS_SECTION_UNASSIGNED 0

static void io_mem_init(void);
static void memory_map_init(void);
static void tcg_log_global_after_sync(MemoryListener *listener);
static void tcg_commit(MemoryListener *listener);

/**
 * CPUAddressSpace: all the information a CPU needs about an AddressSpace
 * @cpu: the CPU whose AddressSpace this is
 * @as: the AddressSpace itself
 * @memory_dispatch: its dispatch pointer (cached, RCU protected)
 * @tcg_as_listener: listener for tracking changes to the AddressSpace
 */
struct CPUAddressSpace {
    CPUState *cpu;
    AddressSpace *as;
    struct AddressSpaceDispatch *memory_dispatch;
    MemoryListener tcg_as_listener;
};

struct DirtyBitmapSnapshot {
    ram_addr_t start;
    ram_addr_t end;
    unsigned long dirty[];
};

static void phys_map_node_reserve(PhysPageMap *map, unsigned nodes)
{
    static unsigned alloc_hint = 16;
    if (map->nodes_nb + nodes > map->nodes_nb_alloc) {
        map->nodes_nb_alloc = MAX(alloc_hint, map->nodes_nb + nodes);
        map->nodes = g_renew(Node, map->nodes, map->nodes_nb_alloc);
        alloc_hint = map->nodes_nb_alloc;
    }
}

static uint32_t phys_map_node_alloc(PhysPageMap *map, bool leaf)
{
    unsigned i;
    uint32_t ret;
    PhysPageEntry e;
    PhysPageEntry *p;

    ret = map->nodes_nb++;
    p = map->nodes[ret];
    assert(ret != PHYS_MAP_NODE_NIL);
    assert(ret != map->nodes_nb_alloc);

    e.skip = leaf ? 0 : 1;
    e.ptr = leaf ? PHYS_SECTION_UNASSIGNED : PHYS_MAP_NODE_NIL;
    for (i = 0; i < P_L2_SIZE; ++i) {
        memcpy(&p[i], &e, sizeof(e));
    }
    return ret;
}

static void phys_page_set_level(PhysPageMap *map, PhysPageEntry *lp,
                                hwaddr *index, uint64_t *nb, uint16_t leaf,
                                int level)
{
    PhysPageEntry *p;
    hwaddr step = (hwaddr)1 << (level * P_L2_BITS);

    if (lp->skip && lp->ptr == PHYS_MAP_NODE_NIL) {
        lp->ptr = phys_map_node_alloc(map, level == 0);
    }
    p = map->nodes[lp->ptr];
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
                          hwaddr index, uint64_t nb,
                          uint16_t leaf)
{
    /* Wildly overreserve - it doesn't matter much. */
    phys_map_node_reserve(&d->map, 3 * P_L2_LEVELS);

    phys_page_set_level(&d->map, &d->phys_map, &index, &nb, leaf, P_L2_LEVELS - 1);
}

/* Compact a non leaf page entry. Simply detect that the entry has a single child,
 * and update our entry so we can skip it and go directly to the destination.
 */
static void phys_page_compact(PhysPageEntry *lp, Node *nodes)
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
            phys_page_compact(&p[i], nodes);
        }
    }

    /* We can only compress if there's only one child. */
    if (valid != 1) {
        return;
    }

    assert(valid_ptr < P_L2_SIZE);

    /* Don't compress if it won't fit in the # of bits we have. */
    if (P_L2_LEVELS >= (1 << 6) &&
        lp->skip + p[valid_ptr].skip >= (1 << 6)) {
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

void address_space_dispatch_compact(AddressSpaceDispatch *d)
{
    if (d->phys_map.skip) {
        phys_page_compact(&d->phys_map, d->map.nodes);
    }
}

static inline bool section_covers_addr(const MemoryRegionSection *section,
                                       hwaddr addr)
{
    /* Memory topology clips a memory region to [0, 2^64); size.hi > 0 means
     * the section must cover the entire address space.
     */
    return int128_gethi(section->size) ||
           range_covers_byte(section->offset_within_address_space,
                             int128_getlo(section->size), addr);
}

static MemoryRegionSection *phys_page_find(AddressSpaceDispatch *d, hwaddr addr)
{
    PhysPageEntry lp = d->phys_map, *p;
    Node *nodes = d->map.nodes;
    MemoryRegionSection *sections = d->map.sections;
    hwaddr index = addr >> TARGET_PAGE_BITS;
    int i;

    for (i = P_L2_LEVELS; lp.skip && (i -= lp.skip) >= 0;) {
        if (lp.ptr == PHYS_MAP_NODE_NIL) {
            return &sections[PHYS_SECTION_UNASSIGNED];
        }
        p = nodes[lp.ptr];
        lp = p[(index >> (i * P_L2_BITS)) & (P_L2_SIZE - 1)];
    }

    if (section_covers_addr(&sections[lp.ptr], addr)) {
        return &sections[lp.ptr];
    } else {
        return &sections[PHYS_SECTION_UNASSIGNED];
    }
}

/* Called from RCU critical section */
static MemoryRegionSection *address_space_lookup_region(AddressSpaceDispatch *d,
                                                        hwaddr addr,
                                                        bool resolve_subpage)
{
    MemoryRegionSection *section = qatomic_read(&d->mru_section);
    subpage_t *subpage;

    if (!section || section == &d->map.sections[PHYS_SECTION_UNASSIGNED] ||
        !section_covers_addr(section, addr)) {
        section = phys_page_find(d, addr);
        qatomic_set(&d->mru_section, section);
    }
    if (resolve_subpage && section->mr->subpage) {
        subpage = container_of(section->mr, subpage_t, iomem);
        section = &d->map.sections[subpage->sub_section[SUBPAGE_IDX(addr)]];
    }
    return section;
}

/* Called from RCU critical section */
static MemoryRegionSection *
address_space_translate_internal(AddressSpaceDispatch *d, hwaddr addr, hwaddr *xlat,
                                 hwaddr *plen, bool resolve_subpage)
{
    MemoryRegionSection *section;
    MemoryRegion *mr;
    Int128 diff;

    section = address_space_lookup_region(d, addr, resolve_subpage);
    /* Compute offset within MemoryRegionSection */
    addr -= section->offset_within_address_space;

    /* Compute offset within MemoryRegion */
    *xlat = addr + section->offset_within_region;

    mr = section->mr;

    /* MMIO registers can be expected to perform full-width accesses based only
     * on their address, without considering adjacent registers that could
     * decode to completely different MemoryRegions.  When such registers
     * exist (e.g. I/O ports 0xcf8 and 0xcf9 on most PC chipsets), MMIO
     * regions overlap wildly.  For this reason we cannot clamp the accesses
     * here.
     *
     * If the length is small (as is the case for address_space_ldl/stl),
     * everything works fine.  If the incoming length is large, however,
     * the caller really has to do the clamping through memory_access_size.
     */
    if (memory_region_is_ram(mr)) {
        diff = int128_sub(section->size, int128_make64(addr));
        *plen = int128_get64(int128_min(diff, int128_make64(*plen)));
    }
    return section;
}

/**
 * address_space_translate_iommu - translate an address through an IOMMU
 * memory region and then through the target address space.
 *
 * @iommu_mr: the IOMMU memory region that we start the translation from
 * @addr: the address to be translated through the MMU
 * @xlat: the translated address offset within the destination memory region.
 *        It cannot be %NULL.
 * @plen_out: valid read/write length of the translated address. It
 *            cannot be %NULL.
 * @page_mask_out: page mask for the translated address. This
 *            should only be meaningful for IOMMU translated
 *            addresses, since there may be huge pages that this bit
 *            would tell. It can be %NULL if we don't care about it.
 * @is_write: whether the translation operation is for write
 * @is_mmio: whether this can be MMIO, set true if it can
 * @target_as: the address space targeted by the IOMMU
 * @attrs: transaction attributes
 *
 * This function is called from RCU critical section.  It is the common
 * part of flatview_do_translate and address_space_translate_cached.
 */
static MemoryRegionSection address_space_translate_iommu(IOMMUMemoryRegion *iommu_mr,
                                                         hwaddr *xlat,
                                                         hwaddr *plen_out,
                                                         hwaddr *page_mask_out,
                                                         bool is_write,
                                                         bool is_mmio,
                                                         AddressSpace **target_as,
                                                         MemTxAttrs attrs)
{
    MemoryRegionSection *section;
    hwaddr page_mask = (hwaddr)-1;

    do {
        hwaddr addr = *xlat;
        IOMMUMemoryRegionClass *imrc = memory_region_get_iommu_class_nocheck(iommu_mr);
        int iommu_idx = 0;
        IOMMUTLBEntry iotlb;

        if (imrc->attrs_to_index) {
            iommu_idx = imrc->attrs_to_index(iommu_mr, attrs);
        }

        iotlb = imrc->translate(iommu_mr, addr, is_write ?
                                IOMMU_WO : IOMMU_RO, iommu_idx);

        if (!(iotlb.perm & (1 << is_write))) {
            goto unassigned;
        }

        addr = ((iotlb.translated_addr & ~iotlb.addr_mask)
                | (addr & iotlb.addr_mask));
        page_mask &= iotlb.addr_mask;
        *plen_out = MIN(*plen_out, (addr | iotlb.addr_mask) - addr + 1);
        *target_as = iotlb.target_as;

        section = address_space_translate_internal(
                address_space_to_dispatch(iotlb.target_as), addr, xlat,
                plen_out, is_mmio);

        iommu_mr = memory_region_get_iommu(section->mr);
    } while (unlikely(iommu_mr));

    if (page_mask_out) {
        *page_mask_out = page_mask;
    }
    return *section;

unassigned:
    return (MemoryRegionSection) { .mr = &io_mem_unassigned };
}

/**
 * flatview_do_translate - translate an address in FlatView
 *
 * @fv: the flat view that we want to translate on
 * @addr: the address to be translated in above address space
 * @xlat: the translated address offset within memory region. It
 *        cannot be @NULL.
 * @plen_out: valid read/write length of the translated address. It
 *            can be @NULL when we don't care about it.
 * @page_mask_out: page mask for the translated address. This
 *            should only be meaningful for IOMMU translated
 *            addresses, since there may be huge pages that this bit
 *            would tell. It can be @NULL if we don't care about it.
 * @is_write: whether the translation operation is for write
 * @is_mmio: whether this can be MMIO, set true if it can
 * @target_as: the address space targeted by the IOMMU
 * @attrs: memory transaction attributes
 *
 * This function is called from RCU critical section
 */
static MemoryRegionSection flatview_do_translate(FlatView *fv,
                                                 hwaddr addr,
                                                 hwaddr *xlat,
                                                 hwaddr *plen_out,
                                                 hwaddr *page_mask_out,
                                                 bool is_write,
                                                 bool is_mmio,
                                                 AddressSpace **target_as,
                                                 MemTxAttrs attrs)
{
    MemoryRegionSection *section;
    IOMMUMemoryRegion *iommu_mr;
    hwaddr plen = (hwaddr)(-1);

    if (!plen_out) {
        plen_out = &plen;
    }

    section = address_space_translate_internal(
            flatview_to_dispatch(fv), addr, xlat,
            plen_out, is_mmio);

    iommu_mr = memory_region_get_iommu(section->mr);
    if (unlikely(iommu_mr)) {
        return address_space_translate_iommu(iommu_mr, xlat,
                                             plen_out, page_mask_out,
                                             is_write, is_mmio,
                                             target_as, attrs);
    }
    if (page_mask_out) {
        /* Not behind an IOMMU, use default page size. */
        *page_mask_out = ~TARGET_PAGE_MASK;
    }

    return *section;
}

/* Called from RCU critical section */
IOMMUTLBEntry address_space_get_iotlb_entry(AddressSpace *as, hwaddr addr,
                                            bool is_write, MemTxAttrs attrs)
{
    MemoryRegionSection section;
    hwaddr xlat, page_mask;

    /*
     * This can never be MMIO, and we don't really care about plen,
     * but page mask.
     */
    section = flatview_do_translate(address_space_to_flatview(as), addr, &xlat,
                                    NULL, &page_mask, is_write, false, &as,
                                    attrs);

    /* Illegal translation */
    if (section.mr == &io_mem_unassigned) {
        goto iotlb_fail;
    }

    /* Convert memory region offset into address space offset */
    xlat += section.offset_within_address_space -
        section.offset_within_region;

    return (IOMMUTLBEntry) {
        .target_as = as,
        .iova = addr & ~page_mask,
        .translated_addr = xlat & ~page_mask,
        .addr_mask = page_mask,
        /* IOTLBs are for DMAs, and DMA only allows on RAMs. */
        .perm = IOMMU_RW,
    };

iotlb_fail:
    return (IOMMUTLBEntry) {0};
}

/* Called from RCU critical section */
MemoryRegion *flatview_translate(FlatView *fv, hwaddr addr, hwaddr *xlat,
                                 hwaddr *plen, bool is_write,
                                 MemTxAttrs attrs)
{
    MemoryRegion *mr;
    MemoryRegionSection section;
    AddressSpace *as = NULL;

    /* This can be MMIO, so setup MMIO bit. */
    section = flatview_do_translate(fv, addr, xlat, plen, NULL,
                                    is_write, true, &as, attrs);
    mr = section.mr;

    if (xen_enabled() && memory_access_is_direct(mr, is_write)) {
        hwaddr page = ((addr & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE) - addr;
        *plen = MIN(page, *plen);
    }

    return mr;
}

typedef struct TCGIOMMUNotifier {
    IOMMUNotifier n;
    MemoryRegion *mr;
    CPUState *cpu;
    int iommu_idx;
    bool active;
} TCGIOMMUNotifier;

static void tcg_iommu_unmap_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    TCGIOMMUNotifier *notifier = container_of(n, TCGIOMMUNotifier, n);

    if (!notifier->active) {
        return;
    }
    tlb_flush(notifier->cpu);
    notifier->active = false;
    /* We leave the notifier struct on the list to avoid reallocating it later.
     * Generally the number of IOMMUs a CPU deals with will be small.
     * In any case we can't unregister the iommu notifier from a notify
     * callback.
     */
}

static void tcg_register_iommu_notifier(CPUState *cpu,
                                        IOMMUMemoryRegion *iommu_mr,
                                        int iommu_idx)
{
    /* Make sure this CPU has an IOMMU notifier registered for this
     * IOMMU/IOMMU index combination, so that we can flush its TLB
     * when the IOMMU tells us the mappings we've cached have changed.
     */
    MemoryRegion *mr = MEMORY_REGION(iommu_mr);
    TCGIOMMUNotifier *notifier = NULL;
    int i;

    for (i = 0; i < cpu->iommu_notifiers->len; i++) {
        notifier = g_array_index(cpu->iommu_notifiers, TCGIOMMUNotifier *, i);
        if (notifier->mr == mr && notifier->iommu_idx == iommu_idx) {
            break;
        }
    }
    if (i == cpu->iommu_notifiers->len) {
        /* Not found, add a new entry at the end of the array */
        cpu->iommu_notifiers = g_array_set_size(cpu->iommu_notifiers, i + 1);
        notifier = g_new0(TCGIOMMUNotifier, 1);
        g_array_index(cpu->iommu_notifiers, TCGIOMMUNotifier *, i) = notifier;

        notifier->mr = mr;
        notifier->iommu_idx = iommu_idx;
        notifier->cpu = cpu;
        /* Rather than trying to register interest in the specific part
         * of the iommu's address space that we've accessed and then
         * expand it later as subsequent accesses touch more of it, we
         * just register interest in the whole thing, on the assumption
         * that iommu reconfiguration will be rare.
         */
        iommu_notifier_init(&notifier->n,
                            tcg_iommu_unmap_notify,
                            IOMMU_NOTIFIER_UNMAP,
                            0,
                            HWADDR_MAX,
                            iommu_idx);
        memory_region_register_iommu_notifier(notifier->mr, &notifier->n,
                                              &error_fatal);
    }

    if (!notifier->active) {
        notifier->active = true;
    }
}

void tcg_iommu_free_notifier_list(CPUState *cpu)
{
    /* Destroy the CPU's notifier list */
    int i;
    TCGIOMMUNotifier *notifier;

    for (i = 0; i < cpu->iommu_notifiers->len; i++) {
        notifier = g_array_index(cpu->iommu_notifiers, TCGIOMMUNotifier *, i);
        memory_region_unregister_iommu_notifier(notifier->mr, &notifier->n);
        g_free(notifier);
    }
    g_array_free(cpu->iommu_notifiers, true);
}

void tcg_iommu_init_notifier_list(CPUState *cpu)
{
    cpu->iommu_notifiers = g_array_new(false, true, sizeof(TCGIOMMUNotifier *));
}

/* Called from RCU critical section */
MemoryRegionSection *
address_space_translate_for_iotlb(CPUState *cpu, int asidx, hwaddr orig_addr,
                                  hwaddr *xlat, hwaddr *plen,
                                  MemTxAttrs attrs, int *prot)
{
    MemoryRegionSection *section;
    IOMMUMemoryRegion *iommu_mr;
    IOMMUMemoryRegionClass *imrc;
    IOMMUTLBEntry iotlb;
    int iommu_idx;
    hwaddr addr = orig_addr;
    AddressSpaceDispatch *d =
        qatomic_rcu_read(&cpu->cpu_ases[asidx].memory_dispatch);

    for (;;) {
        section = address_space_translate_internal(d, addr, &addr, plen, false);

        iommu_mr = memory_region_get_iommu(section->mr);
        if (!iommu_mr) {
            break;
        }

        imrc = memory_region_get_iommu_class_nocheck(iommu_mr);

        iommu_idx = imrc->attrs_to_index(iommu_mr, attrs);
        tcg_register_iommu_notifier(cpu, iommu_mr, iommu_idx);
        /* We need all the permissions, so pass IOMMU_NONE so the IOMMU
         * doesn't short-cut its translation table walk.
         */
        iotlb = imrc->translate(iommu_mr, addr, IOMMU_NONE, iommu_idx);
        addr = ((iotlb.translated_addr & ~iotlb.addr_mask)
                | (addr & iotlb.addr_mask));
        /* Update the caller's prot bits to remove permissions the IOMMU
         * is giving us a failure response for. If we get down to no
         * permissions left at all we can give up now.
         */
        if (!(iotlb.perm & IOMMU_RO)) {
            *prot &= ~(PAGE_READ | PAGE_EXEC);
        }
        if (!(iotlb.perm & IOMMU_WO)) {
            *prot &= ~PAGE_WRITE;
        }

        if (!*prot) {
            goto translate_fail;
        }

        d = flatview_to_dispatch(address_space_to_flatview(iotlb.target_as));
    }

    assert(!memory_region_is_iommu(section->mr));
    *xlat = addr;
    return section;

translate_fail:
    /*
     * We should be given a page-aligned address -- certainly
     * tlb_set_page_with_attrs() does so.  The page offset of xlat
     * is used to index sections[], and PHYS_SECTION_UNASSIGNED = 0.
     * The page portion of xlat will be logged by memory_region_access_valid()
     * when this memory access is rejected, so use the original untranslated
     * physical address.
     */
    assert((orig_addr & ~TARGET_PAGE_MASK) == 0);
    *xlat = orig_addr;
    return &d->map.sections[PHYS_SECTION_UNASSIGNED];
}

void cpu_address_space_init(CPUState *cpu, int asidx,
                            const char *prefix, MemoryRegion *mr)
{
    CPUAddressSpace *newas;
    AddressSpace *as = g_new0(AddressSpace, 1);
    char *as_name;

    assert(mr);
    as_name = g_strdup_printf("%s-%d", prefix, cpu->cpu_index);
    address_space_init(as, mr, as_name);
    g_free(as_name);

    /* Target code should have set num_ases before calling us */
    assert(asidx < cpu->num_ases);

    if (asidx == 0) {
        /* address space 0 gets the convenience alias */
        cpu->as = as;
    }

    /* KVM cannot currently support multiple address spaces. */
    assert(asidx == 0 || !kvm_enabled());

    if (!cpu->cpu_ases) {
        cpu->cpu_ases = g_new0(CPUAddressSpace, cpu->num_ases);
    }

    newas = &cpu->cpu_ases[asidx];
    newas->cpu = cpu;
    newas->as = as;
    if (tcg_enabled()) {
        newas->tcg_as_listener.log_global_after_sync = tcg_log_global_after_sync;
        newas->tcg_as_listener.commit = tcg_commit;
        newas->tcg_as_listener.name = "tcg";
        memory_listener_register(&newas->tcg_as_listener, as);
    }
}

AddressSpace *cpu_get_address_space(CPUState *cpu, int asidx)
{
    /* Return the AddressSpace corresponding to the specified index */
    return cpu->cpu_ases[asidx].as;
}

/* Called from RCU critical section */
static RAMBlock *qemu_get_ram_block(ram_addr_t addr)
{
    RAMBlock *block;

    block = qatomic_rcu_read(&ram_list.mru_block);
    if (block && addr - block->offset < block->max_length) {
        return block;
    }
    RAMBLOCK_FOREACH(block) {
        if (addr - block->offset < block->max_length) {
            goto found;
        }
    }

    fprintf(stderr, "Bad ram offset %" PRIx64 "\n", (uint64_t)addr);
    abort();

found:
    /* It is safe to write mru_block outside the iothread lock.  This
     * is what happens:
     *
     *     mru_block = xxx
     *     rcu_read_unlock()
     *                                        xxx removed from list
     *                  rcu_read_lock()
     *                  read mru_block
     *                                        mru_block = NULL;
     *                                        call_rcu(reclaim_ramblock, xxx);
     *                  rcu_read_unlock()
     *
     * qatomic_rcu_set is not needed here.  The block was already published
     * when it was placed into the list.  Here we're just making an extra
     * copy of the pointer.
     */
    ram_list.mru_block = block;
    return block;
}

static void tlb_reset_dirty_range_all(ram_addr_t start, ram_addr_t length)
{
    CPUState *cpu;
    ram_addr_t start1;
    RAMBlock *block;
    ram_addr_t end;

    assert(tcg_enabled());
    end = TARGET_PAGE_ALIGN(start + length);
    start &= TARGET_PAGE_MASK;

    RCU_READ_LOCK_GUARD();
    block = qemu_get_ram_block(start);
    assert(block == qemu_get_ram_block(end - 1));
    start1 = (uintptr_t)ramblock_ptr(block, start - block->offset);
    CPU_FOREACH(cpu) {
        tlb_reset_dirty(cpu, start1, length);
    }
}

/* Note: start and end must be within the same ram block.  */
bool cpu_physical_memory_test_and_clear_dirty(ram_addr_t start,
                                              ram_addr_t length,
                                              unsigned client)
{
    DirtyMemoryBlocks *blocks;
    unsigned long end, page, start_page;
    bool dirty = false;
    RAMBlock *ramblock;
    uint64_t mr_offset, mr_size;

    if (length == 0) {
        return false;
    }

    end = TARGET_PAGE_ALIGN(start + length) >> TARGET_PAGE_BITS;
    start_page = start >> TARGET_PAGE_BITS;
    page = start_page;

    WITH_RCU_READ_LOCK_GUARD() {
        blocks = qatomic_rcu_read(&ram_list.dirty_memory[client]);
        ramblock = qemu_get_ram_block(start);
        /* Range sanity check on the ramblock */
        assert(start >= ramblock->offset &&
               start + length <= ramblock->offset + ramblock->used_length);

        while (page < end) {
            unsigned long idx = page / DIRTY_MEMORY_BLOCK_SIZE;
            unsigned long offset = page % DIRTY_MEMORY_BLOCK_SIZE;
            unsigned long num = MIN(end - page,
                                    DIRTY_MEMORY_BLOCK_SIZE - offset);

            dirty |= bitmap_test_and_clear_atomic(blocks->blocks[idx],
                                                  offset, num);
            page += num;
        }

        mr_offset = (ram_addr_t)(start_page << TARGET_PAGE_BITS) - ramblock->offset;
        mr_size = (end - start_page) << TARGET_PAGE_BITS;
        memory_region_clear_dirty_bitmap(ramblock->mr, mr_offset, mr_size);
    }

    if (dirty && tcg_enabled()) {
        tlb_reset_dirty_range_all(start, length);
    }

    return dirty;
}

DirtyBitmapSnapshot *cpu_physical_memory_snapshot_and_clear_dirty
    (MemoryRegion *mr, hwaddr offset, hwaddr length, unsigned client)
{
    DirtyMemoryBlocks *blocks;
    ram_addr_t start = memory_region_get_ram_addr(mr) + offset;
    unsigned long align = 1UL << (TARGET_PAGE_BITS + BITS_PER_LEVEL);
    ram_addr_t first = QEMU_ALIGN_DOWN(start, align);
    ram_addr_t last  = QEMU_ALIGN_UP(start + length, align);
    DirtyBitmapSnapshot *snap;
    unsigned long page, end, dest;

    snap = g_malloc0(sizeof(*snap) +
                     ((last - first) >> (TARGET_PAGE_BITS + 3)));
    snap->start = first;
    snap->end   = last;

    page = first >> TARGET_PAGE_BITS;
    end  = last  >> TARGET_PAGE_BITS;
    dest = 0;

    WITH_RCU_READ_LOCK_GUARD() {
        blocks = qatomic_rcu_read(&ram_list.dirty_memory[client]);

        while (page < end) {
            unsigned long idx = page / DIRTY_MEMORY_BLOCK_SIZE;
            unsigned long offset = page % DIRTY_MEMORY_BLOCK_SIZE;
            unsigned long num = MIN(end - page,
                                    DIRTY_MEMORY_BLOCK_SIZE - offset);

            assert(QEMU_IS_ALIGNED(offset, (1 << BITS_PER_LEVEL)));
            assert(QEMU_IS_ALIGNED(num,    (1 << BITS_PER_LEVEL)));
            offset >>= BITS_PER_LEVEL;

            bitmap_copy_and_clear_atomic(snap->dirty + dest,
                                         blocks->blocks[idx] + offset,
                                         num);
            page += num;
            dest += num >> BITS_PER_LEVEL;
        }
    }

    if (tcg_enabled()) {
        tlb_reset_dirty_range_all(start, length);
    }

    memory_region_clear_dirty_bitmap(mr, offset, length);

    return snap;
}

bool cpu_physical_memory_snapshot_get_dirty(DirtyBitmapSnapshot *snap,
                                            ram_addr_t start,
                                            ram_addr_t length)
{
    unsigned long page, end;

    assert(start >= snap->start);
    assert(start + length <= snap->end);

    end = TARGET_PAGE_ALIGN(start + length - snap->start) >> TARGET_PAGE_BITS;
    page = (start - snap->start) >> TARGET_PAGE_BITS;

    while (page < end) {
        if (test_bit(page, snap->dirty)) {
            return true;
        }
        page++;
    }
    return false;
}

/* Called from RCU critical section */
hwaddr memory_region_section_get_iotlb(CPUState *cpu,
                                       MemoryRegionSection *section)
{
    AddressSpaceDispatch *d = flatview_to_dispatch(section->fv);
    return section - d->map.sections;
}

static int subpage_register(subpage_t *mmio, uint32_t start, uint32_t end,
                            uint16_t section);
static subpage_t *subpage_init(FlatView *fv, hwaddr base);

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
    bool have_sub_page = mr->subpage;

    memory_region_unref(mr);

    if (have_sub_page) {
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

static void register_subpage(FlatView *fv, MemoryRegionSection *section)
{
    AddressSpaceDispatch *d = flatview_to_dispatch(fv);
    subpage_t *subpage;
    hwaddr base = section->offset_within_address_space
        & TARGET_PAGE_MASK;
    MemoryRegionSection *existing = phys_page_find(d, base);
    MemoryRegionSection subsection = {
        .offset_within_address_space = base,
        .size = int128_make64(TARGET_PAGE_SIZE),
    };
    hwaddr start, end;

    assert(existing->mr->subpage || existing->mr == &io_mem_unassigned);

    if (!(existing->mr->subpage)) {
        subpage = subpage_init(fv, base);
        subsection.fv = fv;
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


static void register_multipage(FlatView *fv,
                               MemoryRegionSection *section)
{
    AddressSpaceDispatch *d = flatview_to_dispatch(fv);
    hwaddr start_addr = section->offset_within_address_space;
    uint16_t section_index = phys_section_add(&d->map, section);
    uint64_t num_pages = int128_get64(int128_rshift(section->size,
                                                    TARGET_PAGE_BITS));

    assert(num_pages);
    phys_page_set(d, start_addr >> TARGET_PAGE_BITS, num_pages, section_index);
}

/*
 * The range in *section* may look like this:
 *
 *      |s|PPPPPPP|s|
 *
 * where s stands for subpage and P for page.
 */
void flatview_add_to_dispatch(FlatView *fv, MemoryRegionSection *section)
{
    MemoryRegionSection remain = *section;
    Int128 page_size = int128_make64(TARGET_PAGE_SIZE);

    /* register first subpage */
    if (remain.offset_within_address_space & ~TARGET_PAGE_MASK) {
        uint64_t left = TARGET_PAGE_ALIGN(remain.offset_within_address_space)
                        - remain.offset_within_address_space;

        MemoryRegionSection now = remain;
        now.size = int128_min(int128_make64(left), now.size);
        register_subpage(fv, &now);
        if (int128_eq(remain.size, now.size)) {
            return;
        }
        remain.size = int128_sub(remain.size, now.size);
        remain.offset_within_address_space += int128_get64(now.size);
        remain.offset_within_region += int128_get64(now.size);
    }

    /* register whole pages */
    if (int128_ge(remain.size, page_size)) {
        MemoryRegionSection now = remain;
        now.size = int128_and(now.size, int128_neg(page_size));
        register_multipage(fv, &now);
        if (int128_eq(remain.size, now.size)) {
            return;
        }
        remain.size = int128_sub(remain.size, now.size);
        remain.offset_within_address_space += int128_get64(now.size);
        remain.offset_within_region += int128_get64(now.size);
    }

    /* register last subpage */
    register_subpage(fv, &remain);
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

GString *ram_block_format(void)
{
    RAMBlock *block;
    char *psize;
    GString *buf = g_string_new("");

    RCU_READ_LOCK_GUARD();
    g_string_append_printf(buf, "%24s %8s  %18s %18s %18s %18s %3s\n",
                           "Block Name", "PSize", "Offset", "Used", "Total",
                           "HVA", "RO");

    RAMBLOCK_FOREACH(block) {
        psize = size_to_str(block->page_size);
        g_string_append_printf(buf, "%24s %8s  0x%016" PRIx64 " 0x%016" PRIx64
                               " 0x%016" PRIx64 " 0x%016" PRIx64 " %3s\n",
                               block->idstr, psize,
                               (uint64_t)block->offset,
                               (uint64_t)block->used_length,
                               (uint64_t)block->max_length,
                               (uint64_t)(uintptr_t)block->host,
                               block->mr->readonly ? "ro" : "rw");

        g_free(psize);
    }

    return buf;
}

static int find_min_backend_pagesize(Object *obj, void *opaque)
{
    long *hpsize_min = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        HostMemoryBackend *backend = MEMORY_BACKEND(obj);
        long hpsize = host_memory_backend_pagesize(backend);

        if (host_memory_backend_is_mapped(backend) && (hpsize < *hpsize_min)) {
            *hpsize_min = hpsize;
        }
    }

    return 0;
}

static int find_max_backend_pagesize(Object *obj, void *opaque)
{
    long *hpsize_max = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        HostMemoryBackend *backend = MEMORY_BACKEND(obj);
        long hpsize = host_memory_backend_pagesize(backend);

        if (host_memory_backend_is_mapped(backend) && (hpsize > *hpsize_max)) {
            *hpsize_max = hpsize;
        }
    }

    return 0;
}

/*
 * TODO: We assume right now that all mapped host memory backends are
 * used as RAM, however some might be used for different purposes.
 */
long qemu_minrampagesize(void)
{
    long hpsize = LONG_MAX;
    Object *memdev_root = object_resolve_path("/objects", NULL);

    object_child_foreach(memdev_root, find_min_backend_pagesize, &hpsize);
    return hpsize;
}

long qemu_maxrampagesize(void)
{
    long pagesize = 0;
    Object *memdev_root = object_resolve_path("/objects", NULL);

    object_child_foreach(memdev_root, find_max_backend_pagesize, &pagesize);
    return pagesize;
}

#ifdef CONFIG_POSIX
static int64_t get_file_size(int fd)
{
    int64_t size;
#if defined(__linux__)
    struct stat st;

    if (fstat(fd, &st) < 0) {
        return -errno;
    }

    /* Special handling for devdax character devices */
    if (S_ISCHR(st.st_mode)) {
        g_autofree char *subsystem_path = NULL;
        g_autofree char *subsystem = NULL;

        subsystem_path = g_strdup_printf("/sys/dev/char/%d:%d/subsystem",
                                         major(st.st_rdev), minor(st.st_rdev));
        subsystem = g_file_read_link(subsystem_path, NULL);

        if (subsystem && g_str_has_suffix(subsystem, "/dax")) {
            g_autofree char *size_path = NULL;
            g_autofree char *size_str = NULL;

            size_path = g_strdup_printf("/sys/dev/char/%d:%d/size",
                                    major(st.st_rdev), minor(st.st_rdev));

            if (g_file_get_contents(size_path, &size_str, NULL, NULL)) {
                return g_ascii_strtoll(size_str, NULL, 0);
            }
        }
    }
#endif /* defined(__linux__) */

    /* st.st_size may be zero for special files yet lseek(2) works */
    size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        return -errno;
    }
    return size;
}

static int64_t get_file_align(int fd)
{
    int64_t align = -1;
#if defined(__linux__) && defined(CONFIG_LIBDAXCTL)
    struct stat st;

    if (fstat(fd, &st) < 0) {
        return -errno;
    }

    /* Special handling for devdax character devices */
    if (S_ISCHR(st.st_mode)) {
        g_autofree char *path = NULL;
        g_autofree char *rpath = NULL;
        struct daxctl_ctx *ctx;
        struct daxctl_region *region;
        int rc = 0;

        path = g_strdup_printf("/sys/dev/char/%d:%d",
                    major(st.st_rdev), minor(st.st_rdev));
        rpath = realpath(path, NULL);
        if (!rpath) {
            return -errno;
        }

        rc = daxctl_new(&ctx);
        if (rc) {
            return -1;
        }

        daxctl_region_foreach(ctx, region) {
            if (strstr(rpath, daxctl_region_get_path(region))) {
                align = daxctl_region_get_align(region);
                break;
            }
        }
        daxctl_unref(ctx);
    }
#endif /* defined(__linux__) && defined(CONFIG_LIBDAXCTL) */

    return align;
}

static int file_ram_open(const char *path,
                         const char *region_name,
                         bool readonly,
                         bool *created,
                         Error **errp)
{
    char *filename;
    char *sanitized_name;
    char *c;
    int fd = -1;

    *created = false;
    for (;;) {
        fd = open(path, readonly ? O_RDONLY : O_RDWR);
        if (fd >= 0) {
            /* @path names an existing file, use it */
            break;
        }
        if (errno == ENOENT) {
            /* @path names a file that doesn't exist, create it */
            fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
            if (fd >= 0) {
                *created = true;
                break;
            }
        } else if (errno == EISDIR) {
            /* @path names a directory, create a file there */
            /* Make name safe to use with mkstemp by replacing '/' with '_'. */
            sanitized_name = g_strdup(region_name);
            for (c = sanitized_name; *c != '\0'; c++) {
                if (*c == '/') {
                    *c = '_';
                }
            }

            filename = g_strdup_printf("%s/qemu_back_mem.%s.XXXXXX", path,
                                       sanitized_name);
            g_free(sanitized_name);

            fd = mkstemp(filename);
            if (fd >= 0) {
                unlink(filename);
                g_free(filename);
                break;
            }
            g_free(filename);
        }
        if (errno != EEXIST && errno != EINTR) {
            error_setg_errno(errp, errno,
                             "can't open backing store %s for guest RAM",
                             path);
            return -1;
        }
        /*
         * Try again on EINTR and EEXIST.  The latter happens when
         * something else creates the file between our two open().
         */
    }

    return fd;
}

static void *file_ram_alloc(RAMBlock *block,
                            ram_addr_t memory,
                            int fd,
                            bool readonly,
                            bool truncate,
                            off_t offset,
                            Error **errp)
{
    uint32_t qemu_map_flags;
    void *area;

    block->page_size = qemu_fd_getpagesize(fd);
    if (block->mr->align % block->page_size) {
        error_setg(errp, "alignment 0x%" PRIx64
                   " must be multiples of page size 0x%zx",
                   block->mr->align, block->page_size);
        return NULL;
    } else if (block->mr->align && !is_power_of_2(block->mr->align)) {
        error_setg(errp, "alignment 0x%" PRIx64
                   " must be a power of two", block->mr->align);
        return NULL;
    } else if (offset % block->page_size) {
        error_setg(errp, "offset 0x%" PRIx64
                   " must be multiples of page size 0x%zx",
                   offset, block->page_size);
        return NULL;
    }
    block->mr->align = MAX(block->page_size, block->mr->align);
#if defined(__s390x__)
    if (kvm_enabled()) {
        block->mr->align = MAX(block->mr->align, QEMU_VMALLOC_ALIGN);
    }
#endif

    if (memory < block->page_size) {
        error_setg(errp, "memory size 0x" RAM_ADDR_FMT " must be equal to "
                   "or larger than page size 0x%zx",
                   memory, block->page_size);
        return NULL;
    }

    memory = ROUND_UP(memory, block->page_size);

    /*
     * ftruncate is not supported by hugetlbfs in older
     * hosts, so don't bother bailing out on errors.
     * If anything goes wrong with it under other filesystems,
     * mmap will fail.
     *
     * Do not truncate the non-empty backend file to avoid corrupting
     * the existing data in the file. Disabling shrinking is not
     * enough. For example, the current vNVDIMM implementation stores
     * the guest NVDIMM labels at the end of the backend file. If the
     * backend file is later extended, QEMU will not be able to find
     * those labels. Therefore, extending the non-empty backend file
     * is disabled as well.
     */
    if (truncate && ftruncate(fd, offset + memory)) {
        perror("ftruncate");
    }

    qemu_map_flags = readonly ? QEMU_MAP_READONLY : 0;
    qemu_map_flags |= (block->flags & RAM_SHARED) ? QEMU_MAP_SHARED : 0;
    qemu_map_flags |= (block->flags & RAM_PMEM) ? QEMU_MAP_SYNC : 0;
    qemu_map_flags |= (block->flags & RAM_NORESERVE) ? QEMU_MAP_NORESERVE : 0;
    area = qemu_ram_mmap(fd, memory, block->mr->align, qemu_map_flags, offset);
    if (area == MAP_FAILED) {
        error_setg_errno(errp, errno,
                         "unable to map backing store for guest RAM");
        return NULL;
    }

    block->fd = fd;
    block->fd_offset = offset;
    return area;
}
#endif

/* Allocate space within the ram_addr_t space that governs the
 * dirty bitmaps.
 * Called with the ramlist lock held.
 */
static ram_addr_t find_ram_offset(ram_addr_t size)
{
    RAMBlock *block, *next_block;
    ram_addr_t offset = RAM_ADDR_MAX, mingap = RAM_ADDR_MAX;

    assert(size != 0); /* it would hand out same offset multiple times */

    if (QLIST_EMPTY_RCU(&ram_list.blocks)) {
        return 0;
    }

    RAMBLOCK_FOREACH(block) {
        ram_addr_t candidate, next = RAM_ADDR_MAX;

        /* Align blocks to start on a 'long' in the bitmap
         * which makes the bitmap sync'ing take the fast path.
         */
        candidate = block->offset + block->max_length;
        candidate = ROUND_UP(candidate, BITS_PER_LONG << TARGET_PAGE_BITS);

        /* Search for the closest following block
         * and find the gap.
         */
        RAMBLOCK_FOREACH(next_block) {
            if (next_block->offset >= candidate) {
                next = MIN(next, next_block->offset);
            }
        }

        /* If it fits remember our place and remember the size
         * of gap, but keep going so that we might find a smaller
         * gap to fill so avoiding fragmentation.
         */
        if (next - candidate >= size && next - candidate < mingap) {
            offset = candidate;
            mingap = next - candidate;
        }

        trace_find_ram_offset_loop(size, candidate, offset, next, mingap);
    }

    if (offset == RAM_ADDR_MAX) {
        fprintf(stderr, "Failed to find gap of requested size: %" PRIu64 "\n",
                (uint64_t)size);
        abort();
    }

    trace_find_ram_offset(size, offset);

    return offset;
}

static unsigned long last_ram_page(void)
{
    RAMBlock *block;
    ram_addr_t last = 0;

    RCU_READ_LOCK_GUARD();
    RAMBLOCK_FOREACH(block) {
        last = MAX(last, block->offset + block->max_length);
    }
    return last >> TARGET_PAGE_BITS;
}

static void qemu_ram_setup_dump(void *addr, ram_addr_t size)
{
    int ret;

    /* Use MADV_DONTDUMP, if user doesn't want the guest memory in the core */
    if (!machine_dump_guest_core(current_machine)) {
        ret = qemu_madvise(addr, size, QEMU_MADV_DONTDUMP);
        if (ret) {
            perror("qemu_madvise");
            fprintf(stderr, "madvise doesn't support MADV_DONTDUMP, "
                            "but dump_guest_core=off specified\n");
        }
    }
}

const char *qemu_ram_get_idstr(RAMBlock *rb)
{
    return rb->idstr;
}

void *qemu_ram_get_host_addr(RAMBlock *rb)
{
    return rb->host;
}

ram_addr_t qemu_ram_get_offset(RAMBlock *rb)
{
    return rb->offset;
}

ram_addr_t qemu_ram_get_used_length(RAMBlock *rb)
{
    return rb->used_length;
}

ram_addr_t qemu_ram_get_max_length(RAMBlock *rb)
{
    return rb->max_length;
}

bool qemu_ram_is_shared(RAMBlock *rb)
{
    return rb->flags & RAM_SHARED;
}

bool qemu_ram_is_noreserve(RAMBlock *rb)
{
    return rb->flags & RAM_NORESERVE;
}

/* Note: Only set at the start of postcopy */
bool qemu_ram_is_uf_zeroable(RAMBlock *rb)
{
    return rb->flags & RAM_UF_ZEROPAGE;
}

void qemu_ram_set_uf_zeroable(RAMBlock *rb)
{
    rb->flags |= RAM_UF_ZEROPAGE;
}

bool qemu_ram_is_migratable(RAMBlock *rb)
{
    return rb->flags & RAM_MIGRATABLE;
}

void qemu_ram_set_migratable(RAMBlock *rb)
{
    rb->flags |= RAM_MIGRATABLE;
}

void qemu_ram_unset_migratable(RAMBlock *rb)
{
    rb->flags &= ~RAM_MIGRATABLE;
}

bool qemu_ram_is_named_file(RAMBlock *rb)
{
    return rb->flags & RAM_NAMED_FILE;
}

int qemu_ram_get_fd(RAMBlock *rb)
{
    return rb->fd;
}

/* Called with iothread lock held.  */
void qemu_ram_set_idstr(RAMBlock *new_block, const char *name, DeviceState *dev)
{
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

    RCU_READ_LOCK_GUARD();
    RAMBLOCK_FOREACH(block) {
        if (block != new_block &&
            !strcmp(block->idstr, new_block->idstr)) {
            fprintf(stderr, "RAMBlock \"%s\" already registered, abort!\n",
                    new_block->idstr);
            abort();
        }
    }
}

/* Called with iothread lock held.  */
void qemu_ram_unset_idstr(RAMBlock *block)
{
    /* FIXME: arch_init.c assumes that this is not called throughout
     * migration.  Ignore the problem since hot-unplug during migration
     * does not work anyway.
     */
    if (block) {
        memset(block->idstr, 0, sizeof(block->idstr));
    }
}

size_t qemu_ram_pagesize(RAMBlock *rb)
{
    return rb->page_size;
}

/* Returns the largest size of page in use */
size_t qemu_ram_pagesize_largest(void)
{
    RAMBlock *block;
    size_t largest = 0;

    RAMBLOCK_FOREACH(block) {
        largest = MAX(largest, qemu_ram_pagesize(block));
    }

    return largest;
}

static int memory_try_enable_merging(void *addr, size_t len)
{
    if (!machine_mem_merge(current_machine)) {
        /* disabled by the user */
        return 0;
    }

    return qemu_madvise(addr, len, QEMU_MADV_MERGEABLE);
}

/*
 * Resizing RAM while migrating can result in the migration being canceled.
 * Care has to be taken if the guest might have already detected the memory.
 *
 * As memory core doesn't know how is memory accessed, it is up to
 * resize callback to update device state and/or add assertions to detect
 * misuse, if necessary.
 */
int qemu_ram_resize(RAMBlock *block, ram_addr_t newsize, Error **errp)
{
    const ram_addr_t oldsize = block->used_length;
    const ram_addr_t unaligned_size = newsize;

    assert(block);

    newsize = HOST_PAGE_ALIGN(newsize);

    if (block->used_length == newsize) {
        /*
         * We don't have to resize the ram block (which only knows aligned
         * sizes), however, we have to notify if the unaligned size changed.
         */
        if (unaligned_size != memory_region_size(block->mr)) {
            memory_region_set_size(block->mr, unaligned_size);
            if (block->resized) {
                block->resized(block->idstr, unaligned_size, block->host);
            }
        }
        return 0;
    }

    if (!(block->flags & RAM_RESIZEABLE)) {
        error_setg_errno(errp, EINVAL,
                         "Size mismatch: %s: 0x" RAM_ADDR_FMT
                         " != 0x" RAM_ADDR_FMT, block->idstr,
                         newsize, block->used_length);
        return -EINVAL;
    }

    if (block->max_length < newsize) {
        error_setg_errno(errp, EINVAL,
                         "Size too large: %s: 0x" RAM_ADDR_FMT
                         " > 0x" RAM_ADDR_FMT, block->idstr,
                         newsize, block->max_length);
        return -EINVAL;
    }

    /* Notify before modifying the ram block and touching the bitmaps. */
    if (block->host) {
        ram_block_notify_resize(block->host, oldsize, newsize);
    }

    cpu_physical_memory_clear_dirty_range(block->offset, block->used_length);
    block->used_length = newsize;
    cpu_physical_memory_set_dirty_range(block->offset, block->used_length,
                                        DIRTY_CLIENTS_ALL);
    memory_region_set_size(block->mr, unaligned_size);
    if (block->resized) {
        block->resized(block->idstr, unaligned_size, block->host);
    }
    return 0;
}

/*
 * Trigger sync on the given ram block for range [start, start + length]
 * with the backing store if one is available.
 * Otherwise no-op.
 * @Note: this is supposed to be a synchronous op.
 */
void qemu_ram_msync(RAMBlock *block, ram_addr_t start, ram_addr_t length)
{
    /* The requested range should fit in within the block range */
    g_assert((start + length) <= block->used_length);

#ifdef CONFIG_LIBPMEM
    /* The lack of support for pmem should not block the sync */
    if (ramblock_is_pmem(block)) {
        void *addr = ramblock_ptr(block, start);
        pmem_persist(addr, length);
        return;
    }
#endif
    if (block->fd >= 0) {
        /**
         * Case there is no support for PMEM or the memory has not been
         * specified as persistent (or is not one) - use the msync.
         * Less optimal but still achieves the same goal
         */
        void *addr = ramblock_ptr(block, start);
        if (qemu_msync(addr, length, block->fd)) {
            warn_report("%s: failed to sync memory range: start: "
                    RAM_ADDR_FMT " length: " RAM_ADDR_FMT,
                    __func__, start, length);
        }
    }
}

/* Called with ram_list.mutex held */
static void dirty_memory_extend(ram_addr_t old_ram_size,
                                ram_addr_t new_ram_size)
{
    ram_addr_t old_num_blocks = DIV_ROUND_UP(old_ram_size,
                                             DIRTY_MEMORY_BLOCK_SIZE);
    ram_addr_t new_num_blocks = DIV_ROUND_UP(new_ram_size,
                                             DIRTY_MEMORY_BLOCK_SIZE);
    int i;

    /* Only need to extend if block count increased */
    if (new_num_blocks <= old_num_blocks) {
        return;
    }

    for (i = 0; i < DIRTY_MEMORY_NUM; i++) {
        DirtyMemoryBlocks *old_blocks;
        DirtyMemoryBlocks *new_blocks;
        int j;

        old_blocks = qatomic_rcu_read(&ram_list.dirty_memory[i]);
        new_blocks = g_malloc(sizeof(*new_blocks) +
                              sizeof(new_blocks->blocks[0]) * new_num_blocks);

        if (old_num_blocks) {
            memcpy(new_blocks->blocks, old_blocks->blocks,
                   old_num_blocks * sizeof(old_blocks->blocks[0]));
        }

        for (j = old_num_blocks; j < new_num_blocks; j++) {
            new_blocks->blocks[j] = bitmap_new(DIRTY_MEMORY_BLOCK_SIZE);
        }

        qatomic_rcu_set(&ram_list.dirty_memory[i], new_blocks);

        if (old_blocks) {
            g_free_rcu(old_blocks, rcu);
        }
    }
}

static void ram_block_add(RAMBlock *new_block, Error **errp)
{
    const bool noreserve = qemu_ram_is_noreserve(new_block);
    const bool shared = qemu_ram_is_shared(new_block);
    RAMBlock *block;
    RAMBlock *last_block = NULL;
    ram_addr_t old_ram_size, new_ram_size;
    Error *err = NULL;

    old_ram_size = last_ram_page();

    qemu_mutex_lock_ramlist();
    new_block->offset = find_ram_offset(new_block->max_length);

    if (!new_block->host) {
        if (xen_enabled()) {
            xen_ram_alloc(new_block->offset, new_block->max_length,
                          new_block->mr, &err);
            if (err) {
                error_propagate(errp, err);
                qemu_mutex_unlock_ramlist();
                return;
            }
        } else {
            new_block->host = qemu_anon_ram_alloc(new_block->max_length,
                                                  &new_block->mr->align,
                                                  shared, noreserve);
            if (!new_block->host) {
                error_setg_errno(errp, errno,
                                 "cannot set up guest memory '%s'",
                                 memory_region_name(new_block->mr));
                qemu_mutex_unlock_ramlist();
                return;
            }
            memory_try_enable_merging(new_block->host, new_block->max_length);
        }
    }

    new_ram_size = MAX(old_ram_size,
              (new_block->offset + new_block->max_length) >> TARGET_PAGE_BITS);
    if (new_ram_size > old_ram_size) {
        dirty_memory_extend(old_ram_size, new_ram_size);
    }
    /* Keep the list sorted from biggest to smallest block.  Unlike QTAILQ,
     * QLIST (which has an RCU-friendly variant) does not have insertion at
     * tail, so save the last element in last_block.
     */
    RAMBLOCK_FOREACH(block) {
        last_block = block;
        if (block->max_length < new_block->max_length) {
            break;
        }
    }
    if (block) {
        QLIST_INSERT_BEFORE_RCU(block, new_block, next);
    } else if (last_block) {
        QLIST_INSERT_AFTER_RCU(last_block, new_block, next);
    } else { /* list is empty */
        QLIST_INSERT_HEAD_RCU(&ram_list.blocks, new_block, next);
    }
    ram_list.mru_block = NULL;

    /* Write list before version */
    smp_wmb();
    ram_list.version++;
    qemu_mutex_unlock_ramlist();

    cpu_physical_memory_set_dirty_range(new_block->offset,
                                        new_block->used_length,
                                        DIRTY_CLIENTS_ALL);

    if (new_block->host) {
        qemu_ram_setup_dump(new_block->host, new_block->max_length);
        qemu_madvise(new_block->host, new_block->max_length, QEMU_MADV_HUGEPAGE);
        /*
         * MADV_DONTFORK is also needed by KVM in absence of synchronous MMU
         * Configure it unless the machine is a qtest server, in which case
         * KVM is not used and it may be forked (eg for fuzzing purposes).
         */
        if (!qtest_enabled()) {
            qemu_madvise(new_block->host, new_block->max_length,
                         QEMU_MADV_DONTFORK);
        }
        ram_block_notify_add(new_block->host, new_block->used_length,
                             new_block->max_length);
    }
}

#ifdef CONFIG_POSIX
RAMBlock *qemu_ram_alloc_from_fd(ram_addr_t size, MemoryRegion *mr,
                                 uint32_t ram_flags, int fd, off_t offset,
                                 bool readonly, Error **errp)
{
    RAMBlock *new_block;
    Error *local_err = NULL;
    int64_t file_size, file_align;

    /* Just support these ram flags by now. */
    assert((ram_flags & ~(RAM_SHARED | RAM_PMEM | RAM_NORESERVE |
                          RAM_PROTECTED | RAM_NAMED_FILE)) == 0);

    if (xen_enabled()) {
        error_setg(errp, "-mem-path not supported with Xen");
        return NULL;
    }

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        error_setg(errp,
                   "host lacks kvm mmu notifiers, -mem-path unsupported");
        return NULL;
    }

    size = HOST_PAGE_ALIGN(size);
    file_size = get_file_size(fd);
    if (file_size > offset && file_size < (offset + size)) {
        error_setg(errp, "backing store size 0x%" PRIx64
                   " does not match 'size' option 0x" RAM_ADDR_FMT,
                   file_size, size);
        return NULL;
    }

    file_align = get_file_align(fd);
    if (file_align > 0 && file_align > mr->align) {
        error_setg(errp, "backing store align 0x%" PRIx64
                   " is larger than 'align' option 0x%" PRIx64,
                   file_align, mr->align);
        return NULL;
    }

    new_block = g_malloc0(sizeof(*new_block));
    new_block->mr = mr;
    new_block->used_length = size;
    new_block->max_length = size;
    new_block->flags = ram_flags;
    new_block->host = file_ram_alloc(new_block, size, fd, readonly,
                                     !file_size, offset, errp);
    if (!new_block->host) {
        g_free(new_block);
        return NULL;
    }

    ram_block_add(new_block, &local_err);
    if (local_err) {
        g_free(new_block);
        error_propagate(errp, local_err);
        return NULL;
    }
    return new_block;

}


RAMBlock *qemu_ram_alloc_from_file(ram_addr_t size, MemoryRegion *mr,
                                   uint32_t ram_flags, const char *mem_path,
                                   off_t offset, bool readonly, Error **errp)
{
    int fd;
    bool created;
    RAMBlock *block;

    fd = file_ram_open(mem_path, memory_region_name(mr), readonly, &created,
                       errp);
    if (fd < 0) {
        return NULL;
    }

    block = qemu_ram_alloc_from_fd(size, mr, ram_flags, fd, offset, readonly,
                                   errp);
    if (!block) {
        if (created) {
            unlink(mem_path);
        }
        close(fd);
        return NULL;
    }

    return block;
}
#endif

static
RAMBlock *qemu_ram_alloc_internal(ram_addr_t size, ram_addr_t max_size,
                                  void (*resized)(const char*,
                                                  uint64_t length,
                                                  void *host),
                                  void *host, uint32_t ram_flags,
                                  MemoryRegion *mr, Error **errp)
{
    RAMBlock *new_block;
    Error *local_err = NULL;

    assert((ram_flags & ~(RAM_SHARED | RAM_RESIZEABLE | RAM_PREALLOC |
                          RAM_NORESERVE)) == 0);
    assert(!host ^ (ram_flags & RAM_PREALLOC));

    size = HOST_PAGE_ALIGN(size);
    max_size = HOST_PAGE_ALIGN(max_size);
    new_block = g_malloc0(sizeof(*new_block));
    new_block->mr = mr;
    new_block->resized = resized;
    new_block->used_length = size;
    new_block->max_length = max_size;
    assert(max_size >= size);
    new_block->fd = -1;
    new_block->page_size = qemu_real_host_page_size();
    new_block->host = host;
    new_block->flags = ram_flags;
    ram_block_add(new_block, &local_err);
    if (local_err) {
        g_free(new_block);
        error_propagate(errp, local_err);
        return NULL;
    }
    return new_block;
}

RAMBlock *qemu_ram_alloc_from_ptr(ram_addr_t size, void *host,
                                   MemoryRegion *mr, Error **errp)
{
    return qemu_ram_alloc_internal(size, size, NULL, host, RAM_PREALLOC, mr,
                                   errp);
}

RAMBlock *qemu_ram_alloc(ram_addr_t size, uint32_t ram_flags,
                         MemoryRegion *mr, Error **errp)
{
    assert((ram_flags & ~(RAM_SHARED | RAM_NORESERVE)) == 0);
    return qemu_ram_alloc_internal(size, size, NULL, NULL, ram_flags, mr, errp);
}

RAMBlock *qemu_ram_alloc_resizeable(ram_addr_t size, ram_addr_t maxsz,
                                     void (*resized)(const char*,
                                                     uint64_t length,
                                                     void *host),
                                     MemoryRegion *mr, Error **errp)
{
    return qemu_ram_alloc_internal(size, maxsz, resized, NULL,
                                   RAM_RESIZEABLE, mr, errp);
}

static void reclaim_ramblock(RAMBlock *block)
{
    if (block->flags & RAM_PREALLOC) {
        ;
    } else if (xen_enabled()) {
        xen_invalidate_map_cache_entry(block->host);
#ifndef _WIN32
    } else if (block->fd >= 0) {
        qemu_ram_munmap(block->fd, block->host, block->max_length);
        close(block->fd);
#endif
    } else {
        qemu_anon_ram_free(block->host, block->max_length);
    }
    g_free(block);
}

void qemu_ram_free(RAMBlock *block)
{
    if (!block) {
        return;
    }

    if (block->host) {
        ram_block_notify_remove(block->host, block->used_length,
                                block->max_length);
    }

    qemu_mutex_lock_ramlist();
    QLIST_REMOVE_RCU(block, next);
    ram_list.mru_block = NULL;
    /* Write list before version */
    smp_wmb();
    ram_list.version++;
    call_rcu(block, reclaim_ramblock, rcu);
    qemu_mutex_unlock_ramlist();
}

#ifndef _WIN32
void qemu_ram_remap(ram_addr_t addr, ram_addr_t length)
{
    RAMBlock *block;
    ram_addr_t offset;
    int flags;
    void *area, *vaddr;

    RAMBLOCK_FOREACH(block) {
        offset = addr - block->offset;
        if (offset < block->max_length) {
            vaddr = ramblock_ptr(block, offset);
            if (block->flags & RAM_PREALLOC) {
                ;
            } else if (xen_enabled()) {
                abort();
            } else {
                flags = MAP_FIXED;
                flags |= block->flags & RAM_SHARED ?
                         MAP_SHARED : MAP_PRIVATE;
                flags |= block->flags & RAM_NORESERVE ? MAP_NORESERVE : 0;
                if (block->fd >= 0) {
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, block->fd, offset + block->fd_offset);
                } else {
                    flags |= MAP_ANONYMOUS;
                    area = mmap(vaddr, length, PROT_READ | PROT_WRITE,
                                flags, -1, 0);
                }
                if (area != vaddr) {
                    error_report("Could not remap addr: "
                                 RAM_ADDR_FMT "@" RAM_ADDR_FMT "",
                                 length, addr);
                    exit(1);
                }
                memory_try_enable_merging(vaddr, length);
                qemu_ram_setup_dump(vaddr, length);
            }
        }
    }
}
#endif /* !_WIN32 */

/* Return a host pointer to ram allocated with qemu_ram_alloc.
 * This should not be used for general purpose DMA.  Use address_space_map
 * or address_space_rw instead. For local memory (e.g. video ram) that the
 * device owns, use memory_region_get_ram_ptr.
 *
 * Called within RCU critical section.
 */
void *qemu_map_ram_ptr(RAMBlock *ram_block, ram_addr_t addr)
{
    RAMBlock *block = ram_block;

    if (block == NULL) {
        block = qemu_get_ram_block(addr);
        addr -= block->offset;
    }

    if (xen_enabled() && block->host == NULL) {
        /* We need to check if the requested address is in the RAM
         * because we don't want to map the entire memory in QEMU.
         * In that case just map until the end of the page.
         */
        if (block->offset == 0) {
            return xen_map_cache(addr, 0, 0, false);
        }

        block->host = xen_map_cache(block->offset, block->max_length, 1, false);
    }
    return ramblock_ptr(block, addr);
}

/* Return a host pointer to guest's ram. Similar to qemu_map_ram_ptr
 * but takes a size argument.
 *
 * Called within RCU critical section.
 */
static void *qemu_ram_ptr_length(RAMBlock *ram_block, ram_addr_t addr,
                                 hwaddr *size, bool lock)
{
    RAMBlock *block = ram_block;
    if (*size == 0) {
        return NULL;
    }

    if (block == NULL) {
        block = qemu_get_ram_block(addr);
        addr -= block->offset;
    }
    *size = MIN(*size, block->max_length - addr);

    if (xen_enabled() && block->host == NULL) {
        /* We need to check if the requested address is in the RAM
         * because we don't want to map the entire memory in QEMU.
         * In that case just map the requested area.
         */
        if (block->offset == 0) {
            return xen_map_cache(addr, *size, lock, lock);
        }

        block->host = xen_map_cache(block->offset, block->max_length, 1, lock);
    }

    return ramblock_ptr(block, addr);
}

/* Return the offset of a hostpointer within a ramblock */
ram_addr_t qemu_ram_block_host_offset(RAMBlock *rb, void *host)
{
    ram_addr_t res = (uint8_t *)host - (uint8_t *)rb->host;
    assert((uintptr_t)host >= (uintptr_t)rb->host);
    assert(res < rb->max_length);

    return res;
}

/*
 * Translates a host ptr back to a RAMBlock, a ram_addr and an offset
 * in that RAMBlock.
 *
 * ptr: Host pointer to look up
 * round_offset: If true round the result offset down to a page boundary
 * *ram_addr: set to result ram_addr
 * *offset: set to result offset within the RAMBlock
 *
 * Returns: RAMBlock (or NULL if not found)
 *
 * By the time this function returns, the returned pointer is not protected
 * by RCU anymore.  If the caller is not within an RCU critical section and
 * does not hold the iothread lock, it must have other means of protecting the
 * pointer, such as a reference to the region that includes the incoming
 * ram_addr_t.
 */
RAMBlock *qemu_ram_block_from_host(void *ptr, bool round_offset,
                                   ram_addr_t *offset)
{
    RAMBlock *block;
    uint8_t *host = ptr;

    if (xen_enabled()) {
        ram_addr_t ram_addr;
        RCU_READ_LOCK_GUARD();
        ram_addr = xen_ram_addr_from_mapcache(ptr);
        block = qemu_get_ram_block(ram_addr);
        if (block) {
            *offset = ram_addr - block->offset;
        }
        return block;
    }

    RCU_READ_LOCK_GUARD();
    block = qatomic_rcu_read(&ram_list.mru_block);
    if (block && block->host && host - block->host < block->max_length) {
        goto found;
    }

    RAMBLOCK_FOREACH(block) {
        /* This case append when the block is not mapped. */
        if (block->host == NULL) {
            continue;
        }
        if (host - block->host < block->max_length) {
            goto found;
        }
    }

    return NULL;

found:
    *offset = (host - block->host);
    if (round_offset) {
        *offset &= TARGET_PAGE_MASK;
    }
    return block;
}

/*
 * Finds the named RAMBlock
 *
 * name: The name of RAMBlock to find
 *
 * Returns: RAMBlock (or NULL if not found)
 */
RAMBlock *qemu_ram_block_by_name(const char *name)
{
    RAMBlock *block;

    RAMBLOCK_FOREACH(block) {
        if (!strcmp(name, block->idstr)) {
            return block;
        }
    }

    return NULL;
}

/* Some of the softmmu routines need to translate from a host pointer
   (typically a TLB entry) back to a ram offset.  */
ram_addr_t qemu_ram_addr_from_host(void *ptr)
{
    RAMBlock *block;
    ram_addr_t offset;

    block = qemu_ram_block_from_host(ptr, false, &offset);
    if (!block) {
        return RAM_ADDR_INVALID;
    }

    return block->offset + offset;
}

ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr)
{
    ram_addr_t ram_addr;

    ram_addr = qemu_ram_addr_from_host(ptr);
    if (ram_addr == RAM_ADDR_INVALID) {
        error_report("Bad ram pointer %p", ptr);
        abort();
    }
    return ram_addr;
}

static MemTxResult flatview_read(FlatView *fv, hwaddr addr,
                                 MemTxAttrs attrs, void *buf, hwaddr len);
static MemTxResult flatview_write(FlatView *fv, hwaddr addr, MemTxAttrs attrs,
                                  const void *buf, hwaddr len);
static bool flatview_access_valid(FlatView *fv, hwaddr addr, hwaddr len,
                                  bool is_write, MemTxAttrs attrs);

static MemTxResult subpage_read(void *opaque, hwaddr addr, uint64_t *data,
                                unsigned len, MemTxAttrs attrs)
{
    subpage_t *subpage = opaque;
    uint8_t buf[8];
    MemTxResult res;

#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %u addr " HWADDR_FMT_plx "\n", __func__,
           subpage, len, addr);
#endif
    res = flatview_read(subpage->fv, addr + subpage->base, attrs, buf, len);
    if (res) {
        return res;
    }
    *data = ldn_p(buf, len);
    return MEMTX_OK;
}

static MemTxResult subpage_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned len, MemTxAttrs attrs)
{
    subpage_t *subpage = opaque;
    uint8_t buf[8];

#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p len %u addr " HWADDR_FMT_plx
           " value %"PRIx64"\n",
           __func__, subpage, len, addr, value);
#endif
    stn_p(buf, len, value);
    return flatview_write(subpage->fv, addr + subpage->base, attrs, buf, len);
}

static bool subpage_accepts(void *opaque, hwaddr addr,
                            unsigned len, bool is_write,
                            MemTxAttrs attrs)
{
    subpage_t *subpage = opaque;
#if defined(DEBUG_SUBPAGE)
    printf("%s: subpage %p %c len %u addr " HWADDR_FMT_plx "\n",
           __func__, subpage, is_write ? 'w' : 'r', len, addr);
#endif

    return flatview_access_valid(subpage->fv, addr + subpage->base,
                                 len, is_write, attrs);
}

static const MemoryRegionOps subpage_ops = {
    .read_with_attrs = subpage_read,
    .write_with_attrs = subpage_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .valid.accepts = subpage_accepts,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int subpage_register(subpage_t *mmio, uint32_t start, uint32_t end,
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

static subpage_t *subpage_init(FlatView *fv, hwaddr base)
{
    subpage_t *mmio;

    /* mmio->sub_section is set to PHYS_SECTION_UNASSIGNED with g_malloc0 */
    mmio = g_malloc0(sizeof(subpage_t) + TARGET_PAGE_SIZE * sizeof(uint16_t));
    mmio->fv = fv;
    mmio->base = base;
    memory_region_init_io(&mmio->iomem, NULL, &subpage_ops, mmio,
                          NULL, TARGET_PAGE_SIZE);
    mmio->iomem.subpage = true;
#if defined(DEBUG_SUBPAGE)
    printf("%s: %p base " HWADDR_FMT_plx " len %08x\n", __func__,
           mmio, base, TARGET_PAGE_SIZE);
#endif

    return mmio;
}

static uint16_t dummy_section(PhysPageMap *map, FlatView *fv, MemoryRegion *mr)
{
    assert(fv);
    MemoryRegionSection section = {
        .fv = fv,
        .mr = mr,
        .offset_within_address_space = 0,
        .offset_within_region = 0,
        .size = int128_2_64(),
    };

    return phys_section_add(map, &section);
}

MemoryRegionSection *iotlb_to_section(CPUState *cpu,
                                      hwaddr index, MemTxAttrs attrs)
{
    int asidx = cpu_asidx_from_attrs(cpu, attrs);
    CPUAddressSpace *cpuas = &cpu->cpu_ases[asidx];
    AddressSpaceDispatch *d = qatomic_rcu_read(&cpuas->memory_dispatch);
    MemoryRegionSection *sections = d->map.sections;

    return &sections[index & ~TARGET_PAGE_MASK];
}

static void io_mem_init(void)
{
    memory_region_init_io(&io_mem_unassigned, NULL, &unassigned_mem_ops, NULL,
                          NULL, UINT64_MAX);
}

AddressSpaceDispatch *address_space_dispatch_new(FlatView *fv)
{
    AddressSpaceDispatch *d = g_new0(AddressSpaceDispatch, 1);
    uint16_t n;

    n = dummy_section(&d->map, fv, &io_mem_unassigned);
    assert(n == PHYS_SECTION_UNASSIGNED);

    d->phys_map  = (PhysPageEntry) { .ptr = PHYS_MAP_NODE_NIL, .skip = 1 };

    return d;
}

void address_space_dispatch_free(AddressSpaceDispatch *d)
{
    phys_sections_free(&d->map);
    g_free(d);
}

static void do_nothing(CPUState *cpu, run_on_cpu_data d)
{
}

static void tcg_log_global_after_sync(MemoryListener *listener)
{
    CPUAddressSpace *cpuas;

    /* Wait for the CPU to end the current TB.  This avoids the following
     * incorrect race:
     *
     *      vCPU                         migration
     *      ----------------------       -------------------------
     *      TLB check -> slow path
     *        notdirty_mem_write
     *          write to RAM
     *          mark dirty
     *                                   clear dirty flag
     *      TLB check -> fast path
     *                                   read memory
     *        write to RAM
     *
     * by pushing the migration thread's memory read after the vCPU thread has
     * written the memory.
     */
    if (replay_mode == REPLAY_MODE_NONE) {
        /*
         * VGA can make calls to this function while updating the screen.
         * In record/replay mode this causes a deadlock, because
         * run_on_cpu waits for rr mutex. Therefore no races are possible
         * in this case and no need for making run_on_cpu when
         * record/replay is enabled.
         */
        cpuas = container_of(listener, CPUAddressSpace, tcg_as_listener);
        run_on_cpu(cpuas->cpu, do_nothing, RUN_ON_CPU_NULL);
    }
}

static void tcg_commit(MemoryListener *listener)
{
    CPUAddressSpace *cpuas;
    AddressSpaceDispatch *d;

    assert(tcg_enabled());
    /* since each CPU stores ram addresses in its TLB cache, we must
       reset the modified entries */
    cpuas = container_of(listener, CPUAddressSpace, tcg_as_listener);
    cpu_reloading_memory_map();
    /* The CPU and TLB are protected by the iothread lock.
     * We reload the dispatch pointer now because cpu_reloading_memory_map()
     * may have split the RCU critical section.
     */
    d = address_space_to_dispatch(cpuas->as);
    qatomic_rcu_set(&cpuas->memory_dispatch, d);
    tlb_flush(cpuas->cpu);
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
}

MemoryRegion *get_system_memory(void)
{
    return system_memory;
}

MemoryRegion *get_system_io(void)
{
    return system_io;
}

static void invalidate_and_set_dirty(MemoryRegion *mr, hwaddr addr,
                                     hwaddr length)
{
    uint8_t dirty_log_mask = memory_region_get_dirty_log_mask(mr);
    addr += memory_region_get_ram_addr(mr);

    /* No early return if dirty_log_mask is or becomes 0, because
     * cpu_physical_memory_set_dirty_range will still call
     * xen_modified_memory.
     */
    if (dirty_log_mask) {
        dirty_log_mask =
            cpu_physical_memory_range_includes_clean(addr, length, dirty_log_mask);
    }
    if (dirty_log_mask & (1 << DIRTY_MEMORY_CODE)) {
        assert(tcg_enabled());
        tb_invalidate_phys_range(addr, addr + length - 1);
        dirty_log_mask &= ~(1 << DIRTY_MEMORY_CODE);
    }
    cpu_physical_memory_set_dirty_range(addr, length, dirty_log_mask);
}

void memory_region_flush_rom_device(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
    /*
     * In principle this function would work on other memory region types too,
     * but the ROM device use case is the only one where this operation is
     * necessary.  Other memory regions should use the
     * address_space_read/write() APIs.
     */
    assert(memory_region_is_romd(mr));

    invalidate_and_set_dirty(mr, addr, size);
}

int memory_access_size(MemoryRegion *mr, unsigned l, hwaddr addr)
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
    l = pow2floor(l);

    return l;
}

bool prepare_mmio_access(MemoryRegion *mr)
{
    bool release_lock = false;

    if (!qemu_mutex_iothread_locked()) {
        qemu_mutex_lock_iothread();
        release_lock = true;
    }
    if (mr->flush_coalesced_mmio) {
        qemu_flush_coalesced_mmio_buffer();
    }

    return release_lock;
}

/**
 * flatview_access_allowed
 * @mr: #MemoryRegion to be accessed
 * @attrs: memory transaction attributes
 * @addr: address within that memory region
 * @len: the number of bytes to access
 *
 * Check if a memory transaction is allowed.
 *
 * Returns: true if transaction is allowed, false if denied.
 */
static bool flatview_access_allowed(MemoryRegion *mr, MemTxAttrs attrs,
                                    hwaddr addr, hwaddr len)
{
    if (likely(!attrs.memory)) {
        return true;
    }
    if (memory_region_is_ram(mr)) {
        return true;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "Invalid access to non-RAM device at "
                  "addr 0x%" HWADDR_PRIX ", size %" HWADDR_PRIu ", "
                  "region '%s'\n", addr, len, memory_region_name(mr));
    return false;
}

/* Called within RCU critical section.  */
static MemTxResult flatview_write_continue(FlatView *fv, hwaddr addr,
                                           MemTxAttrs attrs,
                                           const void *ptr,
                                           hwaddr len, hwaddr addr1,
                                           hwaddr l, MemoryRegion *mr)
{
    uint8_t *ram_ptr;
    uint64_t val;
    MemTxResult result = MEMTX_OK;
    bool release_lock = false;
    const uint8_t *buf = ptr;

    for (;;) {
        if (!flatview_access_allowed(mr, attrs, addr1, l)) {
            result |= MEMTX_ACCESS_ERROR;
            /* Keep going. */
        } else if (!memory_access_is_direct(mr, true)) {
            release_lock |= prepare_mmio_access(mr);
            l = memory_access_size(mr, l, addr1);
            /* XXX: could force current_cpu to NULL to avoid
               potential bugs */
            val = ldn_he_p(buf, l);
            result |= memory_region_dispatch_write(mr, addr1, val,
                                                   size_memop(l), attrs);
        } else {
            /* RAM case */
            ram_ptr = qemu_ram_ptr_length(mr->ram_block, addr1, &l, false);
            memmove(ram_ptr, buf, l);
            invalidate_and_set_dirty(mr, addr1, l);
        }

        if (release_lock) {
            qemu_mutex_unlock_iothread();
            release_lock = false;
        }

        len -= l;
        buf += l;
        addr += l;

        if (!len) {
            break;
        }

        l = len;
        mr = flatview_translate(fv, addr, &addr1, &l, true, attrs);
    }

    return result;
}

/* Called from RCU critical section.  */
static MemTxResult flatview_write(FlatView *fv, hwaddr addr, MemTxAttrs attrs,
                                  const void *buf, hwaddr len)
{
    hwaddr l;
    hwaddr addr1;
    MemoryRegion *mr;

    l = len;
    mr = flatview_translate(fv, addr, &addr1, &l, true, attrs);
    if (!flatview_access_allowed(mr, attrs, addr, len)) {
        return MEMTX_ACCESS_ERROR;
    }
    return flatview_write_continue(fv, addr, attrs, buf, len,
                                   addr1, l, mr);
}

/* Called within RCU critical section.  */
MemTxResult flatview_read_continue(FlatView *fv, hwaddr addr,
                                   MemTxAttrs attrs, void *ptr,
                                   hwaddr len, hwaddr addr1, hwaddr l,
                                   MemoryRegion *mr)
{
    uint8_t *ram_ptr;
    uint64_t val;
    MemTxResult result = MEMTX_OK;
    bool release_lock = false;
    uint8_t *buf = ptr;

    fuzz_dma_read_cb(addr, len, mr);
    for (;;) {
        if (!flatview_access_allowed(mr, attrs, addr1, l)) {
            result |= MEMTX_ACCESS_ERROR;
            /* Keep going. */
        } else if (!memory_access_is_direct(mr, false)) {
            /* I/O case */
            release_lock |= prepare_mmio_access(mr);
            l = memory_access_size(mr, l, addr1);
            result |= memory_region_dispatch_read(mr, addr1, &val,
                                                  size_memop(l), attrs);
            stn_he_p(buf, l, val);
        } else {
            /* RAM case */
            ram_ptr = qemu_ram_ptr_length(mr->ram_block, addr1, &l, false);
            memcpy(buf, ram_ptr, l);
        }

        if (release_lock) {
            qemu_mutex_unlock_iothread();
            release_lock = false;
        }

        len -= l;
        buf += l;
        addr += l;

        if (!len) {
            break;
        }

        l = len;
        mr = flatview_translate(fv, addr, &addr1, &l, false, attrs);
    }

    return result;
}

/* Called from RCU critical section.  */
static MemTxResult flatview_read(FlatView *fv, hwaddr addr,
                                 MemTxAttrs attrs, void *buf, hwaddr len)
{
    hwaddr l;
    hwaddr addr1;
    MemoryRegion *mr;

    l = len;
    mr = flatview_translate(fv, addr, &addr1, &l, false, attrs);
    if (!flatview_access_allowed(mr, attrs, addr, len)) {
        return MEMTX_ACCESS_ERROR;
    }
    return flatview_read_continue(fv, addr, attrs, buf, len,
                                  addr1, l, mr);
}

MemTxResult address_space_read_full(AddressSpace *as, hwaddr addr,
                                    MemTxAttrs attrs, void *buf, hwaddr len)
{
    MemTxResult result = MEMTX_OK;
    FlatView *fv;

    if (len > 0) {
        RCU_READ_LOCK_GUARD();
        fv = address_space_to_flatview(as);
        result = flatview_read(fv, addr, attrs, buf, len);
    }

    return result;
}

MemTxResult address_space_write(AddressSpace *as, hwaddr addr,
                                MemTxAttrs attrs,
                                const void *buf, hwaddr len)
{
    MemTxResult result = MEMTX_OK;
    FlatView *fv;

    if (len > 0) {
        RCU_READ_LOCK_GUARD();
        fv = address_space_to_flatview(as);
        result = flatview_write(fv, addr, attrs, buf, len);
    }

    return result;
}

MemTxResult address_space_rw(AddressSpace *as, hwaddr addr, MemTxAttrs attrs,
                             void *buf, hwaddr len, bool is_write)
{
    if (is_write) {
        return address_space_write(as, addr, attrs, buf, len);
    } else {
        return address_space_read_full(as, addr, attrs, buf, len);
    }
}

MemTxResult address_space_set(AddressSpace *as, hwaddr addr,
                              uint8_t c, hwaddr len, MemTxAttrs attrs)
{
#define FILLBUF_SIZE 512
    uint8_t fillbuf[FILLBUF_SIZE];
    int l;
    MemTxResult error = MEMTX_OK;

    memset(fillbuf, c, FILLBUF_SIZE);
    while (len > 0) {
        l = len < FILLBUF_SIZE ? len : FILLBUF_SIZE;
        error |= address_space_write(as, addr, attrs, fillbuf, l);
        len -= l;
        addr += l;
    }

    return error;
}

void cpu_physical_memory_rw(hwaddr addr, void *buf,
                            hwaddr len, bool is_write)
{
    address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                     buf, len, is_write);
}

enum write_rom_type {
    WRITE_DATA,
    FLUSH_CACHE,
};

static inline MemTxResult address_space_write_rom_internal(AddressSpace *as,
                                                           hwaddr addr,
                                                           MemTxAttrs attrs,
                                                           const void *ptr,
                                                           hwaddr len,
                                                           enum write_rom_type type)
{
    hwaddr l;
    uint8_t *ram_ptr;
    hwaddr addr1;
    MemoryRegion *mr;
    const uint8_t *buf = ptr;

    RCU_READ_LOCK_GUARD();
    while (len > 0) {
        l = len;
        mr = address_space_translate(as, addr, &addr1, &l, true, attrs);

        if (!(memory_region_is_ram(mr) ||
              memory_region_is_romd(mr))) {
            l = memory_access_size(mr, l, addr1);
        } else {
            /* ROM/RAM case */
            ram_ptr = qemu_map_ram_ptr(mr->ram_block, addr1);
            switch (type) {
            case WRITE_DATA:
                memcpy(ram_ptr, buf, l);
                invalidate_and_set_dirty(mr, addr1, l);
                break;
            case FLUSH_CACHE:
                flush_idcache_range((uintptr_t)ram_ptr, (uintptr_t)ram_ptr, l);
                break;
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return MEMTX_OK;
}

/* used for ROM loading : can write in RAM and ROM */
MemTxResult address_space_write_rom(AddressSpace *as, hwaddr addr,
                                    MemTxAttrs attrs,
                                    const void *buf, hwaddr len)
{
    return address_space_write_rom_internal(as, addr, attrs,
                                            buf, len, WRITE_DATA);
}

void cpu_flush_icache_range(hwaddr start, hwaddr len)
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

    address_space_write_rom_internal(&address_space_memory,
                                     start, MEMTXATTRS_UNSPECIFIED,
                                     NULL, len, FLUSH_CACHE);
}

typedef struct {
    MemoryRegion *mr;
    void *buffer;
    hwaddr addr;
    hwaddr len;
    bool in_use;
} BounceBuffer;

static BounceBuffer bounce;

typedef struct MapClient {
    QEMUBH *bh;
    QLIST_ENTRY(MapClient) link;
} MapClient;

QemuMutex map_client_list_lock;
static QLIST_HEAD(, MapClient) map_client_list
    = QLIST_HEAD_INITIALIZER(map_client_list);

static void cpu_unregister_map_client_do(MapClient *client)
{
    QLIST_REMOVE(client, link);
    g_free(client);
}

static void cpu_notify_map_clients_locked(void)
{
    MapClient *client;

    while (!QLIST_EMPTY(&map_client_list)) {
        client = QLIST_FIRST(&map_client_list);
        qemu_bh_schedule(client->bh);
        cpu_unregister_map_client_do(client);
    }
}

void cpu_register_map_client(QEMUBH *bh)
{
    MapClient *client = g_malloc(sizeof(*client));

    qemu_mutex_lock(&map_client_list_lock);
    client->bh = bh;
    QLIST_INSERT_HEAD(&map_client_list, client, link);
    /* Write map_client_list before reading in_use.  */
    smp_mb();
    if (!qatomic_read(&bounce.in_use)) {
        cpu_notify_map_clients_locked();
    }
    qemu_mutex_unlock(&map_client_list_lock);
}

void cpu_exec_init_all(void)
{
    qemu_mutex_init(&ram_list.mutex);
    /* The data structures we set up here depend on knowing the page size,
     * so no more changes can be made after this point.
     * In an ideal world, nothing we did before we had finished the
     * machine setup would care about the target page size, and we could
     * do this much later, rather than requiring board models to state
     * up front what their requirements are.
     */
    finalize_target_page_bits();
    io_mem_init();
    memory_map_init();
    qemu_mutex_init(&map_client_list_lock);
}

void cpu_unregister_map_client(QEMUBH *bh)
{
    MapClient *client;

    qemu_mutex_lock(&map_client_list_lock);
    QLIST_FOREACH(client, &map_client_list, link) {
        if (client->bh == bh) {
            cpu_unregister_map_client_do(client);
            break;
        }
    }
    qemu_mutex_unlock(&map_client_list_lock);
}

static void cpu_notify_map_clients(void)
{
    qemu_mutex_lock(&map_client_list_lock);
    cpu_notify_map_clients_locked();
    qemu_mutex_unlock(&map_client_list_lock);
}

static bool flatview_access_valid(FlatView *fv, hwaddr addr, hwaddr len,
                                  bool is_write, MemTxAttrs attrs)
{
    MemoryRegion *mr;
    hwaddr l, xlat;

    while (len > 0) {
        l = len;
        mr = flatview_translate(fv, addr, &xlat, &l, is_write, attrs);
        if (!memory_access_is_direct(mr, is_write)) {
            l = memory_access_size(mr, l, addr);
            if (!memory_region_access_valid(mr, xlat, l, is_write, attrs)) {
                return false;
            }
        }

        len -= l;
        addr += l;
    }
    return true;
}

bool address_space_access_valid(AddressSpace *as, hwaddr addr,
                                hwaddr len, bool is_write,
                                MemTxAttrs attrs)
{
    FlatView *fv;

    RCU_READ_LOCK_GUARD();
    fv = address_space_to_flatview(as);
    return flatview_access_valid(fv, addr, len, is_write, attrs);
}

static hwaddr
flatview_extend_translation(FlatView *fv, hwaddr addr,
                            hwaddr target_len,
                            MemoryRegion *mr, hwaddr base, hwaddr len,
                            bool is_write, MemTxAttrs attrs)
{
    hwaddr done = 0;
    hwaddr xlat;
    MemoryRegion *this_mr;

    for (;;) {
        target_len -= len;
        addr += len;
        done += len;
        if (target_len == 0) {
            return done;
        }

        len = target_len;
        this_mr = flatview_translate(fv, addr, &xlat,
                                     &len, is_write, attrs);
        if (this_mr != mr || xlat != base + done) {
            return done;
        }
    }
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
                        bool is_write,
                        MemTxAttrs attrs)
{
    hwaddr len = *plen;
    hwaddr l, xlat;
    MemoryRegion *mr;
    FlatView *fv;

    if (len == 0) {
        return NULL;
    }

    l = len;
    RCU_READ_LOCK_GUARD();
    fv = address_space_to_flatview(as);
    mr = flatview_translate(fv, addr, &xlat, &l, is_write, attrs);

    if (!memory_access_is_direct(mr, is_write)) {
        if (qatomic_xchg(&bounce.in_use, true)) {
            *plen = 0;
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
            flatview_read(fv, addr, MEMTXATTRS_UNSPECIFIED,
                               bounce.buffer, l);
        }

        *plen = l;
        return bounce.buffer;
    }


    memory_region_ref(mr);
    *plen = flatview_extend_translation(fv, addr, len, mr, xlat,
                                        l, is_write, attrs);
    fuzz_dma_read_cb(addr, *plen, mr);
    return qemu_ram_ptr_length(mr->ram_block, xlat, plen, true);
}

/* Unmaps a memory region previously mapped by address_space_map().
 * Will also mark the memory as dirty if is_write is true.  access_len gives
 * the amount of memory that was actually read or written by the caller.
 */
void address_space_unmap(AddressSpace *as, void *buffer, hwaddr len,
                         bool is_write, hwaddr access_len)
{
    if (buffer != bounce.buffer) {
        MemoryRegion *mr;
        ram_addr_t addr1;

        mr = memory_region_from_host(buffer, &addr1);
        assert(mr != NULL);
        if (is_write) {
            invalidate_and_set_dirty(mr, addr1, access_len);
        }
        if (xen_enabled()) {
            xen_invalidate_map_cache_entry(buffer);
        }
        memory_region_unref(mr);
        return;
    }
    if (is_write) {
        address_space_write(as, bounce.addr, MEMTXATTRS_UNSPECIFIED,
                            bounce.buffer, access_len);
    }
    qemu_vfree(bounce.buffer);
    bounce.buffer = NULL;
    memory_region_unref(bounce.mr);
    /* Clear in_use before reading map_client_list.  */
    qatomic_set_mb(&bounce.in_use, false);
    cpu_notify_map_clients();
}

void *cpu_physical_memory_map(hwaddr addr,
                              hwaddr *plen,
                              bool is_write)
{
    return address_space_map(&address_space_memory, addr, plen, is_write,
                             MEMTXATTRS_UNSPECIFIED);
}

void cpu_physical_memory_unmap(void *buffer, hwaddr len,
                               bool is_write, hwaddr access_len)
{
    return address_space_unmap(&address_space_memory, buffer, len, is_write, access_len);
}

#define ARG1_DECL                AddressSpace *as
#define ARG1                     as
#define SUFFIX
#define TRANSLATE(...)           address_space_translate(as, __VA_ARGS__)
#define RCU_READ_LOCK(...)       rcu_read_lock()
#define RCU_READ_UNLOCK(...)     rcu_read_unlock()
#include "memory_ldst.c.inc"

int64_t address_space_cache_init(MemoryRegionCache *cache,
                                 AddressSpace *as,
                                 hwaddr addr,
                                 hwaddr len,
                                 bool is_write)
{
    AddressSpaceDispatch *d;
    hwaddr l;
    MemoryRegion *mr;
    Int128 diff;

    assert(len > 0);

    l = len;
    cache->fv = address_space_get_flatview(as);
    d = flatview_to_dispatch(cache->fv);
    cache->mrs = *address_space_translate_internal(d, addr, &cache->xlat, &l, true);

    /*
     * cache->xlat is now relative to cache->mrs.mr, not to the section itself.
     * Take that into account to compute how many bytes are there between
     * cache->xlat and the end of the section.
     */
    diff = int128_sub(cache->mrs.size,
                      int128_make64(cache->xlat - cache->mrs.offset_within_region));
    l = int128_get64(int128_min(diff, int128_make64(l)));

    mr = cache->mrs.mr;
    memory_region_ref(mr);
    if (memory_access_is_direct(mr, is_write)) {
        /* We don't care about the memory attributes here as we're only
         * doing this if we found actual RAM, which behaves the same
         * regardless of attributes; so UNSPECIFIED is fine.
         */
        l = flatview_extend_translation(cache->fv, addr, len, mr,
                                        cache->xlat, l, is_write,
                                        MEMTXATTRS_UNSPECIFIED);
        cache->ptr = qemu_ram_ptr_length(mr->ram_block, cache->xlat, &l, true);
    } else {
        cache->ptr = NULL;
    }

    cache->len = l;
    cache->is_write = is_write;
    return l;
}

void address_space_cache_invalidate(MemoryRegionCache *cache,
                                    hwaddr addr,
                                    hwaddr access_len)
{
    assert(cache->is_write);
    if (likely(cache->ptr)) {
        invalidate_and_set_dirty(cache->mrs.mr, addr + cache->xlat, access_len);
    }
}

void address_space_cache_destroy(MemoryRegionCache *cache)
{
    if (!cache->mrs.mr) {
        return;
    }

    if (xen_enabled()) {
        xen_invalidate_map_cache_entry(cache->ptr);
    }
    memory_region_unref(cache->mrs.mr);
    flatview_unref(cache->fv);
    cache->mrs.mr = NULL;
    cache->fv = NULL;
}

/* Called from RCU critical section.  This function has the same
 * semantics as address_space_translate, but it only works on a
 * predefined range of a MemoryRegion that was mapped with
 * address_space_cache_init.
 */
static inline MemoryRegion *address_space_translate_cached(
    MemoryRegionCache *cache, hwaddr addr, hwaddr *xlat,
    hwaddr *plen, bool is_write, MemTxAttrs attrs)
{
    MemoryRegionSection section;
    MemoryRegion *mr;
    IOMMUMemoryRegion *iommu_mr;
    AddressSpace *target_as;

    assert(!cache->ptr);
    *xlat = addr + cache->xlat;

    mr = cache->mrs.mr;
    iommu_mr = memory_region_get_iommu(mr);
    if (!iommu_mr) {
        /* MMIO region.  */
        return mr;
    }

    section = address_space_translate_iommu(iommu_mr, xlat, plen,
                                            NULL, is_write, true,
                                            &target_as, attrs);
    return section.mr;
}

/* Called from RCU critical section. address_space_read_cached uses this
 * out of line function when the target is an MMIO or IOMMU region.
 */
MemTxResult
address_space_read_cached_slow(MemoryRegionCache *cache, hwaddr addr,
                                   void *buf, hwaddr len)
{
    hwaddr addr1, l;
    MemoryRegion *mr;

    l = len;
    mr = address_space_translate_cached(cache, addr, &addr1, &l, false,
                                        MEMTXATTRS_UNSPECIFIED);
    return flatview_read_continue(cache->fv,
                                  addr, MEMTXATTRS_UNSPECIFIED, buf, len,
                                  addr1, l, mr);
}

/* Called from RCU critical section. address_space_write_cached uses this
 * out of line function when the target is an MMIO or IOMMU region.
 */
MemTxResult
address_space_write_cached_slow(MemoryRegionCache *cache, hwaddr addr,
                                    const void *buf, hwaddr len)
{
    hwaddr addr1, l;
    MemoryRegion *mr;

    l = len;
    mr = address_space_translate_cached(cache, addr, &addr1, &l, true,
                                        MEMTXATTRS_UNSPECIFIED);
    return flatview_write_continue(cache->fv,
                                   addr, MEMTXATTRS_UNSPECIFIED, buf, len,
                                   addr1, l, mr);
}

#define ARG1_DECL                MemoryRegionCache *cache
#define ARG1                     cache
#define SUFFIX                   _cached_slow
#define TRANSLATE(...)           address_space_translate_cached(cache, __VA_ARGS__)
#define RCU_READ_LOCK()          ((void)0)
#define RCU_READ_UNLOCK()        ((void)0)
#include "memory_ldst.c.inc"

/* virtual memory access for debug (includes writing to ROM) */
int cpu_memory_rw_debug(CPUState *cpu, vaddr addr,
                        void *ptr, size_t len, bool is_write)
{
    hwaddr phys_addr;
    vaddr l, page;
    uint8_t *buf = ptr;

    cpu_synchronize_state(cpu);
    while (len > 0) {
        int asidx;
        MemTxAttrs attrs;
        MemTxResult res;

        page = addr & TARGET_PAGE_MASK;
        phys_addr = cpu_get_phys_page_attrs_debug(cpu, page, &attrs);
        asidx = cpu_asidx_from_attrs(cpu, attrs);
        /* if no physical page mapped, return an error */
        if (phys_addr == -1)
            return -1;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        phys_addr += (addr & ~TARGET_PAGE_MASK);
        if (is_write) {
            res = address_space_write_rom(cpu->cpu_ases[asidx].as, phys_addr,
                                          attrs, buf, l);
        } else {
            res = address_space_read(cpu->cpu_ases[asidx].as, phys_addr,
                                     attrs, buf, l);
        }
        if (res != MEMTX_OK) {
            return -1;
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}

/*
 * Allows code that needs to deal with migration bitmaps etc to still be built
 * target independent.
 */
size_t qemu_target_page_size(void)
{
    return TARGET_PAGE_SIZE;
}

int qemu_target_page_bits(void)
{
    return TARGET_PAGE_BITS;
}

int qemu_target_page_bits_min(void)
{
    return TARGET_PAGE_BITS_MIN;
}

/* Convert target pages to MiB (2**20). */
size_t qemu_target_pages_to_MiB(size_t pages)
{
    int page_bits = TARGET_PAGE_BITS;

    /* So far, the largest (non-huge) page size is 64k, i.e. 16 bits. */
    g_assert(page_bits < 20);

    return pages >> (20 - page_bits);
}

bool cpu_physical_memory_is_io(hwaddr phys_addr)
{
    MemoryRegion*mr;
    hwaddr l = 1;

    RCU_READ_LOCK_GUARD();
    mr = address_space_translate(&address_space_memory,
                                 phys_addr, &phys_addr, &l, false,
                                 MEMTXATTRS_UNSPECIFIED);

    return !(memory_region_is_ram(mr) || memory_region_is_romd(mr));
}

int qemu_ram_foreach_block(RAMBlockIterFunc func, void *opaque)
{
    RAMBlock *block;
    int ret = 0;

    RCU_READ_LOCK_GUARD();
    RAMBLOCK_FOREACH(block) {
        ret = func(block, opaque);
        if (ret) {
            break;
        }
    }
    return ret;
}

/*
 * Unmap pages of memory from start to start+length such that
 * they a) read as 0, b) Trigger whatever fault mechanism
 * the OS provides for postcopy.
 * The pages must be unmapped by the end of the function.
 * Returns: 0 on success, none-0 on failure
 *
 */
int ram_block_discard_range(RAMBlock *rb, uint64_t start, size_t length)
{
    int ret = -1;

    uint8_t *host_startaddr = rb->host + start;

    if (!QEMU_PTR_IS_ALIGNED(host_startaddr, rb->page_size)) {
        error_report("ram_block_discard_range: Unaligned start address: %p",
                     host_startaddr);
        goto err;
    }

    if ((start + length) <= rb->max_length) {
        bool need_madvise, need_fallocate;
        if (!QEMU_IS_ALIGNED(length, rb->page_size)) {
            error_report("ram_block_discard_range: Unaligned length: %zx",
                         length);
            goto err;
        }

        errno = ENOTSUP; /* If we are missing MADVISE etc */

        /* The logic here is messy;
         *    madvise DONTNEED fails for hugepages
         *    fallocate works on hugepages and shmem
         *    shared anonymous memory requires madvise REMOVE
         */
        need_madvise = (rb->page_size == qemu_host_page_size);
        need_fallocate = rb->fd != -1;
        if (need_fallocate) {
            /* For a file, this causes the area of the file to be zero'd
             * if read, and for hugetlbfs also causes it to be unmapped
             * so a userfault will trigger.
             */
#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
            ret = fallocate(rb->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                            start, length);
            if (ret) {
                ret = -errno;
                error_report("ram_block_discard_range: Failed to fallocate "
                             "%s:%" PRIx64 " +%zx (%d)",
                             rb->idstr, start, length, ret);
                goto err;
            }
#else
            ret = -ENOSYS;
            error_report("ram_block_discard_range: fallocate not available/file"
                         "%s:%" PRIx64 " +%zx (%d)",
                         rb->idstr, start, length, ret);
            goto err;
#endif
        }
        if (need_madvise) {
            /* For normal RAM this causes it to be unmapped,
             * for shared memory it causes the local mapping to disappear
             * and to fall back on the file contents (which we just
             * fallocate'd away).
             */
#if defined(CONFIG_MADVISE)
            if (qemu_ram_is_shared(rb) && rb->fd < 0) {
                ret = madvise(host_startaddr, length, QEMU_MADV_REMOVE);
            } else {
                ret = madvise(host_startaddr, length, QEMU_MADV_DONTNEED);
            }
            if (ret) {
                ret = -errno;
                error_report("ram_block_discard_range: Failed to discard range "
                             "%s:%" PRIx64 " +%zx (%d)",
                             rb->idstr, start, length, ret);
                goto err;
            }
#else
            ret = -ENOSYS;
            error_report("ram_block_discard_range: MADVISE not available"
                         "%s:%" PRIx64 " +%zx (%d)",
                         rb->idstr, start, length, ret);
            goto err;
#endif
        }
        trace_ram_block_discard_range(rb->idstr, host_startaddr, length,
                                      need_madvise, need_fallocate, ret);
    } else {
        error_report("ram_block_discard_range: Overrun block '%s' (%" PRIu64
                     "/%zx/" RAM_ADDR_FMT")",
                     rb->idstr, start, length, rb->max_length);
    }

err:
    return ret;
}

bool ramblock_is_pmem(RAMBlock *rb)
{
    return rb->flags & RAM_PMEM;
}

static void mtree_print_phys_entries(int start, int end, int skip, int ptr)
{
    if (start == end - 1) {
        qemu_printf("\t%3d      ", start);
    } else {
        qemu_printf("\t%3d..%-3d ", start, end - 1);
    }
    qemu_printf(" skip=%d ", skip);
    if (ptr == PHYS_MAP_NODE_NIL) {
        qemu_printf(" ptr=NIL");
    } else if (!skip) {
        qemu_printf(" ptr=#%d", ptr);
    } else {
        qemu_printf(" ptr=[%d]", ptr);
    }
    qemu_printf("\n");
}

#define MR_SIZE(size) (int128_nz(size) ? (hwaddr)int128_get64( \
                           int128_sub((size), int128_one())) : 0)

void mtree_print_dispatch(AddressSpaceDispatch *d, MemoryRegion *root)
{
    int i;

    qemu_printf("  Dispatch\n");
    qemu_printf("    Physical sections\n");

    for (i = 0; i < d->map.sections_nb; ++i) {
        MemoryRegionSection *s = d->map.sections + i;
        const char *names[] = { " [unassigned]", " [not dirty]",
                                " [ROM]", " [watch]" };

        qemu_printf("      #%d @" HWADDR_FMT_plx ".." HWADDR_FMT_plx
                    " %s%s%s%s%s",
            i,
            s->offset_within_address_space,
            s->offset_within_address_space + MR_SIZE(s->size),
            s->mr->name ? s->mr->name : "(noname)",
            i < ARRAY_SIZE(names) ? names[i] : "",
            s->mr == root ? " [ROOT]" : "",
            s == d->mru_section ? " [MRU]" : "",
            s->mr->is_iommu ? " [iommu]" : "");

        if (s->mr->alias) {
            qemu_printf(" alias=%s", s->mr->alias->name ?
                    s->mr->alias->name : "noname");
        }
        qemu_printf("\n");
    }

    qemu_printf("    Nodes (%d bits per level, %d levels) ptr=[%d] skip=%d\n",
               P_L2_BITS, P_L2_LEVELS, d->phys_map.ptr, d->phys_map.skip);
    for (i = 0; i < d->map.nodes_nb; ++i) {
        int j, jprev;
        PhysPageEntry prev;
        Node *n = d->map.nodes + i;

        qemu_printf("      [%d]\n", i);

        for (j = 0, jprev = 0, prev = *n[0]; j < ARRAY_SIZE(*n); ++j) {
            PhysPageEntry *pe = *n + j;

            if (pe->ptr == prev.ptr && pe->skip == prev.skip) {
                continue;
            }

            mtree_print_phys_entries(jprev, j, prev.skip, prev.ptr);

            jprev = j;
            prev = *pe;
        }

        if (jprev != ARRAY_SIZE(*n)) {
            mtree_print_phys_entries(jprev, j, prev.skip, prev.ptr);
        }
    }
}

/* Require any discards to work. */
static unsigned int ram_block_discard_required_cnt;
/* Require only coordinated discards to work. */
static unsigned int ram_block_coordinated_discard_required_cnt;
/* Disable any discards. */
static unsigned int ram_block_discard_disabled_cnt;
/* Disable only uncoordinated discards. */
static unsigned int ram_block_uncoordinated_discard_disabled_cnt;
static QemuMutex ram_block_discard_disable_mutex;

static void ram_block_discard_disable_mutex_lock(void)
{
    static gsize initialized;

    if (g_once_init_enter(&initialized)) {
        qemu_mutex_init(&ram_block_discard_disable_mutex);
        g_once_init_leave(&initialized, 1);
    }
    qemu_mutex_lock(&ram_block_discard_disable_mutex);
}

static void ram_block_discard_disable_mutex_unlock(void)
{
    qemu_mutex_unlock(&ram_block_discard_disable_mutex);
}

int ram_block_discard_disable(bool state)
{
    int ret = 0;

    ram_block_discard_disable_mutex_lock();
    if (!state) {
        ram_block_discard_disabled_cnt--;
    } else if (ram_block_discard_required_cnt ||
               ram_block_coordinated_discard_required_cnt) {
        ret = -EBUSY;
    } else {
        ram_block_discard_disabled_cnt++;
    }
    ram_block_discard_disable_mutex_unlock();
    return ret;
}

int ram_block_uncoordinated_discard_disable(bool state)
{
    int ret = 0;

    ram_block_discard_disable_mutex_lock();
    if (!state) {
        ram_block_uncoordinated_discard_disabled_cnt--;
    } else if (ram_block_discard_required_cnt) {
        ret = -EBUSY;
    } else {
        ram_block_uncoordinated_discard_disabled_cnt++;
    }
    ram_block_discard_disable_mutex_unlock();
    return ret;
}

int ram_block_discard_require(bool state)
{
    int ret = 0;

    ram_block_discard_disable_mutex_lock();
    if (!state) {
        ram_block_discard_required_cnt--;
    } else if (ram_block_discard_disabled_cnt ||
               ram_block_uncoordinated_discard_disabled_cnt) {
        ret = -EBUSY;
    } else {
        ram_block_discard_required_cnt++;
    }
    ram_block_discard_disable_mutex_unlock();
    return ret;
}

int ram_block_coordinated_discard_require(bool state)
{
    int ret = 0;

    ram_block_discard_disable_mutex_lock();
    if (!state) {
        ram_block_coordinated_discard_required_cnt--;
    } else if (ram_block_discard_disabled_cnt) {
        ret = -EBUSY;
    } else {
        ram_block_coordinated_discard_required_cnt++;
    }
    ram_block_discard_disable_mutex_unlock();
    return ret;
}

bool ram_block_discard_is_disabled(void)
{
    return qatomic_read(&ram_block_discard_disabled_cnt) ||
           qatomic_read(&ram_block_uncoordinated_discard_disabled_cnt);
}

bool ram_block_discard_is_required(void)
{
    return qatomic_read(&ram_block_discard_required_cnt) ||
           qatomic_read(&ram_block_coordinated_discard_required_cnt);
}
