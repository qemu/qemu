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

#include "memory.h"
#include "exec-memory.h"
#include "ioport.h"
#include "bitops.h"
#include "kvm.h"
#include <assert.h>

#define WANT_EXEC_OBSOLETE
#include "exec-obsolete.h"

unsigned memory_region_transaction_depth = 0;
static bool memory_region_update_pending = false;
static bool global_dirty_log = false;

static QTAILQ_HEAD(memory_listeners, MemoryListener) memory_listeners
    = QTAILQ_HEAD_INITIALIZER(memory_listeners);

typedef struct AddrRange AddrRange;

/*
 * Note using signed integers limits us to physical addresses at most
 * 63 bits wide.  They are needed for negative offsetting in aliases
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

static bool memory_listener_match(MemoryListener *listener,
                                  MemoryRegionSection *section)
{
    return !listener->address_space_filter
        || listener->address_space_filter == section->address_space;
}

#define MEMORY_LISTENER_CALL_GLOBAL(_callback, _direction, _args...)    \
    do {                                                                \
        MemoryListener *_listener;                                      \
                                                                        \
        switch (_direction) {                                           \
        case Forward:                                                   \
            QTAILQ_FOREACH(_listener, &memory_listeners, link) {        \
                _listener->_callback(_listener, ##_args);               \
            }                                                           \
            break;                                                      \
        case Reverse:                                                   \
            QTAILQ_FOREACH_REVERSE(_listener, &memory_listeners,        \
                                   memory_listeners, link) {            \
                _listener->_callback(_listener, ##_args);               \
            }                                                           \
            break;                                                      \
        default:                                                        \
            abort();                                                    \
        }                                                               \
    } while (0)

#define MEMORY_LISTENER_CALL(_callback, _direction, _section, _args...) \
    do {                                                                \
        MemoryListener *_listener;                                      \
                                                                        \
        switch (_direction) {                                           \
        case Forward:                                                   \
            QTAILQ_FOREACH(_listener, &memory_listeners, link) {        \
                if (memory_listener_match(_listener, _section)) {       \
                    _listener->_callback(_listener, _section, ##_args); \
                }                                                       \
            }                                                           \
            break;                                                      \
        case Reverse:                                                   \
            QTAILQ_FOREACH_REVERSE(_listener, &memory_listeners,        \
                                   memory_listeners, link) {            \
                if (memory_listener_match(_listener, _section)) {       \
                    _listener->_callback(_listener, _section, ##_args); \
                }                                                       \
            }                                                           \
            break;                                                      \
        default:                                                        \
            abort();                                                    \
        }                                                               \
    } while (0)

#define MEMORY_LISTENER_UPDATE_REGION(fr, as, dir, callback)            \
    MEMORY_LISTENER_CALL(callback, dir, (&(MemoryRegionSection) {       \
        .mr = (fr)->mr,                                                 \
        .address_space = (as)->root,                                    \
        .offset_within_region = (fr)->offset_in_region,                 \
        .size = int128_get64((fr)->addr.size),                          \
        .offset_within_address_space = int128_get64((fr)->addr.start),  \
        .readonly = (fr)->readonly,                                     \
              }))

struct CoalescedMemoryRange {
    AddrRange addr;
    QTAILQ_ENTRY(CoalescedMemoryRange) link;
};

struct MemoryRegionIoeventfd {
    AddrRange addr;
    bool match_data;
    uint64_t data;
    int fd;
};

static bool memory_region_ioeventfd_before(MemoryRegionIoeventfd a,
                                           MemoryRegionIoeventfd b)
{
    if (int128_lt(a.addr.start, b.addr.start)) {
        return true;
    } else if (int128_gt(a.addr.start, b.addr.start)) {
        return false;
    } else if (int128_lt(a.addr.size, b.addr.size)) {
        return true;
    } else if (int128_gt(a.addr.size, b.addr.size)) {
        return false;
    } else if (a.match_data < b.match_data) {
        return true;
    } else  if (a.match_data > b.match_data) {
        return false;
    } else if (a.match_data) {
        if (a.data < b.data) {
            return true;
        } else if (a.data > b.data) {
            return false;
        }
    }
    if (a.fd < b.fd) {
        return true;
    } else if (a.fd > b.fd) {
        return false;
    }
    return false;
}

static bool memory_region_ioeventfd_equal(MemoryRegionIoeventfd a,
                                          MemoryRegionIoeventfd b)
{
    return !memory_region_ioeventfd_before(a, b)
        && !memory_region_ioeventfd_before(b, a);
}

typedef struct FlatRange FlatRange;
typedef struct FlatView FlatView;

/* Range of memory in the global map.  Addresses are absolute. */
struct FlatRange {
    MemoryRegion *mr;
    target_phys_addr_t offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
    bool readable;
    bool readonly;
};

/* Flattened global view of current active memory hierarchy.  Kept in sorted
 * order.
 */
struct FlatView {
    FlatRange *ranges;
    unsigned nr;
    unsigned nr_allocated;
};

