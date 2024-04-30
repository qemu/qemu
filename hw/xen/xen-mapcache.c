/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"

#include <sys/resource.h>

#include "hw/xen/xen_native.h"
#include "qemu/bitmap.h"

#include "sysemu/runstate.h"
#include "sysemu/xen-mapcache.h"
#include "trace.h"


#if HOST_LONG_BITS == 32
#  define MCACHE_BUCKET_SHIFT 16
#  define MCACHE_MAX_SIZE     (1UL<<31) /* 2GB Cap */
#else
#  define MCACHE_BUCKET_SHIFT 20
#  define MCACHE_MAX_SIZE     (1UL<<35) /* 32GB Cap */
#endif
#define MCACHE_BUCKET_SIZE (1UL << MCACHE_BUCKET_SHIFT)

/* This is the size of the virtual address space reserve to QEMU that will not
 * be use by MapCache.
 * From empirical tests I observed that qemu use 75MB more than the
 * max_mcache_size.
 */
#define NON_MCACHE_MEMORY_SIZE (80 * MiB)

typedef struct MapCacheEntry {
    hwaddr paddr_index;
    uint8_t *vaddr_base;
    unsigned long *valid_mapping;
    uint32_t lock;
#define XEN_MAPCACHE_ENTRY_DUMMY (1 << 0)
    uint8_t flags;
    hwaddr size;
    struct MapCacheEntry *next;
} MapCacheEntry;

typedef struct MapCacheRev {
    uint8_t *vaddr_req;
    hwaddr paddr_index;
    hwaddr size;
    QTAILQ_ENTRY(MapCacheRev) next;
    bool dma;
} MapCacheRev;

typedef struct MapCache {
    MapCacheEntry *entry;
    unsigned long nr_buckets;
    QTAILQ_HEAD(, MapCacheRev) locked_entries;

    /* For most cases (>99.9%), the page address is the same. */
    MapCacheEntry *last_entry;
    unsigned long max_mcache_size;
    unsigned int mcache_bucket_shift;

    phys_offset_to_gaddr_t phys_offset_to_gaddr;
    QemuMutex lock;
    void *opaque;
} MapCache;

static MapCache *mapcache;

static inline void mapcache_lock(MapCache *mc)
{
    qemu_mutex_lock(&mc->lock);
}

static inline void mapcache_unlock(MapCache *mc)
{
    qemu_mutex_unlock(&mc->lock);
}

static inline int test_bits(int nr, int size, const unsigned long *addr)
{
    unsigned long res = find_next_zero_bit(addr, size + nr, nr);
    if (res >= nr + size)
        return 1;
    else
        return 0;
}

static MapCache *xen_map_cache_init_single(phys_offset_to_gaddr_t f,
                                           void *opaque,
                                           unsigned long max_size)
{
    unsigned long size;
    MapCache *mc;

    mc = g_new0(MapCache, 1);

    mc->phys_offset_to_gaddr = f;
    mc->opaque = opaque;
    qemu_mutex_init(&mc->lock);

    QTAILQ_INIT(&mc->locked_entries);

    mc->max_mcache_size = max_size;

    mc->nr_buckets =
        (((mc->max_mcache_size >> XC_PAGE_SHIFT) +
          (1UL << (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT)) - 1) >>
         (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT));

    size = mc->nr_buckets * sizeof(MapCacheEntry);
    size = (size + XC_PAGE_SIZE - 1) & ~(XC_PAGE_SIZE - 1);
    trace_xen_map_cache_init(mc->nr_buckets, size);
    mc->entry = g_malloc0(size);
    return mc;
}

void xen_map_cache_init(phys_offset_to_gaddr_t f, void *opaque)
{
    struct rlimit rlimit_as;
    unsigned long max_mcache_size;

    if (geteuid() == 0) {
        rlimit_as.rlim_cur = RLIM_INFINITY;
        rlimit_as.rlim_max = RLIM_INFINITY;
        max_mcache_size = MCACHE_MAX_SIZE;
    } else {
        getrlimit(RLIMIT_AS, &rlimit_as);
        rlimit_as.rlim_cur = rlimit_as.rlim_max;

        if (rlimit_as.rlim_max != RLIM_INFINITY) {
            warn_report("QEMU's maximum size of virtual"
                        " memory is not infinity");
        }
        if (rlimit_as.rlim_max < MCACHE_MAX_SIZE + NON_MCACHE_MEMORY_SIZE) {
            max_mcache_size = rlimit_as.rlim_max - NON_MCACHE_MEMORY_SIZE;
        } else {
            max_mcache_size = MCACHE_MAX_SIZE;
        }
    }

    mapcache = xen_map_cache_init_single(f, opaque, max_mcache_size);
    setrlimit(RLIMIT_AS, &rlimit_as);
}

