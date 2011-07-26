/*
 * Physical memory management
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "memory.h"
#include <assert.h>

typedef struct AddrRange AddrRange;

struct AddrRange {
    uint64_t start;
    uint64_t size;
};

static AddrRange addrrange_make(uint64_t start, uint64_t size)
{
    return (AddrRange) { start, size };
}

static bool addrrange_equal(AddrRange r1, AddrRange r2)
{
    return r1.start == r2.start && r1.size == r2.size;
}

static uint64_t addrrange_end(AddrRange r)
{
    return r.start + r.size;
}

static AddrRange addrrange_shift(AddrRange range, int64_t delta)
{
    range.start += delta;
    return range;
}

static bool addrrange_intersects(AddrRange r1, AddrRange r2)
{
    return (r1.start >= r2.start && r1.start < r2.start + r2.size)
        || (r2.start >= r1.start && r2.start < r1.start + r1.size);
}

static AddrRange addrrange_intersection(AddrRange r1, AddrRange r2)
{
    uint64_t start = MAX(r1.start, r2.start);
    /* off-by-one arithmetic to prevent overflow */
    uint64_t end = MIN(addrrange_end(r1) - 1, addrrange_end(r2) - 1);
    return addrrange_make(start, end - start + 1);
}

struct CoalescedMemoryRange {
    AddrRange addr;
    QTAILQ_ENTRY(CoalescedMemoryRange) link;
};

typedef struct FlatRange FlatRange;
typedef struct FlatView FlatView;

/* Range of memory in the global map.  Addresses are absolute. */
struct FlatRange {
    MemoryRegion *mr;
    target_phys_addr_t offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
};

/* Flattened global view of current active memory hierarchy.  Kept in sorted
 * order.
 */
struct FlatView {
    FlatRange *ranges;
    unsigned nr;
    unsigned nr_allocated;
};

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

static FlatView current_memory_map;
static MemoryRegion *root_memory_region;

static bool flatrange_equal(FlatRange *a, FlatRange *b)
{
    return a->mr == b->mr
        && addrrange_equal(a->addr, b->addr)
        && a->offset_in_region == b->offset_in_region;
}

static void flatview_init(FlatView *view)
{
    view->ranges = NULL;
    view->nr = 0;
    view->nr_allocated = 0;
}

/* Insert a range into a given position.  Caller is responsible for maintaining
 * sorting order.
 */
static void flatview_insert(FlatView *view, unsigned pos, FlatRange *range)
{
    if (view->nr == view->nr_allocated) {
        view->nr_allocated = MAX(2 * view->nr, 10);
        view->ranges = qemu_realloc(view->ranges,
                                    view->nr_allocated * sizeof(*view->ranges));
    }
    memmove(view->ranges + pos + 1, view->ranges + pos,
            (view->nr - pos) * sizeof(FlatRange));
    view->ranges[pos] = *range;
    ++view->nr;
}

static void flatview_destroy(FlatView *view)
{
    qemu_free(view->ranges);
}

static bool can_merge(FlatRange *r1, FlatRange *r2)
{
    return addrrange_end(r1->addr) == r2->addr.start
        && r1->mr == r2->mr
        && r1->offset_in_region + r1->addr.size == r2->offset_in_region
        && r1->dirty_log_mask == r2->dirty_log_mask;
}

/* Attempt to simplify a view by merging ajacent ranges */
static void flatview_simplify(FlatView *view)
{
    unsigned i, j;

    i = 0;
    while (i < view->nr) {
        j = i + 1;
        while (j < view->nr
               && can_merge(&view->ranges[j-1], &view->ranges[j])) {
            view->ranges[i].addr.size += view->ranges[j].addr.size;
            ++j;
        }
        ++i;
        memmove(&view->ranges[i], &view->ranges[j],
                (view->nr - j) * sizeof(view->ranges[j]));
        view->nr -= j - i;
    }
}

/* Render a memory region into the global view.  Ranges in @view obscure
 * ranges in @mr.
 */