typedef struct AddressSpace AddressSpace;
typedef struct AddressSpaceOps AddressSpaceOps;

/* A system address space - I/O, memory, etc. */
struct AddressSpace {
    MemoryRegion *root;
    FlatView current_map;
    int ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
};

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

static bool flatrange_equal(FlatRange *a, FlatRange *b)
{
    return a->mr == b->mr
        && addrrange_equal(a->addr, b->addr)
        && a->offset_in_region == b->offset_in_region
        && a->readable == b->readable
        && a->readonly == b->readonly;
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
        view->ranges = g_realloc(view->ranges,
                                    view->nr_allocated * sizeof(*view->ranges));
    }
    memmove(view->ranges + pos + 1, view->ranges + pos,
            (view->nr - pos) * sizeof(FlatRange));
    view->ranges[pos] = *range;
    ++view->nr;
}

static void flatview_destroy(FlatView *view)
{
    g_free(view->ranges);
}

static bool can_merge(FlatRange *r1, FlatRange *r2)
{
    return int128_eq(addrrange_end(r1->addr), r2->addr.start)
        && r1->mr == r2->mr
        && int128_eq(int128_add(int128_make64(r1->offset_in_region),
                                r1->addr.size),
                     int128_make64(r2->offset_in_region))
        && r1->dirty_log_mask == r2->dirty_log_mask
        && r1->readable == r2->readable
        && r1->readonly == r2->readonly;
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
            int128_addto(&view->ranges[i].addr.size, view->ranges[j].addr.size);
            ++j;
        }
        ++i;
        memmove(&view->ranges[i], &view->ranges[j],
                (view->nr - j) * sizeof(view->ranges[j]));
        view->nr -= j - i;
    }
}

static void memory_region_read_accessor(void *opaque,
                                        target_phys_addr_t addr,
                                        uint64_t *value,
                                        unsigned size,
                                        unsigned shift,
                                        uint64_t mask)
{
    MemoryRegion *mr = opaque;
    uint64_t tmp;

    tmp = mr->ops->read(mr->opaque, addr, size);
    *value |= (tmp & mask) << shift;
}

static void memory_region_write_accessor(void *opaque,
                                         target_phys_addr_t addr,
                                         uint64_t *value,
                                         unsigned size,
                                         unsigned shift,
                                         uint64_t mask)
{
    MemoryRegion *mr = opaque;
    uint64_t tmp;

    tmp = (*value >> shift) & mask;
    mr->ops->write(mr->opaque, addr, tmp, size);
}

static void access_with_adjusted_size(target_phys_addr_t addr,
                                      uint64_t *value,
                                      unsigned size,
                                      unsigned access_size_min,
                                      unsigned access_size_max,
                                      void (*access)(void *opaque,
                                                     target_phys_addr_t addr,
                                                     uint64_t *value,
                                                     unsigned size,
                                                     unsigned shift,
                                                     uint64_t mask),
                                      void *opaque)
{
    uint64_t access_mask;
    unsigned access_size;
    unsigned i;

    if (!access_size_min) {
        access_size_min = 1;
    }
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        access(opaque, addr + i, value, access_size, i * 8, access_mask);
    }
}

static AddressSpace address_space_memory;

static const MemoryRegionPortio *find_portio(MemoryRegion *mr, uint64_t offset,
                                             unsigned width, bool write)
{
    const MemoryRegionPortio *mrp;

    for (mrp = mr->ops->old_portio; mrp->size; ++mrp) {
        if (offset >= mrp->offset && offset < mrp->offset + mrp->len
            && width == mrp->size
            && (write ? (bool)mrp->write : (bool)mrp->read)) {
            return mrp;
        }
    }
    return NULL;
}

static void memory_region_iorange_read(IORange *iorange,
                                       uint64_t offset,
                                       unsigned width,
                                       uint64_t *data)
{
    MemoryRegionIORange *mrio
        = container_of(iorange, MemoryRegionIORange, iorange);
    MemoryRegion *mr = mrio->mr;

    offset += mrio->offset;
    if (mr->ops->old_portio) {
        const MemoryRegionPortio *mrp = find_portio(mr, offset - mrio->offset,
                                                    width, false);

        *data = ((uint64_t)1 << (width * 8)) - 1;
        if (mrp) {
            *data = mrp->read(mr->opaque, offset);
        } else if (width == 2) {
            mrp = find_portio(mr, offset - mrio->offset, 1, false);
            assert(mrp);
            *data = mrp->read(mr->opaque, offset) |
                    (mrp->read(mr->opaque, offset + 1) << 8);
        }
        return;
    }
    *data = 0;
    access_with_adjusted_size(offset, data, width,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_read_accessor, mr);
}

