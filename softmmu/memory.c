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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "qapi/visitor.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include "qom/object.h"
#include "trace.h"

#include "exec/memory-internal.h"
#include "exec/ram_addr.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "sysemu/tcg.h"
#include "qemu/accel.h"
#include "hw/boards.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

//#define DEBUG_UNASSIGNED

static unsigned memory_region_transaction_depth;
static bool memory_region_update_pending;
static bool ioeventfd_update_pending;
unsigned int global_dirty_tracking;

static QTAILQ_HEAD(, MemoryListener) memory_listeners
    = QTAILQ_HEAD_INITIALIZER(memory_listeners);

static QTAILQ_HEAD(, AddressSpace) address_spaces
    = QTAILQ_HEAD_INITIALIZER(address_spaces);

static GHashTable *flat_views;

typedef struct AddrRange AddrRange;

/*
 * Note that signed integers are needed for negative offsetting in aliases
 * (large MemoryRegion::alias_offset).
 */
struct AddrRange {
    Int128 start;
    Int128 size;
};

static AddrRange addrrange_make(Int128 start, Int128 size)
{
    return (AddrRange) { start, size };
}

static bool addrrange_equal(AddrRange r1, AddrRange r2)
{
    return int128_eq(r1.start, r2.start) && int128_eq(r1.size, r2.size);
}

static Int128 addrrange_end(AddrRange r)
{
    return int128_add(r.start, r.size);
}

static AddrRange addrrange_shift(AddrRange range, Int128 delta)
{
    int128_addto(&range.start, delta);
    return range;
}

static bool addrrange_contains(AddrRange range, Int128 addr)
{
    return int128_ge(addr, range.start)
        && int128_lt(addr, addrrange_end(range));
}

static bool addrrange_intersects(AddrRange r1, AddrRange r2)
{
    return addrrange_contains(r1, r2.start)
        || addrrange_contains(r2, r1.start);
}

static AddrRange addrrange_intersection(AddrRange r1, AddrRange r2)
{
    Int128 start = int128_max(r1.start, r2.start);
    Int128 end = int128_min(addrrange_end(r1), addrrange_end(r2));
    return addrrange_make(start, int128_sub(end, start));
}

enum ListenerDirection { Forward, Reverse };