static void xen_remap_bucket(MapCache *mc,
                             MapCacheEntry *entry,
                             void *vaddr,
                             hwaddr size,
                             hwaddr address_index,
                             bool dummy)
{
    uint8_t *vaddr_base;
    xen_pfn_t *pfns;
    int *err;
    unsigned int i;
    hwaddr nb_pfn = size >> XC_PAGE_SHIFT;

    trace_xen_remap_bucket(address_index);

    pfns = g_new0(xen_pfn_t, nb_pfn);
    err = g_new0(int, nb_pfn);

    if (entry->vaddr_base != NULL) {
        if (!(entry->flags & XEN_MAPCACHE_ENTRY_DUMMY)) {
            ram_block_notify_remove(entry->vaddr_base, entry->size,
                                    entry->size);
        }

        /*
         * If an entry is being replaced by another mapping and we're using
         * MAP_FIXED flag for it - there is possibility of a race for vaddr
         * address with another thread doing an mmap call itself
         * (see man 2 mmap). To avoid that we skip explicit unmapping here
         * and allow the kernel to destroy the previous mappings by replacing
         * them in mmap call later.
         *
         * Non-identical replacements are not allowed therefore.
         */
        assert(!vaddr || (entry->vaddr_base == vaddr && entry->size == size));

        if (!vaddr && munmap(entry->vaddr_base, entry->size) != 0) {
            perror("unmap fails");
            exit(-1);
        }
    }
    g_free(entry->valid_mapping);
    entry->valid_mapping = NULL;

    for (i = 0; i < nb_pfn; i++) {
        pfns[i] = (address_index << (MCACHE_BUCKET_SHIFT-XC_PAGE_SHIFT)) + i;
    }

    /*
     * If the caller has requested the mapping at a specific address use
     * MAP_FIXED to make sure it's honored.
     */
    if (!dummy) {
        vaddr_base = xenforeignmemory_map2(xen_fmem, xen_domid, vaddr,
                                           PROT_READ | PROT_WRITE,
                                           vaddr ? MAP_FIXED : 0,
                                           nb_pfn, pfns, err);
        if (vaddr_base == NULL) {
            perror("xenforeignmemory_map2");
            exit(-1);
        }
    } else {
        /*
         * We create dummy mappings where we are unable to create a foreign
         * mapping immediately due to certain circumstances (i.e. on resume now)
         */
        vaddr_base = mmap(vaddr, size, PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_SHARED | (vaddr ? MAP_FIXED : 0),
                          -1, 0);
        if (vaddr_base == MAP_FAILED) {
            perror("mmap");
            exit(-1);
        }
    }

    if (!(entry->flags & XEN_MAPCACHE_ENTRY_DUMMY)) {
        ram_block_notify_add(vaddr_base, size, size);
    }

    entry->vaddr_base = vaddr_base;
    entry->paddr_index = address_index;
    entry->size = size;
    entry->valid_mapping = g_new0(unsigned long,
                                  BITS_TO_LONGS(size >> XC_PAGE_SHIFT));

    if (dummy) {
        entry->flags |= XEN_MAPCACHE_ENTRY_DUMMY;
    } else {
        entry->flags &= ~(XEN_MAPCACHE_ENTRY_DUMMY);
    }

    bitmap_zero(entry->valid_mapping, nb_pfn);
    for (i = 0; i < nb_pfn; i++) {
        if (!err[i]) {
            bitmap_set(entry->valid_mapping, i, 1);
        }
    }

    g_free(pfns);
    g_free(err);
}