static void memory_region_iorange_write(IORange *iorange,
                                        uint64_t offset,
                                        unsigned width,
                                        uint64_t data)
{
    MemoryRegionIORange *mrio
        = container_of(iorange, MemoryRegionIORange, iorange);
    MemoryRegion *mr = mrio->mr;

    offset += mrio->offset;
    if (mr->ops->old_portio) {
        const MemoryRegionPortio *mrp = find_portio(mr, offset - mrio->offset,
                                                    width, true);

        if (mrp) {
            mrp->write(mr->opaque, offset, data);
        } else if (width == 2) {
            mrp = find_portio(mr, offset - mrio->offset, 1, false);
            assert(mrp);
            mrp->write(mr->opaque, offset, data & 0xff);
            mrp->write(mr->opaque, offset + 1, data >> 8);
        }
        return;
    }
    access_with_adjusted_size(offset, &data, width,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_write_accessor, mr);
}

static void memory_region_iorange_destructor(IORange *iorange)
{
    g_free(container_of(iorange, MemoryRegionIORange, iorange));
}

const IORangeOps memory_region_iorange_ops = {
    .read = memory_region_iorange_read,
    .write = memory_region_iorange_write,
    .destructor = memory_region_iorange_destructor,
};

static AddressSpace address_space_io;

static AddressSpace *memory_region_to_address_space(MemoryRegion *mr)
{
    while (mr->parent) {
        mr = mr->parent;
    }
    if (mr == address_space_memory.root) {
        return &address_space_memory;
    }
    if (mr == address_space_io.root) {
        return &address_space_io;
    }
    abort();
}

/* Render a memory region into the global view.  Ranges in @view obscure
 * ranges in @mr.
 */
static void render_memory_region(FlatView *view,
                                 MemoryRegion *mr,
                                 Int128 base,
                                 AddrRange clip,
                                 bool readonly)
{
    MemoryRegion *subregion;
    unsigned i;
    target_phys_addr_t offset_in_region;
    Int128 remain;
    Int128 now;
    FlatRange fr;
    AddrRange tmp;

    if (!mr->enabled) {
        return;
    }

    int128_addto(&base, int128_make64(mr->addr));
    readonly |= mr->readonly;

    tmp = addrrange_make(base, mr->size);

    if (!addrrange_intersects(tmp, clip)) {
        return;
    }

    clip = addrrange_intersection(tmp, clip);

    if (mr->alias) {
        int128_subfrom(&base, int128_make64(mr->alias->addr));
        int128_subfrom(&base, int128_make64(mr->alias_offset));
        render_memory_region(view, mr->alias, base, clip, readonly);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip, readonly);
    }

    if (!mr->terminates) {
        return;
    }

    offset_in_region = int128_get64(int128_sub(clip.start, base));
    base = clip.start;
    remain = clip.size;

    /* Render the region itself into any gaps left by the current view. */
    for (i = 0; i < view->nr && int128_nz(remain); ++i) {
        if (int128_ge(base, addrrange_end(view->ranges[i].addr))) {
            continue;
        }
        if (int128_lt(base, view->ranges[i].addr.start)) {
            now = int128_min(remain,
                             int128_sub(view->ranges[i].addr.start, base));
            fr.mr = mr;
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            fr.dirty_log_mask = mr->dirty_log_mask;
            fr.readable = mr->readable;
            fr.readonly = readonly;
            flatview_insert(view, i, &fr);
            ++i;
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
        if (int128_eq(base, view->ranges[i].addr.start)) {
            now = int128_min(remain, view->ranges[i].addr.size);
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
    }
    if (int128_nz(remain)) {
        fr.mr = mr;
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        fr.dirty_log_mask = mr->dirty_log_mask;
        fr.readable = mr->readable;
        fr.readonly = readonly;
        flatview_insert(view, i, &fr);
    }
}

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView generate_memory_topology(MemoryRegion *mr)
{
    FlatView view;

    flatview_init(&view);

    render_memory_region(&view, mr, int128_zero(),
                         addrrange_make(int128_zero(), int128_2_64()), false);
    flatview_simplify(&view);

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
                || memory_region_ioeventfd_before(fds_old[iold],
                                                  fds_new[inew]))) {
            fd = &fds_old[iold];
            section = (MemoryRegionSection) {
                .address_space = as->root,
                .offset_within_address_space = int128_get64(fd->addr.start),
                .size = int128_get64(fd->addr.size),
            };
            MEMORY_LISTENER_CALL(eventfd_del, Forward, &section,
                                 fd->match_data, fd->data, fd->fd);
            ++iold;
        } else if (inew < fds_new_nb
                   && (iold == fds_old_nb
                       || memory_region_ioeventfd_before(fds_new[inew],
                                                         fds_old[iold]))) {
            fd = &fds_new[inew];
            section = (MemoryRegionSection) {
                .address_space = as->root,
                .offset_within_address_space = int128_get64(fd->addr.start),
                .size = int128_get64(fd->addr.size),
            };
            MEMORY_LISTENER_CALL(eventfd_add, Reverse, &section,
                                 fd->match_data, fd->data, fd->fd);
            ++inew;
        } else {
            ++iold;
            ++inew;
        }
    }
}