#define MEMORY_LISTENER_CALL_GLOBAL(_callback, _direction, _args...)    \
    do {                                                                \
        MemoryListener *_listener;                                      \
                                                                        \
        switch (_direction) {                                           \
        case Forward:                                                   \
            QTAILQ_FOREACH(_listener, &memory_listeners, link) {        \
                if (_listener->_callback) {                             \
                    _listener->_callback(_listener, ##_args);           \
                }                                                       \
            }                                                           \
            break;                                                      \
        case Reverse:                                                   \
            QTAILQ_FOREACH_REVERSE(_listener, &memory_listeners, link) { \
                if (_listener->_callback) {                             \
                    _listener->_callback(_listener, ##_args);           \
                }                                                       \
            }                                                           \
            break;                                                      \
        default:                                                        \
            abort();                                                    \
        }                                                               \
    } while (0)

#define MEMORY_LISTENER_CALL(_as, _callback, _direction, _section, _args...) \
    do {                                                                \
        MemoryListener *_listener;                                      \
                                                                        \
        switch (_direction) {                                           \
        case Forward:                                                   \
            QTAILQ_FOREACH(_listener, &(_as)->listeners, link_as) {     \
                if (_listener->_callback) {                             \
                    _listener->_callback(_listener, _section, ##_args); \
                }                                                       \
            }                                                           \
            break;                                                      \
        case Reverse:                                                   \
            QTAILQ_FOREACH_REVERSE(_listener, &(_as)->listeners, link_as) { \
                if (_listener->_callback) {                             \
                    _listener->_callback(_listener, _section, ##_args); \
                }                                                       \
            }                                                           \
            break;                                                      \
        default:                                                        \
            abort();                                                    \
        }                                                               \
    } while (0)

/* No need to ref/unref .mr, the FlatRange keeps it alive.  */
#define MEMORY_LISTENER_UPDATE_REGION(fr, as, dir, callback, _args...)  \
    do {                                                                \
        MemoryRegionSection mrs = section_from_flat_range(fr,           \
                address_space_to_flatview(as));                         \
        MEMORY_LISTENER_CALL(as, callback, dir, &mrs, ##_args);         \
    } while(0)

struct CoalescedMemoryRange {
    AddrRange addr;
    QTAILQ_ENTRY(CoalescedMemoryRange) link;
};

struct MemoryRegionIoeventfd {
    AddrRange addr;
    bool match_data;
    uint64_t data;
    EventNotifier *e;
};

static bool memory_region_ioeventfd_before(MemoryRegionIoeventfd *a,
                                           MemoryRegionIoeventfd *b)
{
    if (int128_lt(a->addr.start, b->addr.start)) {
        return true;
    } else if (int128_gt(a->addr.start, b->addr.start)) {
        return false;
    } else if (int128_lt(a->addr.size, b->addr.size)) {
        return true;
    } else if (int128_gt(a->addr.size, b->addr.size)) {
        return false;
    } else if (a->match_data < b->match_data) {
        return true;
    } else  if (a->match_data > b->match_data) {
        return false;
    } else if (a->match_data) {
        if (a->data < b->data) {
            return true;
        } else if (a->data > b->data) {
            return false;
        }
    }
    if (a->e < b->e) {
        return true;
    } else if (a->e > b->e) {
        return false;
    }
    return false;
}

static bool memory_region_ioeventfd_equal(MemoryRegionIoeventfd *a,
                                          MemoryRegionIoeventfd *b)
{
    if (int128_eq(a->addr.start, b->addr.start) &&
        (!int128_nz(a->addr.size) || !int128_nz(b->addr.size) ||
         (int128_eq(a->addr.size, b->addr.size) &&
          (a->match_data == b->match_data) &&
          ((a->match_data && (a->data == b->data)) || !a->match_data) &&
          (a->e == b->e))))
        return true;

    return false;
}

/* Range of memory in the global map.  Addresses are absolute. */
struct FlatRange {
    MemoryRegion *mr;
    hwaddr offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
    bool romd_mode;
    bool readonly;
    bool nonvolatile;
};

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

static inline MemoryRegionSection
section_from_flat_range(FlatRange *fr, FlatView *fv)
{
    return (MemoryRegionSection) {
        .mr = fr->mr,
        .fv = fv,
        .offset_within_region = fr->offset_in_region,
        .size = fr->addr.size,
        .offset_within_address_space = int128_get64(fr->addr.start),
        .readonly = fr->readonly,
        .nonvolatile = fr->nonvolatile,
    };
}

static bool flatrange_equal(FlatRange *a, FlatRange *b)
{
    return a->mr == b->mr
        && addrrange_equal(a->addr, b->addr)
        && a->offset_in_region == b->offset_in_region
        && a->romd_mode == b->romd_mode
        && a->readonly == b->readonly
        && a->nonvolatile == b->nonvolatile;
}

static FlatView *flatview_new(MemoryRegion *mr_root)
{
    FlatView *view;

    view = g_new0(FlatView, 1);
    view->ref = 1;
    view->root = mr_root;
    memory_region_ref(mr_root);
    trace_flatview_new(view, mr_root);

    return view;
}

/* Insert a range into a given position.  Caller is responsible for maintaining
 * sorting order.
 */
static void flatview_insert(FlatView *view, unsigned pos, FlatRange *range)
{
    if (view->nr == view->nr_allocated) {
        view->nr_allocated = MAX(2 * view->nr, 10);
        view->ranges = g_realloc(view->ranges,
                                    view->nr_allocated * sizeof(*view->ranges));
    }
    memmove(view->ranges + pos + 1, view->ranges + pos,
            (view->nr - pos) * sizeof(FlatRange));
    view->ranges[pos] = *range;
    memory_region_ref(range->mr);
    ++view->nr;
}

static void flatview_destroy(FlatView *view)
{
    int i;

    trace_flatview_destroy(view, view->root);
    if (view->dispatch) {
        address_space_dispatch_free(view->dispatch);
    }
    for (i = 0; i < view->nr; i++) {
        memory_region_unref(view->ranges[i].mr);
    }
    g_free(view->ranges);
    memory_region_unref(view->root);
    g_free(view);
}

static bool flatview_ref(FlatView *view)
{
    return qatomic_fetch_inc_nonzero(&view->ref) > 0;
}

void flatview_unref(FlatView *view)
{
    if (qatomic_fetch_dec(&view->ref) == 1) {
        trace_flatview_destroy_rcu(view, view->root);
        assert(view->root);
        call_rcu(view, flatview_destroy, rcu);
    }
}

static bool can_merge(FlatRange *r1, FlatRange *r2)
{
    return int128_eq(addrrange_end(r1->addr), r2->addr.start)
        && r1->mr == r2->mr
        && int128_eq(int128_add(int128_make64(r1->offset_in_region),
                                r1->addr.size),
                     int128_make64(r2->offset_in_region))
        && r1->dirty_log_mask == r2->dirty_log_mask
        && r1->romd_mode == r2->romd_mode
        && r1->readonly == r2->readonly
        && r1->nonvolatile == r2->nonvolatile;
}

/* Attempt to simplify a view by merging adjacent ranges */
static void flatview_simplify(FlatView *view)
{
    unsigned i, j, k;

    i = 0;
    while (i < view->nr) {
        j = i + 1;
        while (j < view->nr
               && can_merge(&view->ranges[j-1], &view->ranges[j])) {
            int128_addto(&view->ranges[i].addr.size, view->ranges[j].addr.size);
            ++j;
        }
        ++i;
        for (k = i; k < j; k++) {
            memory_region_unref(view->ranges[k].mr);
        }
        memmove(&view->ranges[i], &view->ranges[j],
                (view->nr - j) * sizeof(view->ranges[j]));
        view->nr -= j - i;
    }
}

static bool memory_region_big_endian(MemoryRegion *mr)
{
#if TARGET_BIG_ENDIAN
    return mr->ops->endianness != DEVICE_LITTLE_ENDIAN;
#else
    return mr->ops->endianness == DEVICE_BIG_ENDIAN;
#endif
}

static void adjust_endianness(MemoryRegion *mr, uint64_t *data, MemOp op)
{
    if ((op & MO_BSWAP) != devend_memop(mr->ops->endianness)) {
        switch (op & MO_SIZE) {
        case MO_8:
            break;
        case MO_16:
            *data = bswap16(*data);
            break;
        case MO_32:
            *data = bswap32(*data);
            break;
        case MO_64:
            *data = bswap64(*data);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static inline void memory_region_shift_read_access(uint64_t *value,
                                                   signed shift,
                                                   uint64_t mask,
                                                   uint64_t tmp)
{
    if (shift >= 0) {
        *value |= (tmp & mask) << shift;
    } else {
        *value |= (tmp & mask) >> -shift;
    }
}

static inline uint64_t memory_region_shift_write_access(uint64_t *value,
                                                        signed shift,
                                                        uint64_t mask)
{
    uint64_t tmp;

    if (shift >= 0) {
        tmp = (*value >> shift) & mask;
    } else {
        tmp = (*value << -shift) & mask;
    }

    return tmp;
}

static hwaddr memory_region_to_absolute_addr(MemoryRegion *mr, hwaddr offset)
{
    MemoryRegion *root;
    hwaddr abs_addr = offset;

    abs_addr += mr->addr;
    for (root = mr; root->container; ) {
        root = root->container;
        abs_addr += root->addr;
    }

    return abs_addr;
}

static int get_cpu_index(void)
{
    if (current_cpu) {
        return current_cpu->cpu_index;
    }
    return -1;
}

static MemTxResult  memory_region_read_accessor(MemoryRegion *mr,
                                                hwaddr addr,
                                                uint64_t *value,
                                                unsigned size,
                                                signed shift,
                                                uint64_t mask,
                                                MemTxAttrs attrs)
{
    uint64_t tmp;

    tmp = mr->ops->read(mr->opaque, addr, size);
    if (mr->subpage) {
        trace_memory_region_subpage_read(get_cpu_index(), mr, addr, tmp, size);
    } else if (trace_event_get_state_backends(TRACE_MEMORY_REGION_OPS_READ)) {
        hwaddr abs_addr = memory_region_to_absolute_addr(mr, addr);
        trace_memory_region_ops_read(get_cpu_index(), mr, abs_addr, tmp, size,
                                     memory_region_name(mr));
    }
    memory_region_shift_read_access(value, shift, mask, tmp);
    return MEMTX_OK;
}

static MemTxResult memory_region_read_with_attrs_accessor(MemoryRegion *mr,
                                                          hwaddr addr,
                                                          uint64_t *value,
                                                          unsigned size,
                                                          signed shift,
                                                          uint64_t mask,
                                                          MemTxAttrs attrs)
{
    uint64_t tmp = 0;
    MemTxResult r;

    r = mr->ops->read_with_attrs(mr->opaque, addr, &tmp, size, attrs);
    if (mr->subpage) {
        trace_memory_region_subpage_read(get_cpu_index(), mr, addr, tmp, size);
    } else if (trace_event_get_state_backends(TRACE_MEMORY_REGION_OPS_READ)) {
        hwaddr abs_addr = memory_region_to_absolute_addr(mr, addr);
        trace_memory_region_ops_read(get_cpu_index(), mr, abs_addr, tmp, size,
                                     memory_region_name(mr));
    }
    memory_region_shift_read_access(value, shift, mask, tmp);
    return r;
}

static MemTxResult memory_region_write_accessor(MemoryRegion *mr,
                                                hwaddr addr,
                                                uint64_t *value,
                                                unsigned size,
                                                signed shift,
                                                uint64_t mask,
                                                MemTxAttrs attrs)
{
    uint64_t tmp = memory_region_shift_write_access(value, shift, mask);

    if (mr->subpage) {
        trace_memory_region_subpage_write(get_cpu_index(), mr, addr, tmp, size);
    } else if (trace_event_get_state_backends(TRACE_MEMORY_REGION_OPS_WRITE)) {
        hwaddr abs_addr = memory_region_to_absolute_addr(mr, addr);
        trace_memory_region_ops_write(get_cpu_index(), mr, abs_addr, tmp, size,
                                      memory_region_name(mr));
    }
    mr->ops->write(mr->opaque, addr, tmp, size);
    return MEMTX_OK;
}

static MemTxResult memory_region_write_with_attrs_accessor(MemoryRegion *mr,
                                                           hwaddr addr,
                                                           uint64_t *value,
                                                           unsigned size,
                                                           signed shift,
                                                           uint64_t mask,
                                                           MemTxAttrs attrs)
{
    uint64_t tmp = memory_region_shift_write_access(value, shift, mask);

    if (mr->subpage) {
        trace_memory_region_subpage_write(get_cpu_index(), mr, addr, tmp, size);
    } else if (trace_event_get_state_backends(TRACE_MEMORY_REGION_OPS_WRITE)) {
        hwaddr abs_addr = memory_region_to_absolute_addr(mr, addr);
        trace_memory_region_ops_write(get_cpu_index(), mr, abs_addr, tmp, size,
                                      memory_region_name(mr));
    }
    return mr->ops->write_with_attrs(mr->opaque, addr, tmp, size, attrs);
}

static MemTxResult access_with_adjusted_size(hwaddr addr,
                                      uint64_t *value,
                                      unsigned size,
                                      unsigned access_size_min,
                                      unsigned access_size_max,
                                      MemTxResult (*access_fn)
                                                  (MemoryRegion *mr,
                                                   hwaddr addr,
                                                   uint64_t *value,
                                                   unsigned size,
                                                   signed shift,
                                                   uint64_t mask,
                                                   MemTxAttrs attrs),
                                      MemoryRegion *mr,
                                      MemTxAttrs attrs)
{
    uint64_t access_mask;
    unsigned access_size;
    unsigned i;
    MemTxResult r = MEMTX_OK;

    if (!access_size_min) {
        access_size_min = 1;
    }
    if (!access_size_max) {
        access_size_max = 4;
    }

    /* Do not allow more than one simultaneous access to a device's IO Regions */
    if (mr->dev && !mr->disable_reentrancy_guard &&
        !mr->ram_device && !mr->ram && !mr->rom_device && !mr->readonly) {
        if (mr->dev->mem_reentrancy_guard.engaged_in_io) {
            warn_report_once("Blocked re-entrant IO on MemoryRegion: "
                             "%s at addr: 0x%" HWADDR_PRIX,
                             memory_region_name(mr), addr);
            return MEMTX_ACCESS_ERROR;
        }
        mr->dev->mem_reentrancy_guard.engaged_in_io = true;
    }

    /* FIXME: support unaligned access? */
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = MAKE_64BIT_MASK(0, access_size * 8);
    if (memory_region_big_endian(mr)) {
        for (i = 0; i < size; i += access_size) {
            r |= access_fn(mr, addr + i, value, access_size,
                        (size - access_size - i) * 8, access_mask, attrs);
        }
    } else {
        for (i = 0; i < size; i += access_size) {
            r |= access_fn(mr, addr + i, value, access_size, i * 8,
                        access_mask, attrs);
        }
    }
    if (mr->dev) {
        mr->dev->mem_reentrancy_guard.engaged_in_io = false;
    }
    return r;
}

static AddressSpace *memory_region_to_address_space(MemoryRegion *mr)
{
    AddressSpace *as;

    while (mr->container) {
        mr = mr->container;
    }
    QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
        if (mr == as->root) {
            return as;
        }
    }
    return NULL;
}

/* Render a memory region into the global view.  Ranges in @view obscure
 * ranges in @mr.
 */
static void render_memory_region(FlatView *view,
                                 MemoryRegion *mr,
                                 Int128 base,
                                 AddrRange clip,
                                 bool readonly,
                                 bool nonvolatile)
{
    MemoryRegion *subregion;
    unsigned i;
    hwaddr offset_in_region;
    Int128 remain;
    Int128 now;
    FlatRange fr;
    AddrRange tmp;

    if (!mr->enabled) {
        return;
    }

    int128_addto(&base, int128_make64(mr->addr));
    readonly |= mr->readonly;
    nonvolatile |= mr->nonvolatile;

    tmp = addrrange_make(base, mr->size);

    if (!addrrange_intersects(tmp, clip)) {
        return;
    }

    clip = addrrange_intersection(tmp, clip);

    if (mr->alias) {
        int128_subfrom(&base, int128_make64(mr->alias->addr));
        int128_subfrom(&base, int128_make64(mr->alias_offset));
        render_memory_region(view, mr->alias, base, clip,
                             readonly, nonvolatile);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip,
                             readonly, nonvolatile);
    }

    if (!mr->terminates) {
        return;
    }

    offset_in_region = int128_get64(int128_sub(clip.start, base));
    base = clip.start;
    remain = clip.size;

    fr.mr = mr;
    fr.dirty_log_mask = memory_region_get_dirty_log_mask(mr);
    fr.romd_mode = mr->romd_mode;
    fr.readonly = readonly;
    fr.nonvolatile = nonvolatile;

    /* Render the region itself into any gaps left by the current view. */
    for (i = 0; i < view->nr && int128_nz(remain); ++i) {
        if (int128_ge(base, addrrange_end(view->ranges[i].addr))) {
            continue;
        }
        if (int128_lt(base, view->ranges[i].addr.start)) {
            now = int128_min(remain,
                             int128_sub(view->ranges[i].addr.start, base));
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            flatview_insert(view, i, &fr);
            ++i;
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
        now = int128_sub(int128_min(int128_add(base, remain),
                                    addrrange_end(view->ranges[i].addr)),
                         base);
        int128_addto(&base, now);
        offset_in_region += int128_get64(now);
        int128_subfrom(&remain, now);
    }
    if (int128_nz(remain)) {
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        flatview_insert(view, i, &fr);
    }
}

void flatview_for_each_range(FlatView *fv, flatview_cb cb , void *opaque)
{
    FlatRange *fr;

    assert(fv);
    assert(cb);

    FOR_EACH_FLAT_RANGE(fr, fv) {
        if (cb(fr->addr.start, fr->addr.size, fr->mr,
               fr->offset_in_region, opaque)) {
            break;
        }
    }
}

static MemoryRegion *memory_region_get_flatview_root(MemoryRegion *mr)
{
    while (mr->enabled) {
        if (mr->alias) {
            if (!mr->alias_offset && int128_ge(mr->size, mr->alias->size)) {
                /* The alias is included in its entirety.  Use it as
                 * the "real" root, so that we can share more FlatViews.
                 */
                mr = mr->alias;
                continue;
            }
        } else if (!mr->terminates) {
            unsigned int found = 0;
            MemoryRegion *child, *next = NULL;
            QTAILQ_FOREACH(child, &mr->subregions, subregions_link) {
                if (child->enabled) {
                    if (++found > 1) {
                        next = NULL;
                        break;
                    }
                    if (!child->addr && int128_ge(mr->size, child->size)) {
                        /* A child is included in its entirety.  If it's the only
                         * enabled one, use it in the hope of finding an alias down the
                         * way. This will also let us share FlatViews.
                         */
                        next = child;
                    }
                }
            }
            if (found == 0) {
                return NULL;
            }
            if (next) {
                mr = next;
                continue;
            }
        }

        return mr;
    }

    return NULL;
}

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView *generate_memory_topology(MemoryRegion *mr)
{
    int i;
    FlatView *view;

    view = flatview_new(mr);

    if (mr) {
        render_memory_region(view, mr, int128_zero(),
                             addrrange_make(int128_zero(), int128_2_64()),
                             false, false);
    }
    flatview_simplify(view);

    view->dispatch = address_space_dispatch_new(view);
    for (i = 0; i < view->nr; i++) {
        MemoryRegionSection mrs =
            section_from_flat_range(&view->ranges[i], view);
        flatview_add_to_dispatch(view, &mrs);
    }
    address_space_dispatch_compact(view->dispatch);
    g_hash_table_replace(flat_views, mr, view);

    return view;
}

static void address_space_add_del_ioeventfds(AddressSpace *as,
                                             MemoryRegionIoeventfd *fds_new,
                                             unsigned fds_new_nb,
                                             MemoryRegionIoeventfd *fds_old,
                                             unsigned fds_old_nb)
{
    unsigned iold, inew;
    MemoryRegionIoeventfd *fd;
    MemoryRegionSection section;

    /* Generate a symmetric difference of the old and new fd sets, adding
     * and deleting as necessary.
     */

    iold = inew = 0;
    while (iold < fds_old_nb || inew < fds_new_nb) {
        if (iold < fds_old_nb
            && (inew == fds_new_nb
                || memory_region_ioeventfd_before(&fds_old[iold],
                                                  &fds_new[inew]))) {
            fd = &fds_old[iold];
            section = (MemoryRegionSection) {
                .fv = address_space_to_flatview(as),
                .offset_within_address_space = int128_get64(fd->addr.start),
                .size = fd->addr.size,
            };
            MEMORY_LISTENER_CALL(as, eventfd_del, Forward, &section,
                                 fd->match_data, fd->data, fd->e);
            ++iold;
        } else if (inew < fds_new_nb
                   && (iold == fds_old_nb
                       || memory_region_ioeventfd_before(&fds_new[inew],
                                                         &fds_old[iold]))) {
            fd = &fds_new[inew];
            section = (MemoryRegionSection) {
                .fv = address_space_to_flatview(as),
                .offset_within_address_space = int128_get64(fd->addr.start),
                .size = fd->addr.size,
            };
            MEMORY_LISTENER_CALL(as, eventfd_add, Reverse, &section,
                                 fd->match_data, fd->data, fd->e);
            ++inew;
        } else {
            ++iold;
            ++inew;
        }
    }
}

FlatView *address_space_get_flatview(AddressSpace *as)
{
    FlatView *view;

    RCU_READ_LOCK_GUARD();
    do {
        view = address_space_to_flatview(as);
        /* If somebody has replaced as->current_map concurrently,
         * flatview_ref returns false.
         */
    } while (!flatview_ref(view));
    return view;
}

static void address_space_update_ioeventfds(AddressSpace *as)
{
    FlatView *view;
    FlatRange *fr;
    unsigned ioeventfd_nb = 0;
    unsigned ioeventfd_max;
    MemoryRegionIoeventfd *ioeventfds;
    AddrRange tmp;
    unsigned i;

    /*
     * It is likely that the number of ioeventfds hasn't changed much, so use
     * the previous size as the starting value, with some headroom to avoid
     * gratuitous reallocations.
     */
    ioeventfd_max = QEMU_ALIGN_UP(as->ioeventfd_nb, 4);
    ioeventfds = g_new(MemoryRegionIoeventfd, ioeventfd_max);

    view = address_space_get_flatview(as);
    FOR_EACH_FLAT_RANGE(fr, view) {
        for (i = 0; i < fr->mr->ioeventfd_nb; ++i) {
            tmp = addrrange_shift(fr->mr->ioeventfds[i].addr,
                                  int128_sub(fr->addr.start,
                                             int128_make64(fr->offset_in_region)));
            if (addrrange_intersects(fr->addr, tmp)) {
                ++ioeventfd_nb;
                if (ioeventfd_nb > ioeventfd_max) {
                    ioeventfd_max = MAX(ioeventfd_max * 2, 4);
                    ioeventfds = g_realloc(ioeventfds,
                            ioeventfd_max * sizeof(*ioeventfds));
                }
                ioeventfds[ioeventfd_nb-1] = fr->mr->ioeventfds[i];
                ioeventfds[ioeventfd_nb-1].addr = tmp;
            }
        }
    }

    address_space_add_del_ioeventfds(as, ioeventfds, ioeventfd_nb,
                                     as->ioeventfds, as->ioeventfd_nb);

    g_free(as->ioeventfds);
    as->ioeventfds = ioeventfds;
    as->ioeventfd_nb = ioeventfd_nb;
    flatview_unref(view);
}

/*
 * Notify the memory listeners about the coalesced IO change events of
 * range `cmr'.  Only the part that has intersection of the specified
 * FlatRange will be sent.
 */
static void flat_range_coalesced_io_notify(FlatRange *fr, AddressSpace *as,
                                           CoalescedMemoryRange *cmr, bool add)
{
    AddrRange tmp;

    tmp = addrrange_shift(cmr->addr,
                          int128_sub(fr->addr.start,
                                     int128_make64(fr->offset_in_region)));
    if (!addrrange_intersects(tmp, fr->addr)) {
        return;
    }
    tmp = addrrange_intersection(tmp, fr->addr);

    if (add) {
        MEMORY_LISTENER_UPDATE_REGION(fr, as, Forward, coalesced_io_add,
                                      int128_get64(tmp.start),
                                      int128_get64(tmp.size));
    } else {
        MEMORY_LISTENER_UPDATE_REGION(fr, as, Reverse, coalesced_io_del,
                                      int128_get64(tmp.start),
                                      int128_get64(tmp.size));
    }
}

static void flat_range_coalesced_io_del(FlatRange *fr, AddressSpace *as)
{
    CoalescedMemoryRange *cmr;

    QTAILQ_FOREACH(cmr, &fr->mr->coalesced, link) {
        flat_range_coalesced_io_notify(fr, as, cmr, false);
    }
}

static void flat_range_coalesced_io_add(FlatRange *fr, AddressSpace *as)
{
    MemoryRegion *mr = fr->mr;
    CoalescedMemoryRange *cmr;

    if (QTAILQ_EMPTY(&mr->coalesced)) {
        return;
    }

    QTAILQ_FOREACH(cmr, &mr->coalesced, link) {
        flat_range_coalesced_io_notify(fr, as, cmr, true);
    }
}

static void address_space_update_topology_pass(AddressSpace *as,
                                               const FlatView *old_view,
                                               const FlatView *new_view,
                                               bool adding)
{
    unsigned iold, inew;
    FlatRange *frold, *frnew;

    /* Generate a symmetric difference of the old and new memory maps.
     * Kill ranges in the old map, and instantiate ranges in the new map.
     */
    iold = inew = 0;
    while (iold < old_view->nr || inew < new_view->nr) {
        if (iold < old_view->nr) {
            frold = &old_view->ranges[iold];
        } else {
            frold = NULL;
        }
        if (inew < new_view->nr) {
            frnew = &new_view->ranges[inew];
        } else {
            frnew = NULL;
        }

        if (frold
            && (!frnew
                || int128_lt(frold->addr.start, frnew->addr.start)
                || (int128_eq(frold->addr.start, frnew->addr.start)
                    && !flatrange_equal(frold, frnew)))) {
            /* In old but not in new, or in both but attributes changed. */

            if (!adding) {
                flat_range_coalesced_io_del(frold, as);
                MEMORY_LISTENER_UPDATE_REGION(frold, as, Reverse, region_del);
            }

            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both and unchanged (except logging may have changed) */

            if (adding) {
                MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, region_nop);
                if (frnew->dirty_log_mask & ~frold->dirty_log_mask) {
                    MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, log_start,
                                                  frold->dirty_log_mask,
                                                  frnew->dirty_log_mask);
                }
                if (frold->dirty_log_mask & ~frnew->dirty_log_mask) {
                    MEMORY_LISTENER_UPDATE_REGION(frnew, as, Reverse, log_stop,
                                                  frold->dirty_log_mask,
                                                  frnew->dirty_log_mask);
                }
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            if (adding) {
                MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, region_add);
                flat_range_coalesced_io_add(frnew, as);
            }

            ++inew;
        }
    }
}

static void flatviews_init(void)
{
    static FlatView *empty_view;

    if (flat_views) {
        return;
    }

    flat_views = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                       (GDestroyNotify) flatview_unref);
    if (!empty_view) {
        empty_view = generate_memory_topology(NULL);
        /* We keep it alive forever in the global variable.  */
        flatview_ref(empty_view);
    } else {
        g_hash_table_replace(flat_views, NULL, empty_view);
        flatview_ref(empty_view);
    }
}

static void flatviews_reset(void)
{
    AddressSpace *as;

    if (flat_views) {
        g_hash_table_unref(flat_views);
        flat_views = NULL;
    }
    flatviews_init();

    /* Render unique FVs */
    QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
        MemoryRegion *physmr = memory_region_get_flatview_root(as->root);

        if (g_hash_table_lookup(flat_views, physmr)) {
            continue;
        }

        generate_memory_topology(physmr);
    }
}

static void address_space_set_flatview(AddressSpace *as)
{
    FlatView *old_view = address_space_to_flatview(as);
    MemoryRegion *physmr = memory_region_get_flatview_root(as->root);
    FlatView *new_view = g_hash_table_lookup(flat_views, physmr);

    assert(new_view);

    if (old_view == new_view) {
        return;
    }

    if (old_view) {
        flatview_ref(old_view);
    }

    flatview_ref(new_view);

    if (!QTAILQ_EMPTY(&as->listeners)) {
        FlatView tmpview = { .nr = 0 }, *old_view2 = old_view;

        if (!old_view2) {
            old_view2 = &tmpview;
        }
        address_space_update_topology_pass(as, old_view2, new_view, false);
        address_space_update_topology_pass(as, old_view2, new_view, true);
    }

    /* Writes are protected by the BQL.  */
    qatomic_rcu_set(&as->current_map, new_view);
    if (old_view) {
        flatview_unref(old_view);
    }

    /* Note that all the old MemoryRegions are still alive up to this
     * point.  This relieves most MemoryListeners from the need to
     * ref/unref the MemoryRegions they get---unless they use them
     * outside the iothread mutex, in which case precise reference
     * counting is necessary.
     */
    if (old_view) {
        flatview_unref(old_view);
    }
}

static void address_space_update_topology(AddressSpace *as)
{
    MemoryRegion *physmr = memory_region_get_flatview_root(as->root);

    flatviews_init();
    if (!g_hash_table_lookup(flat_views, physmr)) {
        generate_memory_topology(physmr);
    }
    address_space_set_flatview(as);
}

void memory_region_transaction_begin(void)
{
    qemu_flush_coalesced_mmio_buffer();
    ++memory_region_transaction_depth;
}

void memory_region_transaction_commit(void)
{
    AddressSpace *as;

    assert(memory_region_transaction_depth);
    assert(qemu_mutex_iothread_locked());

    --memory_region_transaction_depth;
    if (!memory_region_transaction_depth) {
        if (memory_region_update_pending) {
            flatviews_reset();

            MEMORY_LISTENER_CALL_GLOBAL(begin, Forward);

            QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
                address_space_set_flatview(as);
                address_space_update_ioeventfds(as);
            }
            memory_region_update_pending = false;
            ioeventfd_update_pending = false;
            MEMORY_LISTENER_CALL_GLOBAL(commit, Forward);
        } else if (ioeventfd_update_pending) {
            QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
                address_space_update_ioeventfds(as);
            }
            ioeventfd_update_pending = false;
        }
   }
}

static void memory_region_destructor_none(MemoryRegion *mr)
{
}

static void memory_region_destructor_ram(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_block);
}