static uint8_t *xen_map_cache_unlocked(MapCache *mc,
                                       hwaddr phys_addr, hwaddr size,
                                       uint8_t lock, bool dma, bool is_write)
{
    MapCacheEntry *entry, *pentry = NULL,
                  *free_entry = NULL, *free_pentry = NULL;
    hwaddr address_index;
    hwaddr address_offset;
    hwaddr cache_size = size;
    hwaddr test_bit_size;
    bool translated G_GNUC_UNUSED = false;
    bool dummy = false;

tryagain:
    address_index  = phys_addr >> MCACHE_BUCKET_SHIFT;
    address_offset = phys_addr & (MCACHE_BUCKET_SIZE - 1);

    trace_xen_map_cache(phys_addr);

    /* test_bit_size is always a multiple of XC_PAGE_SIZE */
    if (size) {
        test_bit_size = size + (phys_addr & (XC_PAGE_SIZE - 1));

        if (test_bit_size % XC_PAGE_SIZE) {
            test_bit_size += XC_PAGE_SIZE - (test_bit_size % XC_PAGE_SIZE);
        }
    } else {
        test_bit_size = XC_PAGE_SIZE;
    }

    if (mc->last_entry != NULL &&
        mc->last_entry->paddr_index == address_index &&
        !lock && !size &&
        test_bits(address_offset >> XC_PAGE_SHIFT,
                  test_bit_size >> XC_PAGE_SHIFT,
                  mc->last_entry->valid_mapping)) {
        trace_xen_map_cache_return(
            mc->last_entry->vaddr_base + address_offset
        );
        return mc->last_entry->vaddr_base + address_offset;
    }

    /* size is always a multiple of MCACHE_BUCKET_SIZE */
    if (size) {
        cache_size = size + address_offset;
        if (cache_size % MCACHE_BUCKET_SIZE) {
            cache_size += MCACHE_BUCKET_SIZE - (cache_size % MCACHE_BUCKET_SIZE);
        }
    } else {
        cache_size = MCACHE_BUCKET_SIZE;
    }

    entry = &mc->entry[address_index % mc->nr_buckets];

    while (entry && (lock || entry->lock) && entry->vaddr_base &&
            (entry->paddr_index != address_index || entry->size != cache_size ||
             !test_bits(address_offset >> XC_PAGE_SHIFT,
                 test_bit_size >> XC_PAGE_SHIFT,
                 entry->valid_mapping))) {
        if (!free_entry && !entry->lock) {
            free_entry = entry;
            free_pentry = pentry;
        }
        pentry = entry;
        entry = entry->next;
    }
    if (!entry && free_entry) {
        entry = free_entry;
        pentry = free_pentry;
    }
    if (!entry) {
        entry = g_new0(MapCacheEntry, 1);
        pentry->next = entry;
        xen_remap_bucket(mc, entry, NULL, cache_size, address_index, dummy);
    } else if (!entry->lock) {
        if (!entry->vaddr_base || entry->paddr_index != address_index ||
                entry->size != cache_size ||
                !test_bits(address_offset >> XC_PAGE_SHIFT,
                    test_bit_size >> XC_PAGE_SHIFT,
                    entry->valid_mapping)) {
            xen_remap_bucket(mc, entry, NULL, cache_size, address_index, dummy);
        }
    }

    if(!test_bits(address_offset >> XC_PAGE_SHIFT,
                test_bit_size >> XC_PAGE_SHIFT,
                entry->valid_mapping)) {
        mc->last_entry = NULL;
#ifdef XEN_COMPAT_PHYSMAP
        if (!translated && mc->phys_offset_to_gaddr) {
            phys_addr = mc->phys_offset_to_gaddr(phys_addr, size);
            translated = true;
            goto tryagain;
        }
#endif
        if (!dummy && runstate_check(RUN_STATE_INMIGRATE)) {
            dummy = true;
            goto tryagain;
        }
        trace_xen_map_cache_return(NULL);
        return NULL;
    }

    mc->last_entry = entry;
    if (lock) {
        MapCacheRev *reventry = g_new0(MapCacheRev, 1);
        entry->lock++;
        if (entry->lock == 0) {
            error_report("mapcache entry lock overflow: "HWADDR_FMT_plx" -> %p",
                         entry->paddr_index, entry->vaddr_base);
            abort();
        }
        reventry->dma = dma;
        reventry->vaddr_req = mc->last_entry->vaddr_base + address_offset;
        reventry->paddr_index = mc->last_entry->paddr_index;
        reventry->size = entry->size;
        QTAILQ_INSERT_HEAD(&mc->locked_entries, reventry, next);
    }

    trace_xen_map_cache_return(
        mc->last_entry->vaddr_base + address_offset
    );
    return mc->last_entry->vaddr_base + address_offset;
}