static void address_space_update_ioeventfds(AddressSpace *as)
{
    FlatRange *fr;
    unsigned ioeventfd_nb = 0;
    MemoryRegionIoeventfd *ioeventfds = NULL;
    AddrRange tmp;
    unsigned i;

    FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
        for (i = 0; i < fr->mr->ioeventfd_nb; ++i) {
            tmp = addrrange_shift(fr->mr->ioeventfds[i].addr,
                                  int128_sub(fr->addr.start,
                                             int128_make64(fr->offset_in_region)));
            if (addrrange_intersects(fr->addr, tmp)) {
                ++ioeventfd_nb;
                ioeventfds = g_realloc(ioeventfds,
                                          ioeventfd_nb * sizeof(*ioeventfds));
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
}

static void address_space_update_topology_pass(AddressSpace *as,
                                               FlatView old_view,
                                               FlatView new_view,
                                               bool adding)
{
    unsigned iold, inew;
    FlatRange *frold, *frnew;

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
                || int128_lt(frold->addr.start, frnew->addr.start)
                || (int128_eq(frold->addr.start, frnew->addr.start)
                    && !flatrange_equal(frold, frnew)))) {
            /* In old, but (not in new, or in new but attributes changed). */

            if (!adding) {
                MEMORY_LISTENER_UPDATE_REGION(frold, as, Reverse, region_del);
            }

            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both (logging may have changed) */

            if (adding) {
                MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, region_nop);
                if (frold->dirty_log_mask && !frnew->dirty_log_mask) {
                    MEMORY_LISTENER_UPDATE_REGION(frnew, as, Reverse, log_stop);
                } else if (frnew->dirty_log_mask && !frold->dirty_log_mask) {
                    MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, log_start);
                }
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            if (adding) {
                MEMORY_LISTENER_UPDATE_REGION(frnew, as, Forward, region_add);
            }

            ++inew;
        }
    }
}


static void address_space_update_topology(AddressSpace *as)
{
    FlatView old_view = as->current_map;
    FlatView new_view = generate_memory_topology(as->root);

    address_space_update_topology_pass(as, old_view, new_view, false);
    address_space_update_topology_pass(as, old_view, new_view, true);

    as->current_map = new_view;
    flatview_destroy(&old_view);
    address_space_update_ioeventfds(as);
}

static void memory_region_update_topology(MemoryRegion *mr)
{
    if (memory_region_transaction_depth) {
        memory_region_update_pending |= !mr || mr->enabled;
        return;
    }

    if (mr && !mr->enabled) {
        return;
    }

    MEMORY_LISTENER_CALL_GLOBAL(begin, Forward);

    if (address_space_memory.root) {
        address_space_update_topology(&address_space_memory);
    }
    if (address_space_io.root) {
        address_space_update_topology(&address_space_io);
    }

    MEMORY_LISTENER_CALL_GLOBAL(commit, Forward);

    memory_region_update_pending = false;
}

void memory_region_transaction_begin(void)
{
    ++memory_region_transaction_depth;
}

void memory_region_transaction_commit(void)
{
    assert(memory_region_transaction_depth);
    --memory_region_transaction_depth;
    if (!memory_region_transaction_depth && memory_region_update_pending) {
        memory_region_update_topology(NULL);
    }
}

static void memory_region_destructor_none(MemoryRegion *mr)
{
}

static void memory_region_destructor_ram(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_addr);
}

static void memory_region_destructor_ram_from_ptr(MemoryRegion *mr)
{
    qemu_ram_free_from_ptr(mr->ram_addr);
}

static void memory_region_destructor_iomem(MemoryRegion *mr)
{
}

static void memory_region_destructor_rom_device(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_addr & TARGET_PAGE_MASK);
}

static bool memory_region_wrong_endianness(MemoryRegion *mr)
{
#ifdef TARGET_WORDS_BIGENDIAN
    return mr->ops->endianness == DEVICE_LITTLE_ENDIAN;
#else
    return mr->ops->endianness == DEVICE_BIG_ENDIAN;
#endif
}

void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size)
{
    mr->ops = NULL;
    mr->parent = NULL;
    mr->size = int128_make64(size);
    if (size == UINT64_MAX) {
        mr->size = int128_2_64();
    }
    mr->addr = 0;
    mr->subpage = false;
    mr->enabled = true;
    mr->terminates = false;
    mr->ram = false;
    mr->readable = true;
    mr->readonly = false;
    mr->rom_device = false;
    mr->destructor = memory_region_destructor_none;
    mr->priority = 0;
    mr->may_overlap = false;
    mr->alias = NULL;
    QTAILQ_INIT(&mr->subregions);
    memset(&mr->subregions_link, 0, sizeof mr->subregions_link);
    QTAILQ_INIT(&mr->coalesced);
    mr->name = g_strdup(name);
    mr->dirty_log_mask = 0;
    mr->ioeventfd_nb = 0;
    mr->ioeventfds = NULL;
}