static bool memory_region_need_escape(char c)
{
    return c == '/' || c == '[' || c == '\\' || c == ']';
}

static char *memory_region_escape_name(const char *name)
{
    const char *p;
    char *escaped, *q;
    uint8_t c;
    size_t bytes = 0;

    for (p = name; *p; p++) {
        bytes += memory_region_need_escape(*p) ? 4 : 1;
    }
    if (bytes == p - name) {
       return g_memdup(name, bytes + 1);
    }

    escaped = g_malloc(bytes + 1);
    for (p = name, q = escaped; *p; p++) {
        c = *p;
        if (unlikely(memory_region_need_escape(c))) {
            *q++ = '\\';
            *q++ = 'x';
            *q++ = "0123456789abcdef"[c >> 4];
            c = "0123456789abcdef"[c & 15];
        }
        *q++ = c;
    }
    *q = 0;
    return escaped;
}

static void memory_region_do_init(MemoryRegion *mr,
                                  Object *owner,
                                  const char *name,
                                  uint64_t size)
{
    mr->size = int128_make64(size);
    if (size == UINT64_MAX) {
        mr->size = int128_2_64();
    }
    mr->name = g_strdup(name);
    mr->owner = owner;
    mr->dev = (DeviceState *) object_dynamic_cast(mr->owner, TYPE_DEVICE);
    mr->ram_block = NULL;

    if (name) {
        char *escaped_name = memory_region_escape_name(name);
        char *name_array = g_strdup_printf("%s[*]", escaped_name);

        if (!owner) {
            owner = container_get(qdev_get_machine(), "/unattached");
        }

        object_property_add_child(owner, name_array, OBJECT(mr));
        object_unref(OBJECT(mr));
        g_free(name_array);
        g_free(escaped_name);
    }
}

void memory_region_init(MemoryRegion *mr,
                        Object *owner,
                        const char *name,
                        uint64_t size)
{
    object_initialize(mr, sizeof(*mr), TYPE_MEMORY_REGION);
    memory_region_do_init(mr, owner, name, size);
}

static void memory_region_get_container(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    MemoryRegion *mr = MEMORY_REGION(obj);
    char *path = (char *)"";

    if (mr->container) {
        path = object_get_canonical_path(OBJECT(mr->container));
    }
    visit_type_str(v, name, &path, errp);
    if (mr->container) {
        g_free(path);
    }
}

static Object *memory_region_resolve_container(Object *obj, void *opaque,
                                               const char *part)
{
    MemoryRegion *mr = MEMORY_REGION(obj);

    return OBJECT(mr->container);
}

static void memory_region_get_priority(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    MemoryRegion *mr = MEMORY_REGION(obj);
    int32_t value = mr->priority;

    visit_type_int32(v, name, &value, errp);
}

static void memory_region_get_size(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    MemoryRegion *mr = MEMORY_REGION(obj);
    uint64_t value = memory_region_size(mr);

    visit_type_uint64(v, name, &value, errp);
}

static void memory_region_initfn(Object *obj)
{
    MemoryRegion *mr = MEMORY_REGION(obj);
    ObjectProperty *op;

    mr->ops = &unassigned_mem_ops;
    mr->enabled = true;
    mr->romd_mode = true;
    mr->destructor = memory_region_destructor_none;
    QTAILQ_INIT(&mr->subregions);
    QTAILQ_INIT(&mr->coalesced);

    op = object_property_add(OBJECT(mr), "container",
                             "link<" TYPE_MEMORY_REGION ">",
                             memory_region_get_container,
                             NULL, /* memory_region_set_container */
                             NULL, NULL);
    op->resolve = memory_region_resolve_container;

    object_property_add_uint64_ptr(OBJECT(mr), "addr",
                                   &mr->addr, OBJ_PROP_FLAG_READ);
    object_property_add(OBJECT(mr), "priority", "uint32",
                        memory_region_get_priority,
                        NULL, /* memory_region_set_priority */
                        NULL, NULL);
    object_property_add(OBJECT(mr), "size", "uint64",
                        memory_region_get_size,
                        NULL, /* memory_region_set_size, */
                        NULL, NULL);
}

static void iommu_memory_region_initfn(Object *obj)
{
    MemoryRegion *mr = MEMORY_REGION(obj);

    mr->is_iommu = true;
}

static uint64_t unassigned_mem_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem read " HWADDR_FMT_plx "\n", addr);
#endif
    return 0;
}

static void unassigned_mem_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem write " HWADDR_FMT_plx " = 0x%"PRIx64"\n", addr, val);
#endif
}

static bool unassigned_mem_accepts(void *opaque, hwaddr addr,
                                   unsigned size, bool is_write,
                                   MemTxAttrs attrs)
{
    return false;
}

const MemoryRegionOps unassigned_mem_ops = {
    .valid.accepts = unassigned_mem_accepts,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t memory_region_ram_device_read(void *opaque,
                                              hwaddr addr, unsigned size)
{
    MemoryRegion *mr = opaque;
    uint64_t data = (uint64_t)~0;

    switch (size) {
    case 1:
        data = *(uint8_t *)(mr->ram_block->host + addr);
        break;
    case 2:
        data = *(uint16_t *)(mr->ram_block->host + addr);
        break;
    case 4:
        data = *(uint32_t *)(mr->ram_block->host + addr);
        break;
    case 8:
        data = *(uint64_t *)(mr->ram_block->host + addr);
        break;
    }

    trace_memory_region_ram_device_read(get_cpu_index(), mr, addr, data, size);

    return data;
}

static void memory_region_ram_device_write(void *opaque, hwaddr addr,
                                           uint64_t data, unsigned size)
{
    MemoryRegion *mr = opaque;

    trace_memory_region_ram_device_write(get_cpu_index(), mr, addr, data, size);

    switch (size) {
    case 1:
        *(uint8_t *)(mr->ram_block->host + addr) = (uint8_t)data;
        break;
    case 2:
        *(uint16_t *)(mr->ram_block->host + addr) = (uint16_t)data;
        break;
    case 4:
        *(uint32_t *)(mr->ram_block->host + addr) = (uint32_t)data;
        break;
    case 8:
        *(uint64_t *)(mr->ram_block->host + addr) = data;
        break;
    }
}

static const MemoryRegionOps ram_device_mem_ops = {
    .read = memory_region_ram_device_read,
    .write = memory_region_ram_device_write,
    .endianness = DEVICE_HOST_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

bool memory_region_access_valid(MemoryRegion *mr,
                                hwaddr addr,
                                unsigned size,
                                bool is_write,
                                MemTxAttrs attrs)
{
    if (mr->ops->valid.accepts
        && !mr->ops->valid.accepts(mr->opaque, addr, size, is_write, attrs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid %s at addr 0x%" HWADDR_PRIX
                      ", size %u, region '%s', reason: rejected\n",
                      is_write ? "write" : "read",
                      addr, size, memory_region_name(mr));
        return false;
    }

    if (!mr->ops->valid.unaligned && (addr & (size - 1))) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid %s at addr 0x%" HWADDR_PRIX
                      ", size %u, region '%s', reason: unaligned\n",
                      is_write ? "write" : "read",
                      addr, size, memory_region_name(mr));
        return false;
    }

    /* Treat zero as compatibility all valid */
    if (!mr->ops->valid.max_access_size) {
        return true;
    }

    if (size > mr->ops->valid.max_access_size
        || size < mr->ops->valid.min_access_size) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid %s at addr 0x%" HWADDR_PRIX
                      ", size %u, region '%s', reason: invalid size "
                      "(min:%u max:%u)\n",
                      is_write ? "write" : "read",
                      addr, size, memory_region_name(mr),
                      mr->ops->valid.min_access_size,
                      mr->ops->valid.max_access_size);
        return false;
    }
    return true;
}