static void render_memory_region(FlatView *view,
                                 MemoryRegion *mr,
                                 target_phys_addr_t base,
                                 AddrRange clip)
{
    MemoryRegion *subregion;
    unsigned i;
    target_phys_addr_t offset_in_region;
    uint64_t remain;
    uint64_t now;
    FlatRange fr;
    AddrRange tmp;

    base += mr->addr;

    tmp = addrrange_make(base, mr->size);

    if (!addrrange_intersects(tmp, clip)) {
        return;
    }

    clip = addrrange_intersection(tmp, clip);

    if (mr->alias) {
        base -= mr->alias->addr;
        base -= mr->alias_offset;
        render_memory_region(view, mr->alias, base, clip);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip);
    }

    if (!mr->has_ram_addr) {
        return;
    }

    offset_in_region = clip.start - base;
    base = clip.start;
    remain = clip.size;

    /* Render the region itself into any gaps left by the current view. */
    for (i = 0; i < view->nr && remain; ++i) {
        if (base >= addrrange_end(view->ranges[i].addr)) {
            continue;
        }
        if (base < view->ranges[i].addr.start) {
            now = MIN(remain, view->ranges[i].addr.start - base);
            fr.mr = mr;
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            fr.dirty_log_mask = mr->dirty_log_mask;
            flatview_insert(view, i, &fr);
            ++i;
            base += now;
            offset_in_region += now;
            remain -= now;
        }
        if (base == view->ranges[i].addr.start) {
            now = MIN(remain, view->ranges[i].addr.size);
            base += now;
            offset_in_region += now;
            remain -= now;
        }
    }
    if (remain) {
        fr.mr = mr;
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        fr.dirty_log_mask = mr->dirty_log_mask;
        flatview_insert(view, i, &fr);
    }
}

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView generate_memory_topology(MemoryRegion *mr)
{
    FlatView view;

    flatview_init(&view);

    render_memory_region(&view, mr, 0, addrrange_make(0, UINT64_MAX));
    flatview_simplify(&view);

    return view;
}

static void memory_region_update_topology(void)
{
    FlatView old_view = current_memory_map;
    FlatView new_view = generate_memory_topology(root_memory_region);
    unsigned iold, inew;
    FlatRange *frold, *frnew;
    ram_addr_t phys_offset, region_offset;

    /* Generate a symmetric difference of the old and new memory maps.
     * Kill ranges in the old map, and instantiate ranges in the new map.
     */
    iold = inew = 0;
    while (iold < old_view.nr || inew < new_view.nr) {
        if (iold < old_view.nr) {
            frold = &old_view.ranges[iold];
        } else {
            frold = NULL;
        }
        if (inew < new_view.nr) {
            frnew = &new_view.ranges[inew];
        } else {
            frnew = NULL;
        }

        if (frold
            && (!frnew
                || frold->addr.start < frnew->addr.start
                || (frold->addr.start == frnew->addr.start
                    && !flatrange_equal(frold, frnew)))) {
            /* In old, but (not in new, or in new but attributes changed). */

            cpu_register_physical_memory(frold->addr.start, frold->addr.size,
                                         IO_MEM_UNASSIGNED);
            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both (logging may have changed) */

            if (frold->dirty_log_mask && !frnew->dirty_log_mask) {
                cpu_physical_log_stop(frnew->addr.start, frnew->addr.size);
            } else if (frnew->dirty_log_mask && !frold->dirty_log_mask) {
                cpu_physical_log_start(frnew->addr.start, frnew->addr.size);
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            phys_offset = frnew->mr->ram_addr;
            region_offset = frnew->offset_in_region;
            /* cpu_register_physical_memory_log() wants region_offset for
             * mmio, but prefers offseting phys_offset for RAM.  Humour it.
             */
            if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM) {
                phys_offset += region_offset;
                region_offset = 0;
            }

            cpu_register_physical_memory_log(frnew->addr.start,
                                             frnew->addr.size,
                                             phys_offset,
                                             region_offset,
                                             frnew->dirty_log_mask);
            ++inew;
        }
    }
    current_memory_map = new_view;
    flatview_destroy(&old_view);
}

void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size)
{
    mr->ops = NULL;
    mr->parent = NULL;
    mr->size = size;
    mr->addr = 0;
    mr->offset = 0;
    mr->has_ram_addr = false;
    mr->priority = 0;
    mr->may_overlap = false;
    mr->alias = NULL;
    QTAILQ_INIT(&mr->subregions);
    memset(&mr->subregions_link, 0, sizeof mr->subregions_link);
    QTAILQ_INIT(&mr->coalesced);
    mr->name = qemu_strdup(name);
    mr->dirty_log_mask = 0;
}