static bool memory_region_access_valid(MemoryRegion *mr,
                                       target_phys_addr_t addr,
                                       unsigned size,
                                       bool is_write)
{
    if (mr->ops->valid.accepts
        && !mr->ops->valid.accepts(mr->opaque, addr, size, is_write)) {
        return false;
    }

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

static uint64_t memory_region_dispatch_read1(MemoryRegion *mr,
                                             target_phys_addr_t addr,
                                             unsigned size)
{
    uint64_t data = 0;

    if (!memory_region_access_valid(mr, addr, size, false)) {
        return -1U; /* FIXME: better signalling */
    }

    if (!mr->ops->read) {
        return mr->ops->old_mmio.read[bitops_ffsl(size)](mr->opaque, addr);
    }

    /* FIXME: support unaligned access */
    access_with_adjusted_size(addr, &data, size,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_read_accessor, mr);

    return data;
}

static void adjust_endianness(MemoryRegion *mr, uint64_t *data, unsigned size)
{
    if (memory_region_wrong_endianness(mr)) {
        switch (size) {
        case 1:
            break;
        case 2:
            *data = bswap16(*data);
            break;
        case 4:
            *data = bswap32(*data);
            break;
        default:
            abort();
        }
    }
}

static uint64_t memory_region_dispatch_read(MemoryRegion *mr,
                                            target_phys_addr_t addr,
                                            unsigned size)
{
    uint64_t ret;

    ret = memory_region_dispatch_read1(mr, addr, size);
    adjust_endianness(mr, &ret, size);
    return ret;
}

static void memory_region_dispatch_write(MemoryRegion *mr,
                                         target_phys_addr_t addr,
                                         uint64_t data,
                                         unsigned size)
{
    if (!memory_region_access_valid(mr, addr, size, true)) {
        return; /* FIXME: better signalling */
    }

    adjust_endianness(mr, &data, size);

    if (!mr->ops->write) {
        mr->ops->old_mmio.write[bitops_ffsl(size)](mr->opaque, addr, data);
        return;
    }

    /* FIXME: support unaligned access */
    access_with_adjusted_size(addr, &data, size,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_write_accessor, mr);
}

void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_iomem;
    mr->ram_addr = ~(ram_addr_t)0;
}

void memory_region_init_ram(MemoryRegion *mr,
                            const char *name,
                            uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;
    mr->ram_addr = qemu_ram_alloc(size, mr);
}

void memory_region_init_ram_ptr(MemoryRegion *mr,
                                const char *name,
                                uint64_t size,
                                void *ptr)
{
    memory_region_init(mr, name, size);
    mr->ram = true;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram_from_ptr;
    mr->ram_addr = qemu_ram_alloc_from_ptr(size, ptr, mr);
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

void memory_region_init_rom_device(MemoryRegion *mr,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   const char *name,
                                   uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->terminates = true;
    mr->rom_device = true;
    mr->destructor = memory_region_destructor_rom_device;
    mr->ram_addr = qemu_ram_alloc(size, mr);
}

static uint64_t invalid_read(void *opaque, target_phys_addr_t addr,
                             unsigned size)
{
    MemoryRegion *mr = opaque;

    if (!mr->warning_printed) {
        fprintf(stderr, "Invalid read from memory region %s\n", mr->name);
        mr->warning_printed = true;
    }
    return -1U;
}

static void invalid_write(void *opaque, target_phys_addr_t addr, uint64_t data,
                          unsigned size)
{
    MemoryRegion *mr = opaque;

    if (!mr->warning_printed) {
        fprintf(stderr, "Invalid write to memory region %s\n", mr->name);
        mr->warning_printed = true;
    }
}

static const MemoryRegionOps reservation_ops = {
    .read = invalid_read,
    .write = invalid_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void memory_region_init_reservation(MemoryRegion *mr,
                                    const char *name,
                                    uint64_t size)
{
    memory_region_init_io(mr, &reservation_ops, mr, name, size);
}

void memory_region_destroy(MemoryRegion *mr)
{
    assert(QTAILQ_EMPTY(&mr->subregions));
    mr->destructor(mr);
    memory_region_clear_coalescing(mr);
    g_free((char *)mr->name);
    g_free(mr->ioeventfds);
}

uint64_t memory_region_size(MemoryRegion *mr)
{
    if (int128_eq(mr->size, int128_2_64())) {
        return UINT64_MAX;
    }
    return int128_get64(mr->size);
}

const char *memory_region_name(MemoryRegion *mr)
{
    return mr->name;
}

bool memory_region_is_ram(MemoryRegion *mr)
{
    return mr->ram;
}

bool memory_region_is_logging(MemoryRegion *mr)
{
    return mr->dirty_log_mask;
}

bool memory_region_is_rom(MemoryRegion *mr)
{
    return mr->ram && mr->readonly;
}

void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client)
{
    uint8_t mask = 1 << client;

    mr->dirty_log_mask = (mr->dirty_log_mask & ~mask) | (log * mask);
    memory_region_update_topology(mr);
}

bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             target_phys_addr_t size, unsigned client)
{
    assert(mr->terminates);
    return cpu_physical_memory_get_dirty(mr->ram_addr + addr, size,
                                         1 << client);
}