static MemTxResult memory_region_dispatch_read1(MemoryRegion *mr,
                                                hwaddr addr,
                                                uint64_t *pval,
                                                unsigned size,
                                                MemTxAttrs attrs)
{
    *pval = 0;

    if (mr->ops->read) {
        return access_with_adjusted_size(addr, pval, size,
                                         mr->ops->impl.min_access_size,
                                         mr->ops->impl.max_access_size,
                                         memory_region_read_accessor,
                                         mr, attrs);
    } else {
        return access_with_adjusted_size(addr, pval, size,
                                         mr->ops->impl.min_access_size,
                                         mr->ops->impl.max_access_size,
                                         memory_region_read_with_attrs_accessor,
                                         mr, attrs);
    }
}

MemTxResult memory_region_dispatch_read(MemoryRegion *mr,
                                        hwaddr addr,
                                        uint64_t *pval,
                                        MemOp op,
                                        MemTxAttrs attrs)
{
    unsigned size = memop_size(op);
    MemTxResult r;

    if (mr->alias) {
        return memory_region_dispatch_read(mr->alias,
                                           mr->alias_offset + addr,
                                           pval, op, attrs);
    }
    if (!memory_region_access_valid(mr, addr, size, false, attrs)) {
        *pval = unassigned_mem_read(mr, addr, size);
        return MEMTX_DECODE_ERROR;
    }

    r = memory_region_dispatch_read1(mr, addr, pval, size, attrs);
    adjust_endianness(mr, pval, op);
    return r;
}

/* Return true if an eventfd was signalled */
static bool memory_region_dispatch_write_eventfds(MemoryRegion *mr,
                                                    hwaddr addr,
                                                    uint64_t data,
                                                    unsigned size,
                                                    MemTxAttrs attrs)
{
    MemoryRegionIoeventfd ioeventfd = {
        .addr = addrrange_make(int128_make64(addr), int128_make64(size)),
        .data = data,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; i++) {
        ioeventfd.match_data = mr->ioeventfds[i].match_data;
        ioeventfd.e = mr->ioeventfds[i].e;

        if (memory_region_ioeventfd_equal(&ioeventfd, &mr->ioeventfds[i])) {
            event_notifier_set(ioeventfd.e);
            return true;
        }
    }

    return false;
}

MemTxResult memory_region_dispatch_write(MemoryRegion *mr,
                                         hwaddr addr,
                                         uint64_t data,
                                         MemOp op,
                                         MemTxAttrs attrs)
{
    unsigned size = memop_size(op);

    if (mr->alias) {
        return memory_region_dispatch_write(mr->alias,
                                            mr->alias_offset + addr,
                                            data, op, attrs);
    }
    if (!memory_region_access_valid(mr, addr, size, true, attrs)) {
        unassigned_mem_write(mr, addr, data, size);
        return MEMTX_DECODE_ERROR;
    }

    adjust_endianness(mr, &data, op);

    if ((!kvm_eventfds_enabled()) &&
        memory_region_dispatch_write_eventfds(mr, addr, data, size, attrs)) {
        return MEMTX_OK;
    }

    if (mr->ops->write) {
        return access_with_adjusted_size(addr, &data, size,
                                         mr->ops->impl.min_access_size,
                                         mr->ops->impl.max_access_size,
                                         memory_region_write_accessor, mr,
                                         attrs);
    } else {
        return
            access_with_adjusted_size(addr, &data, size,
                                      mr->ops->impl.min_access_size,
                                      mr->ops->impl.max_access_size,
                                      memory_region_write_with_attrs_accessor,
                                      mr, attrs);
    }
}

void memory_region_init_io(MemoryRegion *mr,
                           Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->ops = ops ? ops : &unassigned_mem_ops;
    mr->opaque = opaque;
    mr->terminates = true;
}

void memory_region_init_ram_nomigrate(MemoryRegion *mr,
                                      Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      Error **errp)
{
    memory_region_init_ram_flags_nomigrate(mr, owner, name, size, 0, errp);
}

void memory_region_init_ram_flags_nomigrate(MemoryRegion *mr,
                                            Object *owner,
                                            const char *name,
                                            uint64_t size,
                                            uint32_t ram_flags,
                                            Error **errp)
{
    Error *err = NULL;
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;
    mr->ram_block = qemu_ram_alloc(size, ram_flags, mr, &err);
    if (err) {
        mr->size = int128_zero();
        object_unparent(OBJECT(mr));
        error_propagate(errp, err);
    }
}

void memory_region_init_resizeable_ram(MemoryRegion *mr,
                                       Object *owner,
                                       const char *name,
                                       uint64_t size,
                                       uint64_t max_size,
                                       void (*resized)(const char*,
                                                       uint64_t length,
                                                       void *host),
                                       Error **errp)
{
    Error *err = NULL;
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;
    mr->ram_block = qemu_ram_alloc_resizeable(size, max_size, resized,
                                              mr, &err);
    if (err) {
        mr->size = int128_zero();
        object_unparent(OBJECT(mr));
        error_propagate(errp, err);
    }
}

#ifdef CONFIG_POSIX
void memory_region_init_ram_from_file(MemoryRegion *mr,
                                      Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      uint64_t align,
                                      uint32_t ram_flags,
                                      const char *path,
                                      bool readonly,
                                      Error **errp)
{
    Error *err = NULL;
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->readonly = readonly;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;
    mr->align = align;
    mr->ram_block = qemu_ram_alloc_from_file(size, mr, ram_flags, path,
                                             readonly, &err);
    if (err) {
        mr->size = int128_zero();
        object_unparent(OBJECT(mr));
        error_propagate(errp, err);
    }
}

void memory_region_init_ram_from_fd(MemoryRegion *mr,
                                    Object *owner,
                                    const char *name,
                                    uint64_t size,
                                    uint32_t ram_flags,
                                    int fd,
                                    ram_addr_t offset,
                                    Error **errp)
{
    Error *err = NULL;
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;
    mr->ram_block = qemu_ram_alloc_from_fd(size, mr, ram_flags, fd, offset,
                                           false, &err);
    if (err) {
        mr->size = int128_zero();
        object_unparent(OBJECT(mr));
        error_propagate(errp, err);
    }
}
#endif

void memory_region_init_ram_ptr(MemoryRegion *mr,
                                Object *owner,
                                const char *name,
                                uint64_t size,
                                void *ptr)
{
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;

    /* qemu_ram_alloc_from_ptr cannot fail with ptr != NULL.  */
    assert(ptr != NULL);
    mr->ram_block = qemu_ram_alloc_from_ptr(size, ptr, mr, &error_fatal);
}

void memory_region_init_ram_device_ptr(MemoryRegion *mr,
                                       Object *owner,
                                       const char *name,
                                       uint64_t size,
                                       void *ptr)
{
    memory_region_init(mr, owner, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->ram_device = true;
    mr->ops = &ram_device_mem_ops;
    mr->opaque = mr;
    mr->destructor = memory_region_destructor_ram;

    /* qemu_ram_alloc_from_ptr cannot fail with ptr != NULL.  */
    assert(ptr != NULL);
    mr->ram_block = qemu_ram_alloc_from_ptr(size, ptr, mr, &error_fatal);
}

void memory_region_init_alias(MemoryRegion *mr,
                              Object *owner,
                              const char *name,
                              MemoryRegion *orig,
                              hwaddr offset,
                              uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->alias = orig;
    mr->alias_offset = offset;
}

void memory_region_init_rom_nomigrate(MemoryRegion *mr,
                                      Object *owner,
                                      const char *name,
                                      uint64_t size,
                                      Error **errp)
{
    memory_region_init_ram_flags_nomigrate(mr, owner, name, size, 0, errp);
    mr->readonly = true;
}

void memory_region_init_rom_device_nomigrate(MemoryRegion *mr,
                                             Object *owner,
                                             const MemoryRegionOps *ops,
                                             void *opaque,
                                             const char *name,
                                             uint64_t size,
                                             Error **errp)
{
    Error *err = NULL;
    assert(ops);
    memory_region_init(mr, owner, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->terminates = true;
    mr->rom_device = true;
    mr->destructor = memory_region_destructor_ram;
    mr->ram_block = qemu_ram_alloc(size, 0, mr, &err);
    if (err) {
        mr->size = int128_zero();
        object_unparent(OBJECT(mr));
        error_propagate(errp, err);
    }
}

void memory_region_init_iommu(void *_iommu_mr,
                              size_t instance_size,
                              const char *mrtypename,
                              Object *owner,
                              const char *name,
                              uint64_t size)
{
    struct IOMMUMemoryRegion *iommu_mr;
    struct MemoryRegion *mr;

    object_initialize(_iommu_mr, instance_size, mrtypename);
    mr = MEMORY_REGION(_iommu_mr);
    memory_region_do_init(mr, owner, name, size);
    iommu_mr = IOMMU_MEMORY_REGION(mr);
    mr->terminates = true;  /* then re-forwards */
    QLIST_INIT(&iommu_mr->iommu_notify);
    iommu_mr->iommu_notify_flags = IOMMU_NOTIFIER_NONE;
}

static void memory_region_finalize(Object *obj)
{
    MemoryRegion *mr = MEMORY_REGION(obj);

    assert(!mr->container);

    /* We know the region is not visible in any address space (it
     * does not have a container and cannot be a root either because
     * it has no references, so we can blindly clear mr->enabled.
     * memory_region_set_enabled instead could trigger a transaction
     * and cause an infinite loop.
     */
    mr->enabled = false;
    memory_region_transaction_begin();
    while (!QTAILQ_EMPTY(&mr->subregions)) {
        MemoryRegion *subregion = QTAILQ_FIRST(&mr->subregions);
        memory_region_del_subregion(mr, subregion);
    }
    memory_region_transaction_commit();

    mr->destructor(mr);
    memory_region_clear_coalescing(mr);
    g_free((char *)mr->name);
    g_free(mr->ioeventfds);
}

Object *memory_region_owner(MemoryRegion *mr)
{
    Object *obj = OBJECT(mr);
    return obj->parent;
}

void memory_region_ref(MemoryRegion *mr)
{
    /* MMIO callbacks most likely will access data that belongs
     * to the owner, hence the need to ref/unref the owner whenever
     * the memory region is in use.
     *
     * The memory region is a child of its owner.  As long as the
     * owner doesn't call unparent itself on the memory region,
     * ref-ing the owner will also keep the memory region alive.
     * Memory regions without an owner are supposed to never go away;
     * we do not ref/unref them because it slows down DMA sensibly.
     */
    if (mr && mr->owner) {
        object_ref(mr->owner);
    }
}

void memory_region_unref(MemoryRegion *mr)
{
    if (mr && mr->owner) {
        object_unref(mr->owner);
    }
}

uint64_t memory_region_size(MemoryRegion *mr)
{
    if (int128_eq(mr->size, int128_2_64())) {
        return UINT64_MAX;
    }
    return int128_get64(mr->size);
}

const char *memory_region_name(const MemoryRegion *mr)
{
    if (!mr->name) {
        ((MemoryRegion *)mr)->name =
            g_strdup(object_get_canonical_path_component(OBJECT(mr)));
    }
    return mr->name;
}

bool memory_region_is_ram_device(MemoryRegion *mr)
{
    return mr->ram_device;
}

bool memory_region_is_protected(MemoryRegion *mr)
{
    return mr->ram && (mr->ram_block->flags & RAM_PROTECTED);
}

uint8_t memory_region_get_dirty_log_mask(MemoryRegion *mr)
{
    uint8_t mask = mr->dirty_log_mask;
    RAMBlock *rb = mr->ram_block;

    if (global_dirty_tracking && ((rb && qemu_ram_is_migratable(rb)) ||
                             memory_region_is_iommu(mr))) {
        mask |= (1 << DIRTY_MEMORY_MIGRATION);
    }

    if (tcg_enabled() && rb) {
        /* TCG only cares about dirty memory logging for RAM, not IOMMU.  */
        mask |= (1 << DIRTY_MEMORY_CODE);
    }
    return mask;
}

bool memory_region_is_logging(MemoryRegion *mr, uint8_t client)
{
    return memory_region_get_dirty_log_mask(mr) & (1 << client);
}

static int memory_region_update_iommu_notify_flags(IOMMUMemoryRegion *iommu_mr,
                                                   Error **errp)
{
    IOMMUNotifierFlag flags = IOMMU_NOTIFIER_NONE;
    IOMMUNotifier *iommu_notifier;
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);
    int ret = 0;

    IOMMU_NOTIFIER_FOREACH(iommu_notifier, iommu_mr) {
        flags |= iommu_notifier->notifier_flags;
    }

    if (flags != iommu_mr->iommu_notify_flags && imrc->notify_flag_changed) {
        ret = imrc->notify_flag_changed(iommu_mr,
                                        iommu_mr->iommu_notify_flags,
                                        flags, errp);
    }

    if (!ret) {
        iommu_mr->iommu_notify_flags = flags;
    }
    return ret;
}

int memory_region_iommu_set_page_size_mask(IOMMUMemoryRegion *iommu_mr,
                                           uint64_t page_size_mask,
                                           Error **errp)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);
    int ret = 0;

    if (imrc->iommu_set_page_size_mask) {
        ret = imrc->iommu_set_page_size_mask(iommu_mr, page_size_mask, errp);
    }
    return ret;
}