static bool memory_region_access_valid(MemoryRegion *mr,
                                       target_phys_addr_t addr,
                                       unsigned size)
{
    if (!mr->ops->valid.unaligned && (addr & (size - 1))) {
        return false;
    }

    /* Treat zero as compatibility all valid */
    if (!mr->ops->valid.max_access_size) {
        return true;
    }

    if (size > mr->ops->valid.max_access_size
        || size < mr->ops->valid.min_access_size) {
        return false;
    }
    return true;
}

static uint32_t memory_region_read_thunk_n(void *_mr,
                                           target_phys_addr_t addr,
                                           unsigned size)
{
    MemoryRegion *mr = _mr;
    unsigned access_size, access_size_min, access_size_max;
    uint64_t access_mask;
    uint32_t data = 0, tmp;
    unsigned i;

    if (!memory_region_access_valid(mr, addr, size)) {
        return -1U; /* FIXME: better signalling */
    }

    /* FIXME: support unaligned access */

    access_size_min = mr->ops->impl.min_access_size;
    if (!access_size_min) {
        access_size_min = 1;
    }
    access_size_max = mr->ops->impl.max_access_size;
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    addr += mr->offset;
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        tmp = mr->ops->read(mr->opaque, addr + i, access_size);
        data |= (tmp & access_mask) << (i * 8);
    }

    return data;
}

static void memory_region_write_thunk_n(void *_mr,
                                        target_phys_addr_t addr,
                                        unsigned size,
                                        uint64_t data)
{
    MemoryRegion *mr = _mr;
    unsigned access_size, access_size_min, access_size_max;
    uint64_t access_mask;
    unsigned i;

    if (!memory_region_access_valid(mr, addr, size)) {
        return; /* FIXME: better signalling */
    }

    /* FIXME: support unaligned access */

    access_size_min = mr->ops->impl.min_access_size;
    if (!access_size_min) {
        access_size_min = 1;
    }
    access_size_max = mr->ops->impl.max_access_size;
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    addr += mr->offset;
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        mr->ops->write(mr->opaque, addr + i, (data >> (i * 8)) & access_mask,
                       access_size);
    }
}

static uint32_t memory_region_read_thunk_b(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 1);
}

static uint32_t memory_region_read_thunk_w(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 2);
}

static uint32_t memory_region_read_thunk_l(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 4);
}

static void memory_region_write_thunk_b(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 1, data);
}

static void memory_region_write_thunk_w(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 2, data);
}

static void memory_region_write_thunk_l(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 4, data);
}

static CPUReadMemoryFunc * const memory_region_read_thunk[] = {
    memory_region_read_thunk_b,
    memory_region_read_thunk_w,
    memory_region_read_thunk_l,
};

static CPUWriteMemoryFunc * const memory_region_write_thunk[] = {
    memory_region_write_thunk_b,
    memory_region_write_thunk_w,
    memory_region_write_thunk_l,
};

void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->has_ram_addr = true;
    mr->ram_addr = cpu_register_io_memory(memory_region_read_thunk,
                                          memory_region_write_thunk,
                                          mr,
                                          mr->ops->endianness);
}

void memory_region_init_ram(MemoryRegion *mr,
                            DeviceState *dev,
                            const char *name,
                            uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->has_ram_addr = true;
    mr->ram_addr = qemu_ram_alloc(dev, name, size);
}

void memory_region_init_ram_ptr(MemoryRegion *mr,
                                DeviceState *dev,
                                const char *name,
                                uint64_t size,
                                void *ptr)
{
    memory_region_init(mr, name, size);
    mr->has_ram_addr = true;
    mr->ram_addr = qemu_ram_alloc_from_ptr(dev, name, size, ptr);
}

void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              target_phys_addr_t offset,
                              uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->alias = orig;
    mr->alias_offset = offset;
}

void memory_region_destroy(MemoryRegion *mr)
{
    assert(QTAILQ_EMPTY(&mr->subregions));
    memory_region_clear_coalescing(mr);
    qemu_free((char *)mr->name);
}

uint64_t memory_region_size(MemoryRegion *mr)
{
    return mr->size;
}

void memory_region_set_offset(MemoryRegion *mr, target_phys_addr_t offset)
{
    mr->offset = offset;
}

void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client)
{
    uint8_t mask = 1 << client;

    mr->dirty_log_mask = (mr->dirty_log_mask & ~mask) | (log * mask);
    memory_region_update_topology();
}

bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             unsigned client)
{
    assert(mr->has_ram_addr);
    return cpu_physical_memory_get_dirty(mr->ram_addr + addr, 1 << client);
}

void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr)
{
    assert(mr->has_ram_addr);
    return cpu_physical_memory_set_dirty(mr->ram_addr + addr);
}

void memory_region_sync_dirty_bitmap(MemoryRegion *mr)
{
    FlatRange *fr;

    FOR_EACH_FLAT_RANGE(fr, &current_memory_map) {
        if (fr->mr == mr) {
            cpu_physical_sync_dirty_bitmap(fr->addr.start,
                                           fr->addr.start + fr->addr.size);
        }
    }
}

void memory_region_set_readonly(MemoryRegion *mr, bool readonly)
{
    /* FIXME */
}

void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client)
{
    assert(mr->has_ram_addr);
    cpu_physical_memory_reset_dirty(mr->ram_addr + addr,
                                    mr->ram_addr + addr + size,
                                    1 << client);
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_ram_ptr(mr->alias) + mr->alias_offset;
    }

    assert(mr->has_ram_addr);

    return qemu_get_ram_ptr(mr->ram_addr);
}

static void memory_region_update_coalesced_range(MemoryRegion *mr)
{
    FlatRange *fr;
    CoalescedMemoryRange *cmr;
    AddrRange tmp;

    FOR_EACH_FLAT_RANGE(fr, &current_memory_map) {
        if (fr->mr == mr) {
            qemu_unregister_coalesced_mmio(fr->addr.start, fr->addr.size);
            QTAILQ_FOREACH(cmr, &mr->coalesced, link) {
                tmp = addrrange_shift(cmr->addr,
                                      fr->addr.start - fr->offset_in_region);
                if (!addrrange_intersects(tmp, fr->addr)) {
                    continue;
                }
                tmp = addrrange_intersection(tmp, fr->addr);
                qemu_register_coalesced_mmio(tmp.start, tmp.size);
            }
        }
    }
}

void memory_region_set_coalescing(MemoryRegion *mr)
{
    memory_region_clear_coalescing(mr);
    memory_region_add_coalescing(mr, 0, mr->size);
}

void memory_region_add_coalescing(MemoryRegion *mr,
                                  target_phys_addr_t offset,
                                  uint64_t size)
{
    CoalescedMemoryRange *cmr = qemu_malloc(sizeof(*cmr));

    cmr->addr = addrrange_make(offset, size);
    QTAILQ_INSERT_TAIL(&mr->coalesced, cmr, link);
    memory_region_update_coalesced_range(mr);
}

void memory_region_clear_coalescing(MemoryRegion *mr)
{
    CoalescedMemoryRange *cmr;

    while (!QTAILQ_EMPTY(&mr->coalesced)) {
        cmr = QTAILQ_FIRST(&mr->coalesced);
        QTAILQ_REMOVE(&mr->coalesced, cmr, link);
        qemu_free(cmr);
    }
    memory_region_update_coalesced_range(mr);
}

static void memory_region_add_subregion_common(MemoryRegion *mr,
                                               target_phys_addr_t offset,
                                               MemoryRegion *subregion)
{
    MemoryRegion *other;

    assert(!subregion->parent);
    subregion->parent = mr;
    subregion->addr = offset;
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->may_overlap || other->may_overlap) {
            continue;
        }
        if (offset >= other->offset + other->size
            || offset + subregion->size <= other->offset) {
            continue;
        }
        printf("warning: subregion collision %llx/%llx vs %llx/%llx\n",
               (unsigned long long)offset,
               (unsigned long long)subregion->size,
               (unsigned long long)other->offset,
               (unsigned long long)other->size);
    }
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_topology();
}


void memory_region_add_subregion(MemoryRegion *mr,
                                 target_phys_addr_t offset,
                                 MemoryRegion *subregion)
{
    subregion->may_overlap = false;
    subregion->priority = 0;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         target_phys_addr_t offset,
                                         MemoryRegion *subregion,
                                         unsigned priority)
{
    subregion->may_overlap = true;
    subregion->priority = priority;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion)
{
    assert(subregion->parent == mr);
    subregion->parent = NULL;
    QTAILQ_REMOVE(&mr->subregions, subregion, subregions_link);
    memory_region_update_topology();
}