void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             target_phys_addr_t size)
{
    assert(mr->terminates);
    return cpu_physical_memory_set_dirty_range(mr->ram_addr + addr, size, -1);
}

void memory_region_sync_dirty_bitmap(MemoryRegion *mr)
{
    FlatRange *fr;

    FOR_EACH_FLAT_RANGE(fr, &address_space_memory.current_map) {
        if (fr->mr == mr) {
            MEMORY_LISTENER_UPDATE_REGION(fr, &address_space_memory,
                                          Forward, log_sync);
        }
    }
}

void memory_region_set_readonly(MemoryRegion *mr, bool readonly)
{
    if (mr->readonly != readonly) {
        mr->readonly = readonly;
        memory_region_update_topology(mr);
    }
}

void memory_region_rom_device_set_readable(MemoryRegion *mr, bool readable)
{
    if (mr->readable != readable) {
        mr->readable = readable;
        memory_region_update_topology(mr);
    }
}

void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client)
{
    assert(mr->terminates);
    cpu_physical_memory_reset_dirty(mr->ram_addr + addr,
                                    mr->ram_addr + addr + size,
                                    1 << client);
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_ram_ptr(mr->alias) + mr->alias_offset;
    }

    assert(mr->terminates);

    return qemu_get_ram_ptr(mr->ram_addr & TARGET_PAGE_MASK);
}

static void memory_region_update_coalesced_range(MemoryRegion *mr)
{
    FlatRange *fr;
    CoalescedMemoryRange *cmr;
    AddrRange tmp;

    FOR_EACH_FLAT_RANGE(fr, &address_space_memory.current_map) {
        if (fr->mr == mr) {
            qemu_unregister_coalesced_mmio(int128_get64(fr->addr.start),
                                           int128_get64(fr->addr.size));
            QTAILQ_FOREACH(cmr, &mr->coalesced, link) {
                tmp = addrrange_shift(cmr->addr,
                                      int128_sub(fr->addr.start,
                                                 int128_make64(fr->offset_in_region)));
                if (!addrrange_intersects(tmp, fr->addr)) {
                    continue;
                }
                tmp = addrrange_intersection(tmp, fr->addr);
                qemu_register_coalesced_mmio(int128_get64(tmp.start),
                                             int128_get64(tmp.size));
            }
        }
    }
}

void memory_region_set_coalescing(MemoryRegion *mr)
{
    memory_region_clear_coalescing(mr);
    memory_region_add_coalescing(mr, 0, int128_get64(mr->size));
}

void memory_region_add_coalescing(MemoryRegion *mr,
                                  target_phys_addr_t offset,
                                  uint64_t size)
{
    CoalescedMemoryRange *cmr = g_malloc(sizeof(*cmr));

    cmr->addr = addrrange_make(int128_make64(offset), int128_make64(size));
    QTAILQ_INSERT_TAIL(&mr->coalesced, cmr, link);
    memory_region_update_coalesced_range(mr);
}

void memory_region_clear_coalescing(MemoryRegion *mr)
{
    CoalescedMemoryRange *cmr;

    while (!QTAILQ_EMPTY(&mr->coalesced)) {
        cmr = QTAILQ_FIRST(&mr->coalesced);
        QTAILQ_REMOVE(&mr->coalesced, cmr, link);
        g_free(cmr);
    }
    memory_region_update_coalesced_range(mr);
}

void memory_region_add_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = int128_make64(addr),
        .addr.size = int128_make64(size),
        .match_data = match_data,
        .data = data,
        .fd = fd,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_before(mrfd, mr->ioeventfds[i])) {
            break;
        }
    }
    ++mr->ioeventfd_nb;
    mr->ioeventfds = g_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds) * mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i+1], &mr->ioeventfds[i],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb-1 - i));
    mr->ioeventfds[i] = mrfd;
    memory_region_update_topology(mr);
}

void memory_region_del_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = int128_make64(addr),
        .addr.size = int128_make64(size),
        .match_data = match_data,
        .data = data,
        .fd = fd,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_equal(mrfd, mr->ioeventfds[i])) {
            break;
        }
    }
    assert(i != mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i], &mr->ioeventfds[i+1],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb - (i+1)));
    --mr->ioeventfd_nb;
    mr->ioeventfds = g_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds)*mr->ioeventfd_nb + 1);
    memory_region_update_topology(mr);
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
        if (int128_gt(int128_make64(offset),
                      int128_add(int128_make64(other->addr), other->size))
            || int128_le(int128_add(int128_make64(offset), subregion->size),
                         int128_make64(other->addr))) {
            continue;
        }
#if 0
        printf("warning: subregion collision %llx/%llx (%s) "
               "vs %llx/%llx (%s)\n",
               (unsigned long long)offset,
               (unsigned long long)int128_get64(subregion->size),
               subregion->name,
               (unsigned long long)other->addr,
               (unsigned long long)int128_get64(other->size),
               other->name);
#endif
    }
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_topology(mr);
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
    memory_region_update_topology(mr);
}