int memory_region_register_iommu_notifier(MemoryRegion *mr,
                                          IOMMUNotifier *n, Error **errp)
{
    IOMMUMemoryRegion *iommu_mr;
    int ret;

    if (mr->alias) {
        return memory_region_register_iommu_notifier(mr->alias, n, errp);
    }

    /* We need to register for at least one bitfield */
    iommu_mr = IOMMU_MEMORY_REGION(mr);
    assert(n->notifier_flags != IOMMU_NOTIFIER_NONE);
    assert(n->start <= n->end);
    assert(n->iommu_idx >= 0 &&
           n->iommu_idx < memory_region_iommu_num_indexes(iommu_mr));

    QLIST_INSERT_HEAD(&iommu_mr->iommu_notify, n, node);
    ret = memory_region_update_iommu_notify_flags(iommu_mr, errp);
    if (ret) {
        QLIST_REMOVE(n, node);
    }
    return ret;
}

uint64_t memory_region_iommu_get_min_page_size(IOMMUMemoryRegion *iommu_mr)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);

    if (imrc->get_min_page_size) {
        return imrc->get_min_page_size(iommu_mr);
    }
    return TARGET_PAGE_SIZE;
}

void memory_region_iommu_replay(IOMMUMemoryRegion *iommu_mr, IOMMUNotifier *n)
{
    MemoryRegion *mr = MEMORY_REGION(iommu_mr);
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);
    hwaddr addr, granularity;
    IOMMUTLBEntry iotlb;

    /* If the IOMMU has its own replay callback, override */
    if (imrc->replay) {
        imrc->replay(iommu_mr, n);
        return;
    }

    granularity = memory_region_iommu_get_min_page_size(iommu_mr);

    for (addr = 0; addr < memory_region_size(mr); addr += granularity) {
        iotlb = imrc->translate(iommu_mr, addr, IOMMU_NONE, n->iommu_idx);
        if (iotlb.perm != IOMMU_NONE) {
            n->notify(n, &iotlb);
        }

        /* if (2^64 - MR size) < granularity, it's possible to get an
         * infinite loop here.  This should catch such a wraparound */
        if ((addr + granularity) < addr) {
            break;
        }
    }
}

void memory_region_unregister_iommu_notifier(MemoryRegion *mr,
                                             IOMMUNotifier *n)
{
    IOMMUMemoryRegion *iommu_mr;

    if (mr->alias) {
        memory_region_unregister_iommu_notifier(mr->alias, n);
        return;
    }
    QLIST_REMOVE(n, node);
    iommu_mr = IOMMU_MEMORY_REGION(mr);
    memory_region_update_iommu_notify_flags(iommu_mr, NULL);
}

void memory_region_notify_iommu_one(IOMMUNotifier *notifier,
                                    IOMMUTLBEvent *event)
{
    IOMMUTLBEntry *entry = &event->entry;
    hwaddr entry_end = entry->iova + entry->addr_mask;
    IOMMUTLBEntry tmp = *entry;

    if (event->type == IOMMU_NOTIFIER_UNMAP) {
        assert(entry->perm == IOMMU_NONE);
    }

    /*
     * Skip the notification if the notification does not overlap
     * with registered range.
     */
    if (notifier->start > entry_end || notifier->end < entry->iova) {
        return;
    }

    if (notifier->notifier_flags & IOMMU_NOTIFIER_DEVIOTLB_UNMAP) {
        /* Crop (iova, addr_mask) to range */
        tmp.iova = MAX(tmp.iova, notifier->start);
        tmp.addr_mask = MIN(entry_end, notifier->end) - tmp.iova;
    } else {
        assert(entry->iova >= notifier->start && entry_end <= notifier->end);
    }

    if (event->type & notifier->notifier_flags) {
        notifier->notify(notifier, &tmp);
    }
}

void memory_region_unmap_iommu_notifier_range(IOMMUNotifier *notifier)
{
    IOMMUTLBEvent event;

    event.type = IOMMU_NOTIFIER_UNMAP;
    event.entry.target_as = &address_space_memory;
    event.entry.iova = notifier->start;
    event.entry.perm = IOMMU_NONE;
    event.entry.addr_mask = notifier->end - notifier->start;

    memory_region_notify_iommu_one(notifier, &event);
}

void memory_region_notify_iommu(IOMMUMemoryRegion *iommu_mr,
                                int iommu_idx,
                                IOMMUTLBEvent event)
{
    IOMMUNotifier *iommu_notifier;

    assert(memory_region_is_iommu(MEMORY_REGION(iommu_mr)));

    IOMMU_NOTIFIER_FOREACH(iommu_notifier, iommu_mr) {
        if (iommu_notifier->iommu_idx == iommu_idx) {
            memory_region_notify_iommu_one(iommu_notifier, &event);
        }
    }
}

int memory_region_iommu_get_attr(IOMMUMemoryRegion *iommu_mr,
                                 enum IOMMUMemoryRegionAttr attr,
                                 void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);

    if (!imrc->get_attr) {
        return -EINVAL;
    }

    return imrc->get_attr(iommu_mr, attr, data);
}

int memory_region_iommu_attrs_to_index(IOMMUMemoryRegion *iommu_mr,
                                       MemTxAttrs attrs)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);

    if (!imrc->attrs_to_index) {
        return 0;
    }

    return imrc->attrs_to_index(iommu_mr, attrs);
}

int memory_region_iommu_num_indexes(IOMMUMemoryRegion *iommu_mr)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_GET_CLASS(iommu_mr);

    if (!imrc->num_indexes) {
        return 1;
    }

    return imrc->num_indexes(iommu_mr);
}

RamDiscardManager *memory_region_get_ram_discard_manager(MemoryRegion *mr)
{
    if (!memory_region_is_mapped(mr) || !memory_region_is_ram(mr)) {
        return NULL;
    }
    return mr->rdm;
}

void memory_region_set_ram_discard_manager(MemoryRegion *mr,
                                           RamDiscardManager *rdm)
{
    g_assert(memory_region_is_ram(mr) && !memory_region_is_mapped(mr));
    g_assert(!rdm || !mr->rdm);
    mr->rdm = rdm;
}

uint64_t ram_discard_manager_get_min_granularity(const RamDiscardManager *rdm,
                                                 const MemoryRegion *mr)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_GET_CLASS(rdm);

    g_assert(rdmc->get_min_granularity);
    return rdmc->get_min_granularity(rdm, mr);
}

bool ram_discard_manager_is_populated(const RamDiscardManager *rdm,
                                      const MemoryRegionSection *section)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_GET_CLASS(rdm);

    g_assert(rdmc->is_populated);
    return rdmc->is_populated(rdm, section);
}

int ram_discard_manager_replay_populated(const RamDiscardManager *rdm,
                                         MemoryRegionSection *section,
                                         ReplayRamPopulate replay_fn,
                                         void *opaque)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_GET_CLASS(rdm);

    g_assert(rdmc->replay_populated);
    return rdmc->replay_populated(rdm, section, replay_fn, opaque);
}

void ram_discard_manager_replay_discarded(const RamDiscardManager *rdm,
                                          MemoryRegionSection *section,
                                          ReplayRamDiscard replay_fn,
                                          void *opaque)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_GET_CLASS(rdm);

    g_assert(rdmc->replay_discarded);
    rdmc->replay_discarded(rdm, section, replay_fn, opaque);
}

void ram_discard_manager_register_listener(RamDiscardManager *rdm,
                                           RamDiscardListener *rdl,
                                           MemoryRegionSection *section)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_GET_CLASS(rdm);

    g_assert(rdmc->register_listener);
    rdmc->register_listener(rdm, rdl, section);
}

void ram_discard_manager_unregister_listener(RamDiscardManager *rdm,
                                             RamDiscardListener *rdl)
{
    RamDiscardManagerClass *rdmc = RAM_DISCARD_MANAGER_GET_CLASS(rdm);

    g_assert(rdmc->unregister_listener);
    rdmc->unregister_listener(rdm, rdl);
}

/* Called with rcu_read_lock held.  */
bool memory_get_xlat_addr(IOMMUTLBEntry *iotlb, void **vaddr,
                          ram_addr_t *ram_addr, bool *read_only,
                          bool *mr_has_discard_manager)
{
    MemoryRegion *mr;
    hwaddr xlat;
    hwaddr len = iotlb->addr_mask + 1;
    bool writable = iotlb->perm & IOMMU_WO;

    if (mr_has_discard_manager) {
        *mr_has_discard_manager = false;
    }
    /*
     * The IOMMU TLB entry we have just covers translation through
     * this IOMMU to its immediate target.  We need to translate
     * it the rest of the way through to memory.
     */
    mr = address_space_translate(&address_space_memory, iotlb->translated_addr,
                                 &xlat, &len, writable, MEMTXATTRS_UNSPECIFIED);
    if (!memory_region_is_ram(mr)) {
        error_report("iommu map to non memory area %" HWADDR_PRIx "", xlat);
        return false;
    } else if (memory_region_has_ram_discard_manager(mr)) {
        RamDiscardManager *rdm = memory_region_get_ram_discard_manager(mr);
        MemoryRegionSection tmp = {
            .mr = mr,
            .offset_within_region = xlat,
            .size = int128_make64(len),
        };
        if (mr_has_discard_manager) {
            *mr_has_discard_manager = true;
        }
        /*
         * Malicious VMs can map memory into the IOMMU, which is expected
         * to remain discarded. vfio will pin all pages, populating memory.
         * Disallow that. vmstate priorities make sure any RamDiscardManager
         * were already restored before IOMMUs are restored.
         */
        if (!ram_discard_manager_is_populated(rdm, &tmp)) {
            error_report("iommu map to discarded memory (e.g., unplugged via"
                         " virtio-mem): %" HWADDR_PRIx "",
                         iotlb->translated_addr);
            return false;
        }
    }

    /*
     * Translation truncates length to the IOMMU page size,
     * check that it did not truncate too much.
     */
    if (len & iotlb->addr_mask) {
        error_report("iommu has granularity incompatible with target AS");
        return false;
    }

    if (vaddr) {
        *vaddr = memory_region_get_ram_ptr(mr) + xlat;
    }

    if (ram_addr) {
        *ram_addr = memory_region_get_ram_addr(mr) + xlat;
    }

    if (read_only) {
        *read_only = !writable || mr->readonly;
    }

    return true;
}

void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client)
{
    uint8_t mask = 1 << client;
    uint8_t old_logging;

    assert(client == DIRTY_MEMORY_VGA);
    old_logging = mr->vga_logging_count;
    mr->vga_logging_count += log ? 1 : -1;
    if (!!old_logging == !!mr->vga_logging_count) {
        return;
    }

    memory_region_transaction_begin();
    mr->dirty_log_mask = (mr->dirty_log_mask & ~mask) | (log * mask);
    memory_region_update_pending |= mr->enabled;
    memory_region_transaction_commit();
}

void memory_region_set_dirty(MemoryRegion *mr, hwaddr addr,
                             hwaddr size)
{
    assert(mr->ram_block);
    cpu_physical_memory_set_dirty_range(memory_region_get_ram_addr(mr) + addr,
                                        size,
                                        memory_region_get_dirty_log_mask(mr));
}

/*
 * If memory region `mr' is NULL, do global sync.  Otherwise, sync
 * dirty bitmap for the specified memory region.
 */
static void memory_region_sync_dirty_bitmap(MemoryRegion *mr)
{
    MemoryListener *listener;
    AddressSpace *as;
    FlatView *view;
    FlatRange *fr;

    /* If the same address space has multiple log_sync listeners, we
     * visit that address space's FlatView multiple times.  But because
     * log_sync listeners are rare, it's still cheaper than walking each
     * address space once.
     */
    QTAILQ_FOREACH(listener, &memory_listeners, link) {
        if (listener->log_sync) {
            as = listener->address_space;
            view = address_space_get_flatview(as);
            FOR_EACH_FLAT_RANGE(fr, view) {
                if (fr->dirty_log_mask && (!mr || fr->mr == mr)) {
                    MemoryRegionSection mrs = section_from_flat_range(fr, view);
                    listener->log_sync(listener, &mrs);
                }
            }
            flatview_unref(view);
            trace_memory_region_sync_dirty(mr ? mr->name : "(all)", listener->name, 0);
        } else if (listener->log_sync_global) {
            /*
             * No matter whether MR is specified, what we can do here
             * is to do a global sync, because we are not capable to
             * sync in a finer granularity.
             */
            listener->log_sync_global(listener);
            trace_memory_region_sync_dirty(mr ? mr->name : "(all)", listener->name, 1);
        }
    }
}