uint8_t *xen_map_cache(MemoryRegion *mr,
                       hwaddr phys_addr, hwaddr size,
                       uint8_t lock, bool dma,
                       bool is_write)
{
    uint8_t *p;

    mapcache_lock(mapcache);
    p = xen_map_cache_unlocked(mapcache, phys_addr, size, lock, dma, is_write);
    mapcache_unlock(mapcache);
    return p;
}

static ram_addr_t xen_ram_addr_from_mapcache_single(MapCache *mc, void *ptr)
{
    MapCacheEntry *entry = NULL;
    MapCacheRev *reventry;
    hwaddr paddr_index;
    hwaddr size;
    ram_addr_t raddr;
    int found = 0;

    mapcache_lock(mc);
    QTAILQ_FOREACH(reventry, &mc->locked_entries, next) {
        if (reventry->vaddr_req == ptr) {
            paddr_index = reventry->paddr_index;
            size = reventry->size;
            found = 1;
            break;
        }
    }
    if (!found) {
        trace_xen_ram_addr_from_mapcache_not_found(ptr);
        mapcache_unlock(mc);
        return RAM_ADDR_INVALID;
    }

    entry = &mc->entry[paddr_index % mc->nr_buckets];
    while (entry && (entry->paddr_index != paddr_index || entry->size != size)) {
        entry = entry->next;
    }
    if (!entry) {
        trace_xen_ram_addr_from_mapcache_not_in_cache(ptr);
        raddr = RAM_ADDR_INVALID;
    } else {
        raddr = (reventry->paddr_index << MCACHE_BUCKET_SHIFT) +
             ((unsigned long) ptr - (unsigned long) entry->vaddr_base);
    }
    mapcache_unlock(mc);
    return raddr;
}

ram_addr_t xen_ram_addr_from_mapcache(void *ptr)
{
    return xen_ram_addr_from_mapcache_single(mapcache, ptr);
}

static void xen_invalidate_map_cache_entry_unlocked(MapCache *mc,
                                                    uint8_t *buffer)
{
    MapCacheEntry *entry = NULL, *pentry = NULL;
    MapCacheRev *reventry;
    hwaddr paddr_index;
    hwaddr size;
    int found = 0;

    QTAILQ_FOREACH(reventry, &mc->locked_entries, next) {
        if (reventry->vaddr_req == buffer) {
            paddr_index = reventry->paddr_index;
            size = reventry->size;
            found = 1;
            break;
        }
    }
    if (!found) {
        trace_xen_invalidate_map_cache_entry_unlocked_not_found(buffer);
        QTAILQ_FOREACH(reventry, &mc->locked_entries, next) {
            trace_xen_invalidate_map_cache_entry_unlocked_found(
                reventry->paddr_index,
                reventry->vaddr_req
            );
        }
        return;
    }
    QTAILQ_REMOVE(&mc->locked_entries, reventry, next);
    g_free(reventry);

    if (mc->last_entry != NULL &&
        mc->last_entry->paddr_index == paddr_index) {
        mc->last_entry = NULL;
    }

    entry = &mc->entry[paddr_index % mc->nr_buckets];
    while (entry && (entry->paddr_index != paddr_index || entry->size != size)) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        trace_xen_invalidate_map_cache_entry_unlocked_miss(buffer);
        return;
    }
    entry->lock--;
    if (entry->lock > 0 || pentry == NULL) {
        return;
    }

    pentry->next = entry->next;
    ram_block_notify_remove(entry->vaddr_base, entry->size, entry->size);
    if (munmap(entry->vaddr_base, entry->size) != 0) {
        perror("unmap fails");
        exit(-1);
    }
    g_free(entry->valid_mapping);
    g_free(entry);
}

typedef struct XenMapCacheData {
    Coroutine *co;
    uint8_t *buffer;
} XenMapCacheData;

static void xen_invalidate_map_cache_entry_bh(void *opaque)
{
    XenMapCacheData *data = opaque;

    mapcache_lock(mapcache);
    xen_invalidate_map_cache_entry_unlocked(mapcache, data->buffer);
    mapcache_unlock(mapcache);

    aio_co_wake(data->co);
}