void memory_region_set_enabled(MemoryRegion *mr, bool enabled)
{
    if (enabled == mr->enabled) {
        return;
    }
    mr->enabled = enabled;
    memory_region_update_topology(NULL);
}

void memory_region_set_address(MemoryRegion *mr, target_phys_addr_t addr)
{
    MemoryRegion *parent = mr->parent;
    unsigned priority = mr->priority;
    bool may_overlap = mr->may_overlap;

    if (addr == mr->addr || !parent) {
        mr->addr = addr;
        return;
    }

    memory_region_transaction_begin();
    memory_region_del_subregion(parent, mr);
    if (may_overlap) {
        memory_region_add_subregion_overlap(parent, addr, mr, priority);
    } else {
        memory_region_add_subregion(parent, addr, mr);
    }
    memory_region_transaction_commit();
}

void memory_region_set_alias_offset(MemoryRegion *mr, target_phys_addr_t offset)
{
    target_phys_addr_t old_offset = mr->alias_offset;

    assert(mr->alias);
    mr->alias_offset = offset;

    if (offset == old_offset || !mr->parent) {
        return;
    }

    memory_region_update_topology(mr);
}

ram_addr_t memory_region_get_ram_addr(MemoryRegion *mr)
{
    return mr->ram_addr;
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

static FlatRange *address_space_lookup(AddressSpace *as, AddrRange addr)
{
    return bsearch(&addr, as->current_map.ranges, as->current_map.nr,
                   sizeof(FlatRange), cmp_flatrange_addr);
}

MemoryRegionSection memory_region_find(MemoryRegion *address_space,
                                       target_phys_addr_t addr, uint64_t size)
{
    AddressSpace *as = memory_region_to_address_space(address_space);
    AddrRange range = addrrange_make(int128_make64(addr),
                                     int128_make64(size));
    FlatRange *fr = address_space_lookup(as, range);
    MemoryRegionSection ret = { .mr = NULL, .size = 0 };

    if (!fr) {
        return ret;
    }

    while (fr > as->current_map.ranges
           && addrrange_intersects(fr[-1].addr, range)) {
        --fr;
    }

    ret.mr = fr->mr;
    range = addrrange_intersection(range, fr->addr);
    ret.offset_within_region = fr->offset_in_region;
    ret.offset_within_region += int128_get64(int128_sub(range.start,
                                                        fr->addr.start));
    ret.size = int128_get64(range.size);
    ret.offset_within_address_space = int128_get64(range.start);
    ret.readonly = fr->readonly;
    return ret;
}

void memory_global_sync_dirty_bitmap(MemoryRegion *address_space)
{
    AddressSpace *as = memory_region_to_address_space(address_space);
    FlatRange *fr;

    FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
        MEMORY_LISTENER_UPDATE_REGION(fr, as, Forward, log_sync);
    }
}

void memory_global_dirty_log_start(void)
{
    global_dirty_log = true;
    MEMORY_LISTENER_CALL_GLOBAL(log_global_start, Forward);
}

void memory_global_dirty_log_stop(void)
{
    global_dirty_log = false;
    MEMORY_LISTENER_CALL_GLOBAL(log_global_stop, Reverse);
}

static void listener_add_address_space(MemoryListener *listener,
                                       AddressSpace *as)
{
    FlatRange *fr;

    if (listener->address_space_filter
        && listener->address_space_filter != as->root) {
        return;
    }

    if (global_dirty_log) {
        listener->log_global_start(listener);
    }
    FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
        MemoryRegionSection section = {
            .mr = fr->mr,
            .address_space = as->root,
            .offset_within_region = fr->offset_in_region,
            .size = int128_get64(fr->addr.size),
            .offset_within_address_space = int128_get64(fr->addr.start),
            .readonly = fr->readonly,
        };
        listener->region_add(listener, &section);
    }
}

void memory_listener_register(MemoryListener *listener, MemoryRegion *filter)
{
    MemoryListener *other = NULL;

    listener->address_space_filter = filter;
    if (QTAILQ_EMPTY(&memory_listeners)
        || listener->priority >= QTAILQ_LAST(&memory_listeners,
                                             memory_listeners)->priority) {
        QTAILQ_INSERT_TAIL(&memory_listeners, listener, link);
    } else {
        QTAILQ_FOREACH(other, &memory_listeners, link) {
            if (listener->priority < other->priority) {
                break;
            }
        }
        QTAILQ_INSERT_BEFORE(other, listener, link);
    }
    listener_add_address_space(listener, &address_space_memory);
    listener_add_address_space(listener, &address_space_io);
}

void memory_listener_unregister(MemoryListener *listener)
{
    QTAILQ_REMOVE(&memory_listeners, listener, link);
}

void set_system_memory_map(MemoryRegion *mr)
{
    address_space_memory.root = mr;
    memory_region_update_topology(NULL);
}

void set_system_io_map(MemoryRegion *mr)
{
    address_space_io.root = mr;
    memory_region_update_topology(NULL);
}