void memory_region_clear_dirty_bitmap(MemoryRegion *mr, hwaddr start,
                                      hwaddr len)
{
    MemoryRegionSection mrs;
    MemoryListener *listener;
    AddressSpace *as;
    FlatView *view;
    FlatRange *fr;
    hwaddr sec_start, sec_end, sec_size;

    QTAILQ_FOREACH(listener, &memory_listeners, link) {
        if (!listener->log_clear) {
            continue;
        }
        as = listener->address_space;
        view = address_space_get_flatview(as);
        FOR_EACH_FLAT_RANGE(fr, view) {
            if (!fr->dirty_log_mask || fr->mr != mr) {
                /*
                 * Clear dirty bitmap operation only applies to those
                 * regions whose dirty logging is at least enabled
                 */
                continue;
            }

            mrs = section_from_flat_range(fr, view);

            sec_start = MAX(mrs.offset_within_region, start);
            sec_end = mrs.offset_within_region + int128_get64(mrs.size);
            sec_end = MIN(sec_end, start + len);

            if (sec_start >= sec_end) {
                /*
                 * If this memory region section has no intersection
                 * with the requested range, skip.
                 */
                continue;
            }

            /* Valid case; shrink the section if needed */
            mrs.offset_within_address_space +=
                sec_start - mrs.offset_within_region;
            mrs.offset_within_region = sec_start;
            sec_size = sec_end - sec_start;
            mrs.size = int128_make64(sec_size);
            listener->log_clear(listener, &mrs);
        }
        flatview_unref(view);
    }
}

DirtyBitmapSnapshot *memory_region_snapshot_and_clear_dirty(MemoryRegion *mr,
                                                            hwaddr addr,
                                                            hwaddr size,
                                                            unsigned client)
{
    DirtyBitmapSnapshot *snapshot;
    assert(mr->ram_block);
    memory_region_sync_dirty_bitmap(mr);
    snapshot = cpu_physical_memory_snapshot_and_clear_dirty(mr, addr, size, client);
    memory_global_after_dirty_log_sync();
    return snapshot;
}

bool memory_region_snapshot_get_dirty(MemoryRegion *mr, DirtyBitmapSnapshot *snap,
                                      hwaddr addr, hwaddr size)
{
    assert(mr->ram_block);
    return cpu_physical_memory_snapshot_get_dirty(snap,
                memory_region_get_ram_addr(mr) + addr, size);
}

void memory_region_set_readonly(MemoryRegion *mr, bool readonly)
{
    if (mr->readonly != readonly) {
        memory_region_transaction_begin();
        mr->readonly = readonly;
        memory_region_update_pending |= mr->enabled;
        memory_region_transaction_commit();
    }
}

void memory_region_set_nonvolatile(MemoryRegion *mr, bool nonvolatile)
{
    if (mr->nonvolatile != nonvolatile) {
        memory_region_transaction_begin();
        mr->nonvolatile = nonvolatile;
        memory_region_update_pending |= mr->enabled;
        memory_region_transaction_commit();
    }
}

void memory_region_rom_device_set_romd(MemoryRegion *mr, bool romd_mode)
{
    if (mr->romd_mode != romd_mode) {
        memory_region_transaction_begin();
        mr->romd_mode = romd_mode;
        memory_region_update_pending |= mr->enabled;
        memory_region_transaction_commit();
    }
}

void memory_region_reset_dirty(MemoryRegion *mr, hwaddr addr,
                               hwaddr size, unsigned client)
{
    assert(mr->ram_block);
    cpu_physical_memory_test_and_clear_dirty(
        memory_region_get_ram_addr(mr) + addr, size, client);
}

int memory_region_get_fd(MemoryRegion *mr)
{
    RCU_READ_LOCK_GUARD();
    while (mr->alias) {
        mr = mr->alias;
    }
    return mr->ram_block->fd;
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    uint64_t offset = 0;

    RCU_READ_LOCK_GUARD();
    while (mr->alias) {
        offset += mr->alias_offset;
        mr = mr->alias;
    }
    assert(mr->ram_block);
    return qemu_map_ram_ptr(mr->ram_block, offset);
}

MemoryRegion *memory_region_from_host(void *ptr, ram_addr_t *offset)
{
    RAMBlock *block;

    block = qemu_ram_block_from_host(ptr, false, offset);
    if (!block) {
        return NULL;
    }

    return block->mr;
}

ram_addr_t memory_region_get_ram_addr(MemoryRegion *mr)
{
    return mr->ram_block ? mr->ram_block->offset : RAM_ADDR_INVALID;
}

void memory_region_ram_resize(MemoryRegion *mr, ram_addr_t newsize, Error **errp)
{
    assert(mr->ram_block);

    qemu_ram_resize(mr->ram_block, newsize, errp);
}

void memory_region_msync(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
    if (mr->ram_block) {
        qemu_ram_msync(mr->ram_block, addr, size);
    }
}

void memory_region_writeback(MemoryRegion *mr, hwaddr addr, hwaddr size)
{
    /*
     * Might be extended case needed to cover
     * different types of memory regions
     */
    if (mr->dirty_log_mask) {
        memory_region_msync(mr, addr, size);
    }
}

/*
 * Call proper memory listeners about the change on the newly
 * added/removed CoalescedMemoryRange.
 */
static void memory_region_update_coalesced_range(MemoryRegion *mr,
                                                 CoalescedMemoryRange *cmr,
                                                 bool add)
{
    AddressSpace *as;
    FlatView *view;
    FlatRange *fr;

    QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
        view = address_space_get_flatview(as);
        FOR_EACH_FLAT_RANGE(fr, view) {
            if (fr->mr == mr) {
                flat_range_coalesced_io_notify(fr, as, cmr, add);
            }
        }
        flatview_unref(view);
    }
}

void memory_region_set_coalescing(MemoryRegion *mr)
{
    memory_region_clear_coalescing(mr);
    memory_region_add_coalescing(mr, 0, int128_get64(mr->size));
}

void memory_region_add_coalescing(MemoryRegion *mr,
                                  hwaddr offset,
                                  uint64_t size)
{
    CoalescedMemoryRange *cmr = g_malloc(sizeof(*cmr));

    cmr->addr = addrrange_make(int128_make64(offset), int128_make64(size));
    QTAILQ_INSERT_TAIL(&mr->coalesced, cmr, link);
    memory_region_update_coalesced_range(mr, cmr, true);
    memory_region_set_flush_coalesced(mr);
}

void memory_region_clear_coalescing(MemoryRegion *mr)
{
    CoalescedMemoryRange *cmr;

    if (QTAILQ_EMPTY(&mr->coalesced)) {
        return;
    }

    qemu_flush_coalesced_mmio_buffer();
    mr->flush_coalesced_mmio = false;

    while (!QTAILQ_EMPTY(&mr->coalesced)) {
        cmr = QTAILQ_FIRST(&mr->coalesced);
        QTAILQ_REMOVE(&mr->coalesced, cmr, link);
        memory_region_update_coalesced_range(mr, cmr, false);
        g_free(cmr);
    }
}

void memory_region_set_flush_coalesced(MemoryRegion *mr)
{
    mr->flush_coalesced_mmio = true;
}

void memory_region_clear_flush_coalesced(MemoryRegion *mr)
{
    qemu_flush_coalesced_mmio_buffer();
    if (QTAILQ_EMPTY(&mr->coalesced)) {
        mr->flush_coalesced_mmio = false;
    }
}

static bool userspace_eventfd_warning;

void memory_region_add_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = int128_make64(addr),
        .addr.size = int128_make64(size),
        .match_data = match_data,
        .data = data,
        .e = e,
    };
    unsigned i;

    if (kvm_enabled() && (!(kvm_eventfds_enabled() ||
                            userspace_eventfd_warning))) {
        userspace_eventfd_warning = true;
        error_report("Using eventfd without MMIO binding in KVM. "
                     "Suboptimal performance expected");
    }

    if (size) {
        adjust_endianness(mr, &mrfd.data, size_memop(size) | MO_TE);
    }
    memory_region_transaction_begin();
    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_before(&mrfd, &mr->ioeventfds[i])) {
            break;
        }
    }
    ++mr->ioeventfd_nb;
    mr->ioeventfds = g_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds) * mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i+1], &mr->ioeventfds[i],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb-1 - i));
    mr->ioeventfds[i] = mrfd;
    ioeventfd_update_pending |= mr->enabled;
    memory_region_transaction_commit();
}

void memory_region_del_eventfd(MemoryRegion *mr,
                               hwaddr addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               EventNotifier *e)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = int128_make64(addr),
        .addr.size = int128_make64(size),
        .match_data = match_data,
        .data = data,
        .e = e,
    };
    unsigned i;

    if (size) {
        adjust_endianness(mr, &mrfd.data, size_memop(size) | MO_TE);
    }
    memory_region_transaction_begin();
    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_equal(&mrfd, &mr->ioeventfds[i])) {
            break;
        }
    }
    assert(i != mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i], &mr->ioeventfds[i+1],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb - (i+1)));
    --mr->ioeventfd_nb;
    mr->ioeventfds = g_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds)*mr->ioeventfd_nb + 1);
    ioeventfd_update_pending |= mr->enabled;
    memory_region_transaction_commit();
}

static void memory_region_update_container_subregions(MemoryRegion *subregion)
{
    MemoryRegion *mr = subregion->container;
    MemoryRegion *other;

    memory_region_transaction_begin();

    memory_region_ref(subregion);
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_pending |= mr->enabled && subregion->enabled;
    memory_region_transaction_commit();
}

static void memory_region_add_subregion_common(MemoryRegion *mr,
                                               hwaddr offset,
                                               MemoryRegion *subregion)
{
    MemoryRegion *alias;

    assert(!subregion->container);
    subregion->container = mr;
    for (alias = subregion->alias; alias; alias = alias->alias) {
        alias->mapped_via_alias++;
    }
    subregion->addr = offset;
    memory_region_update_container_subregions(subregion);
}

void memory_region_add_subregion(MemoryRegion *mr,
                                 hwaddr offset,
                                 MemoryRegion *subregion)
{
    subregion->priority = 0;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         hwaddr offset,
                                         MemoryRegion *subregion,
                                         int priority)
{
    subregion->priority = priority;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion)
{
    MemoryRegion *alias;

    memory_region_transaction_begin();
    assert(subregion->container == mr);
    subregion->container = NULL;
    for (alias = subregion->alias; alias; alias = alias->alias) {
        alias->mapped_via_alias--;
        assert(alias->mapped_via_alias >= 0);
    }
    QTAILQ_REMOVE(&mr->subregions, subregion, subregions_link);
    memory_region_unref(subregion);
    memory_region_update_pending |= mr->enabled && subregion->enabled;
    memory_region_transaction_commit();
}

void memory_region_set_enabled(MemoryRegion *mr, bool enabled)
{
    if (enabled == mr->enabled) {
        return;
    }
    memory_region_transaction_begin();
    mr->enabled = enabled;
    memory_region_update_pending = true;
    memory_region_transaction_commit();
}

void memory_region_set_size(MemoryRegion *mr, uint64_t size)
{
    Int128 s = int128_make64(size);

    if (size == UINT64_MAX) {
        s = int128_2_64();
    }
    if (int128_eq(s, mr->size)) {
        return;
    }
    memory_region_transaction_begin();
    mr->size = s;
    memory_region_update_pending = true;
    memory_region_transaction_commit();
}

static void memory_region_readd_subregion(MemoryRegion *mr)
{
    MemoryRegion *container = mr->container;

    if (container) {
        memory_region_transaction_begin();
        memory_region_ref(mr);
        memory_region_del_subregion(container, mr);
        memory_region_add_subregion_common(container, mr->addr, mr);
        memory_region_unref(mr);
        memory_region_transaction_commit();
    }
}

void memory_region_set_address(MemoryRegion *mr, hwaddr addr)
{
    if (addr != mr->addr) {
        mr->addr = addr;
        memory_region_readd_subregion(mr);
    }
}

void memory_region_set_alias_offset(MemoryRegion *mr, hwaddr offset)
{
    assert(mr->alias);

    if (offset == mr->alias_offset) {
        return;
    }

    memory_region_transaction_begin();
    mr->alias_offset = offset;
    memory_region_update_pending |= mr->enabled;
    memory_region_transaction_commit();
}

uint64_t memory_region_get_alignment(const MemoryRegion *mr)
{
    return mr->align;
}

static int cmp_flatrange_addr(const void *addr_, const void *fr_)
{
    const AddrRange *addr = addr_;
    const FlatRange *fr = fr_;

    if (int128_le(addrrange_end(*addr), fr->addr.start)) {
        return -1;
    } else if (int128_ge(addr->start, addrrange_end(fr->addr))) {
        return 1;
    }
    return 0;
}

static FlatRange *flatview_lookup(FlatView *view, AddrRange addr)
{
    return bsearch(&addr, view->ranges, view->nr,
                   sizeof(FlatRange), cmp_flatrange_addr);
}

bool memory_region_is_mapped(MemoryRegion *mr)
{
    return !!mr->container || mr->mapped_via_alias;
}

/* Same as memory_region_find, but it does not add a reference to the
 * returned region.  It must be called from an RCU critical section.
 */