void coroutine_mixed_fn xen_invalidate_map_cache_entry(uint8_t *buffer)
{
    if (qemu_in_coroutine()) {
        XenMapCacheData data = {
            .co = qemu_coroutine_self(),
            .buffer = buffer,
        };
        aio_bh_schedule_oneshot(qemu_get_current_aio_context(),
                                xen_invalidate_map_cache_entry_bh, &data);
        qemu_coroutine_yield();
    } else {
        mapcache_lock(mapcache);
        xen_invalidate_map_cache_entry_unlocked(mapcache, buffer);
        mapcache_unlock(mapcache);
    }
}

static void xen_invalidate_map_cache_single(MapCache *mc)
{
    unsigned long i;
    MapCacheRev *reventry;

    mapcache_lock(mc);

    QTAILQ_FOREACH(reventry, &mc->locked_entries, next) {
        if (!reventry->dma) {
            continue;
        }
        trace_xen_invalidate_map_cache(reventry->paddr_index,
                                       reventry->vaddr_req);
    }

    for (i = 0; i < mc->nr_buckets; i++) {
        MapCacheEntry *entry = &mc->entry[i];

        if (entry->vaddr_base == NULL) {
            continue;
        }
        if (entry->lock > 0) {
            continue;
        }

        if (munmap(entry->vaddr_base, entry->size) != 0) {
            perror("unmap fails");
            exit(-1);
        }

        entry->paddr_index = 0;
        entry->vaddr_base = NULL;
        entry->size = 0;
        g_free(entry->valid_mapping);
        entry->valid_mapping = NULL;
    }

    mc->last_entry = NULL;

    mapcache_unlock(mc);
}

void xen_invalidate_map_cache(void)
{
    /* Flush pending AIO before destroying the mapcache */
    bdrv_drain_all();

    xen_invalidate_map_cache_single(mapcache);
}

static uint8_t *xen_replace_cache_entry_unlocked(MapCache *mc,
                                                 hwaddr old_phys_addr,
                                                 hwaddr new_phys_addr,
                                                 hwaddr size)
{
    MapCacheEntry *entry;
    hwaddr address_index, address_offset;
    hwaddr test_bit_size, cache_size = size;

    address_index  = old_phys_addr >> MCACHE_BUCKET_SHIFT;
    address_offset = old_phys_addr & (MCACHE_BUCKET_SIZE - 1);

    assert(size);
    /* test_bit_size is always a multiple of XC_PAGE_SIZE */
    test_bit_size = size + (old_phys_addr & (XC_PAGE_SIZE - 1));
    if (test_bit_size % XC_PAGE_SIZE) {
        test_bit_size += XC_PAGE_SIZE - (test_bit_size % XC_PAGE_SIZE);
    }
    cache_size = size + address_offset;
    if (cache_size % MCACHE_BUCKET_SIZE) {
        cache_size += MCACHE_BUCKET_SIZE - (cache_size % MCACHE_BUCKET_SIZE);
    }

    entry = &mc->entry[address_index % mc->nr_buckets];
    while (entry && !(entry->paddr_index == address_index &&
                      entry->size == cache_size)) {
        entry = entry->next;
    }
    if (!entry) {
        trace_xen_replace_cache_entry_unlocked(old_phys_addr);
        return NULL;
    }

    address_index  = new_phys_addr >> MCACHE_BUCKET_SHIFT;
    address_offset = new_phys_addr & (MCACHE_BUCKET_SIZE - 1);

    trace_xen_replace_cache_entry_dummy(old_phys_addr, new_phys_addr);

    xen_remap_bucket(mc, entry, entry->vaddr_base,
                     cache_size, address_index, false);
    if (!test_bits(address_offset >> XC_PAGE_SHIFT,
                test_bit_size >> XC_PAGE_SHIFT,
                entry->valid_mapping)) {
        trace_xen_replace_cache_entry_unlocked_could_not_update_entry(
            old_phys_addr
        );
        return NULL;
    }

    return entry->vaddr_base + address_offset;
}

uint8_t *xen_replace_cache_entry(hwaddr old_phys_addr,
                                 hwaddr new_phys_addr,
                                 hwaddr size)
{
    uint8_t *p;

    mapcache_lock(mapcache);
    p = xen_replace_cache_entry_unlocked(mapcache, old_phys_addr,
                                         new_phys_addr, size);
    mapcache_unlock(mapcache);
    return p;
}