uint64_t io_mem_read(MemoryRegion *mr, target_phys_addr_t addr, unsigned size)
{
    return memory_region_dispatch_read(mr, addr, size);
}

void io_mem_write(MemoryRegion *mr, target_phys_addr_t addr,
                  uint64_t val, unsigned size)
{
    memory_region_dispatch_write(mr, addr, val, size);
}

typedef struct MemoryRegionList MemoryRegionList;

struct MemoryRegionList {
    const MemoryRegion *mr;
    bool printed;
    QTAILQ_ENTRY(MemoryRegionList) queue;
};

typedef QTAILQ_HEAD(queue, MemoryRegionList) MemoryRegionListHead;

static void mtree_print_mr(fprintf_function mon_printf, void *f,
                           const MemoryRegion *mr, unsigned int level,
                           target_phys_addr_t base,
                           MemoryRegionListHead *alias_print_queue)
{
    MemoryRegionList *new_ml, *ml, *next_ml;
    MemoryRegionListHead submr_print_queue;
    const MemoryRegion *submr;
    unsigned int i;

    if (!mr) {
        return;
    }

    for (i = 0; i < level; i++) {
        mon_printf(f, "  ");
    }

    if (mr->alias) {
        MemoryRegionList *ml;
        bool found = false;

        /* check if the alias is already in the queue */
        QTAILQ_FOREACH(ml, alias_print_queue, queue) {
            if (ml->mr == mr->alias && !ml->printed) {
                found = true;
            }
        }

        if (!found) {
            ml = g_new(MemoryRegionList, 1);
            ml->mr = mr->alias;
            ml->printed = false;
            QTAILQ_INSERT_TAIL(alias_print_queue, ml, queue);
        }
        mon_printf(f, TARGET_FMT_plx "-" TARGET_FMT_plx
                   " (prio %d, %c%c): alias %s @%s " TARGET_FMT_plx
                   "-" TARGET_FMT_plx "\n",
                   base + mr->addr,
                   base + mr->addr
                   + (target_phys_addr_t)int128_get64(mr->size) - 1,
                   mr->priority,
                   mr->readable ? 'R' : '-',
                   !mr->readonly && !(mr->rom_device && mr->readable) ? 'W'
                                                                      : '-',
                   mr->name,
                   mr->alias->name,
                   mr->alias_offset,
                   mr->alias_offset
                   + (target_phys_addr_t)int128_get64(mr->size) - 1);
    } else {
        mon_printf(f,
                   TARGET_FMT_plx "-" TARGET_FMT_plx " (prio %d, %c%c): %s\n",
                   base + mr->addr,
                   base + mr->addr
                   + (target_phys_addr_t)int128_get64(mr->size) - 1,
                   mr->priority,
                   mr->readable ? 'R' : '-',
                   !mr->readonly && !(mr->rom_device && mr->readable) ? 'W'
                                                                      : '-',
                   mr->name);
    }

    QTAILQ_INIT(&submr_print_queue);

    QTAILQ_FOREACH(submr, &mr->subregions, subregions_link) {
        new_ml = g_new(MemoryRegionList, 1);
        new_ml->mr = submr;
        QTAILQ_FOREACH(ml, &submr_print_queue, queue) {
            if (new_ml->mr->addr < ml->mr->addr ||
                (new_ml->mr->addr == ml->mr->addr &&
                 new_ml->mr->priority > ml->mr->priority)) {
                QTAILQ_INSERT_BEFORE(ml, new_ml, queue);
                new_ml = NULL;
                break;
            }
        }
        if (new_ml) {
            QTAILQ_INSERT_TAIL(&submr_print_queue, new_ml, queue);
        }
    }

    QTAILQ_FOREACH(ml, &submr_print_queue, queue) {
        mtree_print_mr(mon_printf, f, ml->mr, level + 1, base + mr->addr,
                       alias_print_queue);
    }

    QTAILQ_FOREACH_SAFE(ml, &submr_print_queue, queue, next_ml) {
        g_free(ml);
    }
}

void mtree_info(fprintf_function mon_printf, void *f)
{
    MemoryRegionListHead ml_head;
    MemoryRegionList *ml, *ml2;

    QTAILQ_INIT(&ml_head);

    mon_printf(f, "memory\n");
    mtree_print_mr(mon_printf, f, address_space_memory.root, 0, 0, &ml_head);

    if (address_space_io.root &&
        !QTAILQ_EMPTY(&address_space_io.root->subregions)) {
        mon_printf(f, "I/O\n");
        mtree_print_mr(mon_printf, f, address_space_io.root, 0, 0, &ml_head);
    }

    mon_printf(f, "aliases\n");
    /* print aliased regions */
    QTAILQ_FOREACH(ml, &ml_head, queue) {
        if (!ml->printed) {
            mon_printf(f, "%s\n", ml->mr->name);
            mtree_print_mr(mon_printf, f, ml->mr, 0, 0, &ml_head);
        }
    }

    QTAILQ_FOREACH_SAFE(ml, &ml_head, queue, ml2) {
        g_free(ml);
    }
}