static MemoryRegionSection memory_region_find_rcu(MemoryRegion *mr,
                                                  hwaddr addr, uint64_t size)
{
    MemoryRegionSection ret = { .mr = NULL };
    MemoryRegion *root;
    AddressSpace *as;
    AddrRange range;
    FlatView *view;
    FlatRange *fr;

    addr += mr->addr;
    for (root = mr; root->container; ) {
        root = root->container;
        addr += root->addr;
    }

    as = memory_region_to_address_space(root);
    if (!as) {
        return ret;
    }
    range = addrrange_make(int128_make64(addr), int128_make64(size));

    view = address_space_to_flatview(as);
    fr = flatview_lookup(view, range);
    if (!fr) {
        return ret;
    }

    while (fr > view->ranges && addrrange_intersects(fr[-1].addr, range)) {
        --fr;
    }

    ret.mr = fr->mr;
    ret.fv = view;
    range = addrrange_intersection(range, fr->addr);
    ret.offset_within_region = fr->offset_in_region;
    ret.offset_within_region += int128_get64(int128_sub(range.start,
                                                        fr->addr.start));
    ret.size = range.size;
    ret.offset_within_address_space = int128_get64(range.start);
    ret.readonly = fr->readonly;
    ret.nonvolatile = fr->nonvolatile;
    return ret;
}

MemoryRegionSection memory_region_find(MemoryRegion *mr,
                                       hwaddr addr, uint64_t size)
{
    MemoryRegionSection ret;
    RCU_READ_LOCK_GUARD();
    ret = memory_region_find_rcu(mr, addr, size);
    if (ret.mr) {
        memory_region_ref(ret.mr);
    }
    return ret;
}

MemoryRegionSection *memory_region_section_new_copy(MemoryRegionSection *s)
{
    MemoryRegionSection *tmp = g_new(MemoryRegionSection, 1);

    *tmp = *s;
    if (tmp->mr) {
        memory_region_ref(tmp->mr);
    }
    if (tmp->fv) {
        bool ret  = flatview_ref(tmp->fv);

        g_assert(ret);
    }
    return tmp;
}

void memory_region_section_free_copy(MemoryRegionSection *s)
{
    if (s->fv) {
        flatview_unref(s->fv);
    }
    if (s->mr) {
        memory_region_unref(s->mr);
    }
    g_free(s);
}

bool memory_region_present(MemoryRegion *container, hwaddr addr)
{
    MemoryRegion *mr;

    RCU_READ_LOCK_GUARD();
    mr = memory_region_find_rcu(container, addr, 1).mr;
    return mr && mr != container;
}

void memory_global_dirty_log_sync(void)
{
    memory_region_sync_dirty_bitmap(NULL);
}

void memory_global_after_dirty_log_sync(void)
{
    MEMORY_LISTENER_CALL_GLOBAL(log_global_after_sync, Forward);
}

/*
 * Dirty track stop flags that are postponed due to VM being stopped.  Should
 * only be used within vmstate_change hook.
 */
static unsigned int postponed_stop_flags;
static VMChangeStateEntry *vmstate_change;
static void memory_global_dirty_log_stop_postponed_run(void);

void memory_global_dirty_log_start(unsigned int flags)
{
    unsigned int old_flags;

    assert(flags && !(flags & (~GLOBAL_DIRTY_MASK)));

    if (vmstate_change) {
        /* If there is postponed stop(), operate on it first */
        postponed_stop_flags &= ~flags;
        memory_global_dirty_log_stop_postponed_run();
    }

    flags &= ~global_dirty_tracking;
    if (!flags) {
        return;
    }

    old_flags = global_dirty_tracking;
    global_dirty_tracking |= flags;
    trace_global_dirty_changed(global_dirty_tracking);

    if (!old_flags) {
        MEMORY_LISTENER_CALL_GLOBAL(log_global_start, Forward);
        memory_region_transaction_begin();
        memory_region_update_pending = true;
        memory_region_transaction_commit();
    }
}

static void memory_global_dirty_log_do_stop(unsigned int flags)
{
    assert(flags && !(flags & (~GLOBAL_DIRTY_MASK)));
    assert((global_dirty_tracking & flags) == flags);
    global_dirty_tracking &= ~flags;

    trace_global_dirty_changed(global_dirty_tracking);

    if (!global_dirty_tracking) {
        memory_region_transaction_begin();
        memory_region_update_pending = true;
        memory_region_transaction_commit();
        MEMORY_LISTENER_CALL_GLOBAL(log_global_stop, Reverse);
    }
}

/*
 * Execute the postponed dirty log stop operations if there is, then reset
 * everything (including the flags and the vmstate change hook).
 */
static void memory_global_dirty_log_stop_postponed_run(void)
{
    /* This must be called with the vmstate handler registered */
    assert(vmstate_change);

    /* Note: postponed_stop_flags can be cleared in log start routine */
    if (postponed_stop_flags) {
        memory_global_dirty_log_do_stop(postponed_stop_flags);
        postponed_stop_flags = 0;
    }

    qemu_del_vm_change_state_handler(vmstate_change);
    vmstate_change = NULL;
}

static void memory_vm_change_state_handler(void *opaque, bool running,
                                           RunState state)
{
    if (running) {
        memory_global_dirty_log_stop_postponed_run();
    }
}

void memory_global_dirty_log_stop(unsigned int flags)
{
    if (!runstate_is_running()) {
        /* Postpone the dirty log stop, e.g., to when VM starts again */
        if (vmstate_change) {
            /* Batch with previous postponed flags */
            postponed_stop_flags |= flags;
        } else {
            postponed_stop_flags = flags;
            vmstate_change = qemu_add_vm_change_state_handler(
                memory_vm_change_state_handler, NULL);
        }
        return;
    }

    memory_global_dirty_log_do_stop(flags);
}

static void listener_add_address_space(MemoryListener *listener,
                                       AddressSpace *as)
{
    FlatView *view;
    FlatRange *fr;

    if (listener->begin) {
        listener->begin(listener);
    }
    if (global_dirty_tracking) {
        if (listener->log_global_start) {
            listener->log_global_start(listener);
        }
    }

    view = address_space_get_flatview(as);
    FOR_EACH_FLAT_RANGE(fr, view) {
        MemoryRegionSection section = section_from_flat_range(fr, view);

        if (listener->region_add) {
            listener->region_add(listener, &section);
        }
        if (fr->dirty_log_mask && listener->log_start) {
            listener->log_start(listener, &section, 0, fr->dirty_log_mask);
        }
    }
    if (listener->commit) {
        listener->commit(listener);
    }
    flatview_unref(view);
}

static void listener_del_address_space(MemoryListener *listener,
                                       AddressSpace *as)
{
    FlatView *view;
    FlatRange *fr;

    if (listener->begin) {
        listener->begin(listener);
    }
    view = address_space_get_flatview(as);
    FOR_EACH_FLAT_RANGE(fr, view) {
        MemoryRegionSection section = section_from_flat_range(fr, view);

        if (fr->dirty_log_mask && listener->log_stop) {
            listener->log_stop(listener, &section, fr->dirty_log_mask, 0);
        }
        if (listener->region_del) {
            listener->region_del(listener, &section);
        }
    }
    if (listener->commit) {
        listener->commit(listener);
    }
    flatview_unref(view);
}

void memory_listener_register(MemoryListener *listener, AddressSpace *as)
{
    MemoryListener *other = NULL;

    /* Only one of them can be defined for a listener */
    assert(!(listener->log_sync && listener->log_sync_global));

    listener->address_space = as;
    if (QTAILQ_EMPTY(&memory_listeners)
        || listener->priority >= QTAILQ_LAST(&memory_listeners)->priority) {
        QTAILQ_INSERT_TAIL(&memory_listeners, listener, link);
    } else {
        QTAILQ_FOREACH(other, &memory_listeners, link) {
            if (listener->priority < other->priority) {
                break;
            }
        }
        QTAILQ_INSERT_BEFORE(other, listener, link);
    }

    if (QTAILQ_EMPTY(&as->listeners)
        || listener->priority >= QTAILQ_LAST(&as->listeners)->priority) {
        QTAILQ_INSERT_TAIL(&as->listeners, listener, link_as);
    } else {
        QTAILQ_FOREACH(other, &as->listeners, link_as) {
            if (listener->priority < other->priority) {
                break;
            }
        }
        QTAILQ_INSERT_BEFORE(other, listener, link_as);
    }

    listener_add_address_space(listener, as);
}

void memory_listener_unregister(MemoryListener *listener)
{
    if (!listener->address_space) {
        return;
    }

    listener_del_address_space(listener, listener->address_space);
    QTAILQ_REMOVE(&memory_listeners, listener, link);
    QTAILQ_REMOVE(&listener->address_space->listeners, listener, link_as);
    listener->address_space = NULL;
}

void address_space_remove_listeners(AddressSpace *as)
{
    while (!QTAILQ_EMPTY(&as->listeners)) {
        memory_listener_unregister(QTAILQ_FIRST(&as->listeners));
    }
}

void address_space_init(AddressSpace *as, MemoryRegion *root, const char *name)
{
    memory_region_ref(root);
    as->root = root;
    as->current_map = NULL;
    as->ioeventfd_nb = 0;
    as->ioeventfds = NULL;
    QTAILQ_INIT(&as->listeners);
    QTAILQ_INSERT_TAIL(&address_spaces, as, address_spaces_link);
    as->name = g_strdup(name ? name : "anonymous");
    address_space_update_topology(as);
    address_space_update_ioeventfds(as);
}

static void do_address_space_destroy(AddressSpace *as)
{
    assert(QTAILQ_EMPTY(&as->listeners));

    flatview_unref(as->current_map);
    g_free(as->name);
    g_free(as->ioeventfds);
    memory_region_unref(as->root);
}

void address_space_destroy(AddressSpace *as)
{
    MemoryRegion *root = as->root;

    /* Flush out anything from MemoryListeners listening in on this */
    memory_region_transaction_begin();
    as->root = NULL;
    memory_region_transaction_commit();
    QTAILQ_REMOVE(&address_spaces, as, address_spaces_link);

    /* At this point, as->dispatch and as->current_map are dummy
     * entries that the guest should never use.  Wait for the old
     * values to expire before freeing the data.
     */
    as->root = root;
    call_rcu(as, do_address_space_destroy, rcu);
}

static const char *memory_region_type(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_type(mr->alias);
    }
    if (memory_region_is_ram_device(mr)) {
        return "ramd";
    } else if (memory_region_is_romd(mr)) {
        return "romd";
    } else if (memory_region_is_rom(mr)) {
        return "rom";
    } else if (memory_region_is_ram(mr)) {
        return "ram";
    } else {
        return "i/o";
    }
}

typedef struct MemoryRegionList MemoryRegionList;

struct MemoryRegionList {
    const MemoryRegion *mr;
    QTAILQ_ENTRY(MemoryRegionList) mrqueue;
};

typedef QTAILQ_HEAD(, MemoryRegionList) MemoryRegionListHead;

#define MR_SIZE(size) (int128_nz(size) ? (hwaddr)int128_get64( \
                           int128_sub((size), int128_one())) : 0)
#define MTREE_INDENT "  "

static void mtree_expand_owner(const char *label, Object *obj)
{
    DeviceState *dev = (DeviceState *) object_dynamic_cast(obj, TYPE_DEVICE);

    qemu_printf(" %s:{%s", label, dev ? "dev" : "obj");
    if (dev && dev->id) {
        qemu_printf(" id=%s", dev->id);
    } else {
        char *canonical_path = object_get_canonical_path(obj);
        if (canonical_path) {
            qemu_printf(" path=%s", canonical_path);
            g_free(canonical_path);
        } else {
            qemu_printf(" type=%s", object_get_typename(obj));
        }
    }
    qemu_printf("}");
}

static void mtree_print_mr_owner(const MemoryRegion *mr)
{
    Object *owner = mr->owner;
    Object *parent = memory_region_owner((MemoryRegion *)mr);

    if (!owner && !parent) {
        qemu_printf(" orphan");
        return;
    }
    if (owner) {
        mtree_expand_owner("owner", owner);
    }
    if (parent && parent != owner) {
        mtree_expand_owner("parent", parent);
    }
}

static void mtree_print_mr(const MemoryRegion *mr, unsigned int level,
                           hwaddr base,
                           MemoryRegionListHead *alias_print_queue,
                           bool owner, bool display_disabled)
{
    MemoryRegionList *new_ml, *ml, *next_ml;
    MemoryRegionListHead submr_print_queue;
    const MemoryRegion *submr;
    unsigned int i;
    hwaddr cur_start, cur_end;

    if (!mr) {
        return;
    }

    cur_start = base + mr->addr;
    cur_end = cur_start + MR_SIZE(mr->size);

    /*
     * Try to detect overflow of memory region. This should never
     * happen normally. When it happens, we dump something to warn the
     * user who is observing this.
     */
    if (cur_start < base || cur_end < cur_start) {
        qemu_printf("[DETECTED OVERFLOW!] ");
    }

    if (mr->alias) {
        MemoryRegionList *ml;
        bool found = false;

        /* check if the alias is already in the queue */
        QTAILQ_FOREACH(ml, alias_print_queue, mrqueue) {
            if (ml->mr == mr->alias) {
                found = true;
            }
        }

        if (!found) {
            ml = g_new(MemoryRegionList, 1);
            ml->mr = mr->alias;
            QTAILQ_INSERT_TAIL(alias_print_queue, ml, mrqueue);
        }
        if (mr->enabled || display_disabled) {
            for (i = 0; i < level; i++) {
                qemu_printf(MTREE_INDENT);
            }
            qemu_printf(HWADDR_FMT_plx "-" HWADDR_FMT_plx
                        " (prio %d, %s%s): alias %s @%s " HWADDR_FMT_plx
                        "-" HWADDR_FMT_plx "%s",
                        cur_start, cur_end,
                        mr->priority,
                        mr->nonvolatile ? "nv-" : "",
                        memory_region_type((MemoryRegion *)mr),
                        memory_region_name(mr),
                        memory_region_name(mr->alias),
                        mr->alias_offset,
                        mr->alias_offset + MR_SIZE(mr->size),
                        mr->enabled ? "" : " [disabled]");
            if (owner) {
                mtree_print_mr_owner(mr);
            }
            qemu_printf("\n");
        }
    } else {
        if (mr->enabled || display_disabled) {
            for (i = 0; i < level; i++) {
                qemu_printf(MTREE_INDENT);
            }
            qemu_printf(HWADDR_FMT_plx "-" HWADDR_FMT_plx
                        " (prio %d, %s%s): %s%s",
                        cur_start, cur_end,
                        mr->priority,
                        mr->nonvolatile ? "nv-" : "",
                        memory_region_type((MemoryRegion *)mr),
                        memory_region_name(mr),
                        mr->enabled ? "" : " [disabled]");
            if (owner) {
                mtree_print_mr_owner(mr);
            }
            qemu_printf("\n");
        }
    }

    QTAILQ_INIT(&submr_print_queue);

    QTAILQ_FOREACH(submr, &mr->subregions, subregions_link) {
        new_ml = g_new(MemoryRegionList, 1);
        new_ml->mr = submr;
        QTAILQ_FOREACH(ml, &submr_print_queue, mrqueue) {
            if (new_ml->mr->addr < ml->mr->addr ||
                (new_ml->mr->addr == ml->mr->addr &&
                 new_ml->mr->priority > ml->mr->priority)) {
                QTAILQ_INSERT_BEFORE(ml, new_ml, mrqueue);
                new_ml = NULL;
                break;
            }
        }
        if (new_ml) {
            QTAILQ_INSERT_TAIL(&submr_print_queue, new_ml, mrqueue);
        }
    }

    QTAILQ_FOREACH(ml, &submr_print_queue, mrqueue) {
        mtree_print_mr(ml->mr, level + 1, cur_start,
                       alias_print_queue, owner, display_disabled);
    }

    QTAILQ_FOREACH_SAFE(ml, &submr_print_queue, mrqueue, next_ml) {
        g_free(ml);
    }
}

struct FlatViewInfo {
    int counter;
    bool dispatch_tree;
    bool owner;
    AccelClass *ac;
};

static void mtree_print_flatview(gpointer key, gpointer value,
                                 gpointer user_data)
{
    FlatView *view = key;
    GArray *fv_address_spaces = value;
    struct FlatViewInfo *fvi = user_data;
    FlatRange *range = &view->ranges[0];
    MemoryRegion *mr;
    int n = view->nr;
    int i;
    AddressSpace *as;

    qemu_printf("FlatView #%d\n", fvi->counter);
    ++fvi->counter;

    for (i = 0; i < fv_address_spaces->len; ++i) {
        as = g_array_index(fv_address_spaces, AddressSpace*, i);
        qemu_printf(" AS \"%s\", root: %s",
                    as->name, memory_region_name(as->root));
        if (as->root->alias) {
            qemu_printf(", alias %s", memory_region_name(as->root->alias));
        }
        qemu_printf("\n");
    }

    qemu_printf(" Root memory region: %s\n",
      view->root ? memory_region_name(view->root) : "(none)");

    if (n <= 0) {
        qemu_printf(MTREE_INDENT "No rendered FlatView\n\n");
        return;
    }

    while (n--) {
        mr = range->mr;
        if (range->offset_in_region) {
            qemu_printf(MTREE_INDENT HWADDR_FMT_plx "-" HWADDR_FMT_plx
                        " (prio %d, %s%s): %s @" HWADDR_FMT_plx,
                        int128_get64(range->addr.start),
                        int128_get64(range->addr.start)
                        + MR_SIZE(range->addr.size),
                        mr->priority,
                        range->nonvolatile ? "nv-" : "",
                        range->readonly ? "rom" : memory_region_type(mr),
                        memory_region_name(mr),
                        range->offset_in_region);
        } else {
            qemu_printf(MTREE_INDENT HWADDR_FMT_plx "-" HWADDR_FMT_plx
                        " (prio %d, %s%s): %s",
                        int128_get64(range->addr.start),
                        int128_get64(range->addr.start)
                        + MR_SIZE(range->addr.size),
                        mr->priority,
                        range->nonvolatile ? "nv-" : "",
                        range->readonly ? "rom" : memory_region_type(mr),
                        memory_region_name(mr));
        }
        if (fvi->owner) {
            mtree_print_mr_owner(mr);
        }

        if (fvi->ac) {
            for (i = 0; i < fv_address_spaces->len; ++i) {
                as = g_array_index(fv_address_spaces, AddressSpace*, i);
                if (fvi->ac->has_memory(current_machine, as,
                                        int128_get64(range->addr.start),
                                        MR_SIZE(range->addr.size) + 1)) {
                    qemu_printf(" %s", fvi->ac->name);
                }
            }
        }
        qemu_printf("\n");
        range++;
    }

#if !defined(CONFIG_USER_ONLY)
    if (fvi->dispatch_tree && view->root) {
        mtree_print_dispatch(view->dispatch, view->root);
    }
#endif

    qemu_printf("\n");
}

static gboolean mtree_info_flatview_free(gpointer key, gpointer value,
                                      gpointer user_data)
{
    FlatView *view = key;
    GArray *fv_address_spaces = value;

    g_array_unref(fv_address_spaces);
    flatview_unref(view);

    return true;
}

static void mtree_info_flatview(bool dispatch_tree, bool owner)
{
    struct FlatViewInfo fvi = {
        .counter = 0,
        .dispatch_tree = dispatch_tree,
        .owner = owner,
    };
    AddressSpace *as;
    FlatView *view;
    GArray *fv_address_spaces;
    GHashTable *views = g_hash_table_new(g_direct_hash, g_direct_equal);
    AccelClass *ac = ACCEL_GET_CLASS(current_accel());

    if (ac->has_memory) {
        fvi.ac = ac;
    }

    /* Gather all FVs in one table */
    QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
        view = address_space_get_flatview(as);

        fv_address_spaces = g_hash_table_lookup(views, view);
        if (!fv_address_spaces) {
            fv_address_spaces = g_array_new(false, false, sizeof(as));
            g_hash_table_insert(views, view, fv_address_spaces);
        }

        g_array_append_val(fv_address_spaces, as);
    }

    /* Print */
    g_hash_table_foreach(views, mtree_print_flatview, &fvi);

    /* Free */
    g_hash_table_foreach_remove(views, mtree_info_flatview_free, 0);
    g_hash_table_unref(views);
}

struct AddressSpaceInfo {
    MemoryRegionListHead *ml_head;
    bool owner;
    bool disabled;
};

/* Returns negative value if a < b; zero if a = b; positive value if a > b. */
static gint address_space_compare_name(gconstpointer a, gconstpointer b)
{
    const AddressSpace *as_a = a;
    const AddressSpace *as_b = b;

    return g_strcmp0(as_a->name, as_b->name);
}

static void mtree_print_as_name(gpointer data, gpointer user_data)
{
    AddressSpace *as = data;

    qemu_printf("address-space: %s\n", as->name);
}

static void mtree_print_as(gpointer key, gpointer value, gpointer user_data)
{
    MemoryRegion *mr = key;
    GSList *as_same_root_mr_list = value;
    struct AddressSpaceInfo *asi = user_data;

    g_slist_foreach(as_same_root_mr_list, mtree_print_as_name, NULL);
    mtree_print_mr(mr, 1, 0, asi->ml_head, asi->owner, asi->disabled);
    qemu_printf("\n");
}

static gboolean mtree_info_as_free(gpointer key, gpointer value,
                                   gpointer user_data)
{
    GSList *as_same_root_mr_list = value;

    g_slist_free(as_same_root_mr_list);

    return true;
}

static void mtree_info_as(bool dispatch_tree, bool owner, bool disabled)
{
    MemoryRegionListHead ml_head;
    MemoryRegionList *ml, *ml2;
    AddressSpace *as;
    GHashTable *views = g_hash_table_new(g_direct_hash, g_direct_equal);
    GSList *as_same_root_mr_list;
    struct AddressSpaceInfo asi = {
        .ml_head = &ml_head,
        .owner = owner,
        .disabled = disabled,
    };

    QTAILQ_INIT(&ml_head);

    QTAILQ_FOREACH(as, &address_spaces, address_spaces_link) {
        /* Create hashtable, key=AS root MR, value = list of AS */
        as_same_root_mr_list = g_hash_table_lookup(views, as->root);
        as_same_root_mr_list = g_slist_insert_sorted(as_same_root_mr_list, as,
                                                     address_space_compare_name);
        g_hash_table_insert(views, as->root, as_same_root_mr_list);
    }

    /* print address spaces */
    g_hash_table_foreach(views, mtree_print_as, &asi);
    g_hash_table_foreach_remove(views, mtree_info_as_free, 0);
    g_hash_table_unref(views);

    /* print aliased regions */
    QTAILQ_FOREACH(ml, &ml_head, mrqueue) {
        qemu_printf("memory-region: %s\n", memory_region_name(ml->mr));
        mtree_print_mr(ml->mr, 1, 0, &ml_head, owner, disabled);
        qemu_printf("\n");
    }

    QTAILQ_FOREACH_SAFE(ml, &ml_head, mrqueue, ml2) {
        g_free(ml);
    }
}

void mtree_info(bool flatview, bool dispatch_tree, bool owner, bool disabled)
{
    if (flatview) {
        mtree_info_flatview(dispatch_tree, owner);
    } else {
        mtree_info_as(dispatch_tree, owner, disabled);
    }
}

void memory_region_init_ram(MemoryRegion *mr,
                            Object *owner,
                            const char *name,
                            uint64_t size,
                            Error **errp)
{
    DeviceState *owner_dev;
    Error *err = NULL;

    memory_region_init_ram_nomigrate(mr, owner, name, size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    /* This will assert if owner is neither NULL nor a DeviceState.
     * We only want the owner here for the purposes of defining a
     * unique name for migration. TODO: Ideally we should implement
     * a naming scheme for Objects which are not DeviceStates, in
     * which case we can relax this restriction.
     */
    owner_dev = DEVICE(owner);
    vmstate_register_ram(mr, owner_dev);
}

void memory_region_init_rom(MemoryRegion *mr,
                            Object *owner,
                            const char *name,
                            uint64_t size,
                            Error **errp)
{
    DeviceState *owner_dev;
    Error *err = NULL;

    memory_region_init_rom_nomigrate(mr, owner, name, size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    /* This will assert if owner is neither NULL nor a DeviceState.
     * We only want the owner here for the purposes of defining a
     * unique name for migration. TODO: Ideally we should implement
     * a naming scheme for Objects which are not DeviceStates, in
     * which case we can relax this restriction.
     */
    owner_dev = DEVICE(owner);
    vmstate_register_ram(mr, owner_dev);
}

void memory_region_init_rom_device(MemoryRegion *mr,
                                   Object *owner,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   const char *name,
                                   uint64_t size,
                                   Error **errp)
{
    DeviceState *owner_dev;
    Error *err = NULL;

    memory_region_init_rom_device_nomigrate(mr, owner, ops, opaque,
                                            name, size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    /* This will assert if owner is neither NULL nor a DeviceState.
     * We only want the owner here for the purposes of defining a
     * unique name for migration. TODO: Ideally we should implement
     * a naming scheme for Objects which are not DeviceStates, in
     * which case we can relax this restriction.
     */
    owner_dev = DEVICE(owner);
    vmstate_register_ram(mr, owner_dev);
}

/*
 * Support softmmu builds with CONFIG_FUZZ using a weak symbol and a stub for
 * the fuzz_dma_read_cb callback
 */
#ifdef CONFIG_FUZZ
void __attribute__((weak)) fuzz_dma_read_cb(size_t addr,
                      size_t len,
                      MemoryRegion *mr)
{
}
#endif

static const TypeInfo memory_region_info = {
    .parent             = TYPE_OBJECT,
    .name               = TYPE_MEMORY_REGION,
    .class_size         = sizeof(MemoryRegionClass),
    .instance_size      = sizeof(MemoryRegion),
    .instance_init      = memory_region_initfn,
    .instance_finalize  = memory_region_finalize,
};

static const TypeInfo iommu_memory_region_info = {
    .parent             = TYPE_MEMORY_REGION,
    .name               = TYPE_IOMMU_MEMORY_REGION,
    .class_size         = sizeof(IOMMUMemoryRegionClass),
    .instance_size      = sizeof(IOMMUMemoryRegion),
    .instance_init      = iommu_memory_region_initfn,
    .abstract           = true,
};

static const TypeInfo ram_discard_manager_info = {
    .parent             = TYPE_INTERFACE,
    .name               = TYPE_RAM_DISCARD_MANAGER,
    .class_size         = sizeof(RamDiscardManagerClass),
};

static void memory_register_types(void)
{
    type_register_static(&memory_region_info);
    type_register_static(&iommu_memory_region_info);
    type_register_static(&ram_discard_manager_info);
}

type_init(memory_register_types)
