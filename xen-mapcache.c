/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "config.h"

#include <sys/resource.h>

#include "hw/xen/xen_backend.h"
#include "sysemu/blockdev.h"
#include "qemu/bitmap.h"

#include <xen/hvm/params.h>
#include <sys/mman.h>

#include "sysemu/xen-mapcache.h"
#include "trace.h"


//#define MAPCACHE_DEBUG

#ifdef MAPCACHE_DEBUG
#  define DPRINTF(fmt, ...) do { \
    fprintf(stderr, "xen_mapcache: " fmt, ## __VA_ARGS__); \
} while (0)
#else
#  define DPRINTF(fmt, ...) do { } while (0)
#endif

#if defined(__i386__)
#  define MCACHE_BUCKET_SHIFT 16
#  define MCACHE_MAX_SIZE     (1UL<<31) /* 2GB Cap */
#elif defined(__x86_64__)
#  define MCACHE_BUCKET_SHIFT 20
#  define MCACHE_MAX_SIZE     (1UL<<35) /* 32GB Cap */
#endif
#define MCACHE_BUCKET_SIZE (1UL << MCACHE_BUCKET_SHIFT)

/* This is the size of the virtual address space reserve to QEMU that will not
 * be use by MapCache.
 * From empirical tests I observed that qemu use 75MB more than the
 * max_mcache_size.
 */
#define NON_MCACHE_MEMORY_SIZE (80 * 1024 * 1024)

#define mapcache_lock()   ((void)0)
#define mapcache_unlock() ((void)0)

typedef struct MapCacheEntry {
    hwaddr paddr_index;
    uint8_t *vaddr_base;
    unsigned long *valid_mapping;
    uint8_t lock;
    hwaddr size;
    struct MapCacheEntry *next;
} MapCacheEntry;

typedef struct MapCacheRev {
    uint8_t *vaddr_req;
    hwaddr paddr_index;
    hwaddr size;
    QTAILQ_ENTRY(MapCacheRev) next;
} MapCacheRev;

typedef struct MapCache {
    MapCacheEntry *entry;
    unsigned long nr_buckets;
    QTAILQ_HEAD(map_cache_head, MapCacheRev) locked_entries;

    /* For most cases (>99.9%), the page address is the same. */
    MapCacheEntry *last_entry;
    unsigned long max_mcache_size;
    unsigned int mcache_bucket_shift;

    phys_offset_to_gaddr_t phys_offset_to_gaddr;
    void *opaque;
} MapCache;

static MapCache *mapcache;

static inline int test_bits(int nr, int size, const unsigned long *addr)
{
    unsigned long res = find_next_zero_bit(addr, size + nr, nr);
    if (res >= nr + size)
        return 1;
    else
        return 0;
}

void xen_map_cache_init(phys_offset_to_gaddr_t f, void *opaque)
{
    unsigned long size;
    struct rlimit rlimit_as;

    mapcache = g_malloc0(sizeof (MapCache));

    mapcache->phys_offset_to_gaddr = f;
    mapcache->opaque = opaque;

    QTAILQ_INIT(&mapcache->locked_entries);

    if (geteuid() == 0) {
        rlimit_as.rlim_cur = RLIM_INFINITY;
        rlimit_as.rlim_max = RLIM_INFINITY;
        mapcache->max_mcache_size = MCACHE_MAX_SIZE;
    } else {
        getrlimit(RLIMIT_AS, &rlimit_as);
        rlimit_as.rlim_cur = rlimit_as.rlim_max;

        if (rlimit_as.rlim_max != RLIM_INFINITY) {
            fprintf(stderr, "Warning: QEMU's maximum size of virtual"
                    " memory is not infinity.\n");
        }
        if (rlimit_as.rlim_max < MCACHE_MAX_SIZE + NON_MCACHE_MEMORY_SIZE) {
            mapcache->max_mcache_size = rlimit_as.rlim_max -
                NON_MCACHE_MEMORY_SIZE;
        } else {
            mapcache->max_mcache_size = MCACHE_MAX_SIZE;
        }
    }

    setrlimit(RLIMIT_AS, &rlimit_as);

    mapcache->nr_buckets =
        (((mapcache->max_mcache_size >> XC_PAGE_SHIFT) +
          (1UL << (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT)) - 1) >>
         (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT));

    size = mapcache->nr_buckets * sizeof (MapCacheEntry);
    size = (size + XC_PAGE_SIZE - 1) & ~(XC_PAGE_SIZE - 1);
    DPRINTF("%s, nr_buckets = %lx size %lu\n", __func__,
            mapcache->nr_buckets, size);
    mapcache->entry = g_malloc0(size);
}

static void xen_remap_bucket(MapCacheEntry *entry,
                             hwaddr size,
                             hwaddr address_index)
{
    uint8_t *vaddr_base;
    xen_pfn_t *pfns;
    int *err;
    unsigned int i;
    hwaddr nb_pfn = size >> XC_PAGE_SHIFT;

    trace_xen_remap_bucket(address_index);

    pfns = g_malloc0(nb_pfn * sizeof (xen_pfn_t));
    err = g_malloc0(nb_pfn * sizeof (int));

    if (entry->vaddr_base != NULL) {
        if (munmap(entry->vaddr_base, entry->size) != 0) {
            perror("unmap fails");
            exit(-1);
        }
    }
    if (entry->valid_mapping != NULL) {
        g_free(entry->valid_mapping);
        entry->valid_mapping = NULL;
    }

    for (i = 0; i < nb_pfn; i++) {
        pfns[i] = (address_index << (MCACHE_BUCKET_SHIFT-XC_PAGE_SHIFT)) + i;
    }

    vaddr_base = xc_map_foreign_bulk(xen_xc, xen_domid, PROT_READ|PROT_WRITE,
                                     pfns, err, nb_pfn);
    if (vaddr_base == NULL) {
        perror("xc_map_foreign_bulk");
        exit(-1);
    }

    entry->vaddr_base = vaddr_base;
    entry->paddr_index = address_index;
    entry->size = size;
    entry->valid_mapping = (unsigned long *) g_malloc0(sizeof(unsigned long) *
            BITS_TO_LONGS(size >> XC_PAGE_SHIFT));

    bitmap_zero(entry->valid_mapping, nb_pfn);
    for (i = 0; i < nb_pfn; i++) {
        if (!err[i]) {
            bitmap_set(entry->valid_mapping, i, 1);
        }
    }

    g_free(pfns);
    g_free(err);
}

uint8_t *xen_map_cache(hwaddr phys_addr, hwaddr size,
                       uint8_t lock)
{
    MapCacheEntry *entry, *pentry = NULL;
    hwaddr address_index;
    hwaddr address_offset;
    hwaddr __size = size;
    hwaddr __test_bit_size = size;
    bool translated = false;

tryagain:
    address_index  = phys_addr >> MCACHE_BUCKET_SHIFT;
    address_offset = phys_addr & (MCACHE_BUCKET_SIZE - 1);

    trace_xen_map_cache(phys_addr);

    /* __test_bit_size is always a multiple of XC_PAGE_SIZE */
    if (size) {
        __test_bit_size = size + (phys_addr & (XC_PAGE_SIZE - 1));

        if (__test_bit_size % XC_PAGE_SIZE) {
            __test_bit_size += XC_PAGE_SIZE - (__test_bit_size % XC_PAGE_SIZE);
        }
    } else {
        __test_bit_size = XC_PAGE_SIZE;
    }

    if (mapcache->last_entry != NULL &&
        mapcache->last_entry->paddr_index == address_index &&
        !lock && !__size &&
        test_bits(address_offset >> XC_PAGE_SHIFT,
                  __test_bit_size >> XC_PAGE_SHIFT,
                  mapcache->last_entry->valid_mapping)) {
        trace_xen_map_cache_return(mapcache->last_entry->vaddr_base + address_offset);
        return mapcache->last_entry->vaddr_base + address_offset;
    }

    /* size is always a multiple of MCACHE_BUCKET_SIZE */
    if (size) {
        __size = size + address_offset;
        if (__size % MCACHE_BUCKET_SIZE) {
            __size += MCACHE_BUCKET_SIZE - (__size % MCACHE_BUCKET_SIZE);
        }
    } else {
        __size = MCACHE_BUCKET_SIZE;
    }

    entry = &mapcache->entry[address_index % mapcache->nr_buckets];

    while (entry && entry->lock && entry->vaddr_base &&
            (entry->paddr_index != address_index || entry->size != __size ||
             !test_bits(address_offset >> XC_PAGE_SHIFT,
                 __test_bit_size >> XC_PAGE_SHIFT,
                 entry->valid_mapping))) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        entry = g_malloc0(sizeof (MapCacheEntry));
        pentry->next = entry;
        xen_remap_bucket(entry, __size, address_index);
    } else if (!entry->lock) {
        if (!entry->vaddr_base || entry->paddr_index != address_index ||
                entry->size != __size ||
                !test_bits(address_offset >> XC_PAGE_SHIFT,
                    __test_bit_size >> XC_PAGE_SHIFT,
                    entry->valid_mapping)) {
            xen_remap_bucket(entry, __size, address_index);
        }
    }

    if(!test_bits(address_offset >> XC_PAGE_SHIFT,
                __test_bit_size >> XC_PAGE_SHIFT,
                entry->valid_mapping)) {
        mapcache->last_entry = NULL;
        if (!translated && mapcache->phys_offset_to_gaddr) {
            phys_addr = mapcache->phys_offset_to_gaddr(phys_addr, size, mapcache->opaque);
            translated = true;
            goto tryagain;
        }
        trace_xen_map_cache_return(NULL);
        return NULL;
    }

    mapcache->last_entry = entry;
    if (lock) {
        MapCacheRev *reventry = g_malloc0(sizeof(MapCacheRev));
        entry->lock++;
        reventry->vaddr_req = mapcache->last_entry->vaddr_base + address_offset;
        reventry->paddr_index = mapcache->last_entry->paddr_index;
        reventry->size = entry->size;
        QTAILQ_INSERT_HEAD(&mapcache->locked_entries, reventry, next);
    }

    trace_xen_map_cache_return(mapcache->last_entry->vaddr_base + address_offset);
    return mapcache->last_entry->vaddr_base + address_offset;
}

ram_addr_t xen_ram_addr_from_mapcache(void *ptr)
{
    MapCacheEntry *entry = NULL;
    MapCacheRev *reventry;
    hwaddr paddr_index;
    hwaddr size;
    int found = 0;

    QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
        if (reventry->vaddr_req == ptr) {
            paddr_index = reventry->paddr_index;
            size = reventry->size;
            found = 1;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "%s, could not find %p\n", __func__, ptr);
        QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
            DPRINTF("   "TARGET_FMT_plx" -> %p is present\n", reventry->paddr_index,
                    reventry->vaddr_req);
        }
        abort();
        return 0;
    }

    entry = &mapcache->entry[paddr_index % mapcache->nr_buckets];
    while (entry && (entry->paddr_index != paddr_index || entry->size != size)) {
        entry = entry->next;
    }
    if (!entry) {
        DPRINTF("Trying to find address %p that is not in the mapcache!\n", ptr);
        return 0;
    }
    return (reventry->paddr_index << MCACHE_BUCKET_SHIFT) +
        ((unsigned long) ptr - (unsigned long) entry->vaddr_base);
}

void xen_invalidate_map_cache_entry(uint8_t *buffer)
{
    MapCacheEntry *entry = NULL, *pentry = NULL;
    MapCacheRev *reventry;
    hwaddr paddr_index;
    hwaddr size;
    int found = 0;

    QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
        if (reventry->vaddr_req == buffer) {
            paddr_index = reventry->paddr_index;
            size = reventry->size;
            found = 1;
            break;
        }
    }
    if (!found) {
        DPRINTF("%s, could not find %p\n", __func__, buffer);
        QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
            DPRINTF("   "TARGET_FMT_plx" -> %p is present\n", reventry->paddr_index, reventry->vaddr_req);
        }
        return;
    }
    QTAILQ_REMOVE(&mapcache->locked_entries, reventry, next);
    g_free(reventry);

    if (mapcache->last_entry != NULL &&
        mapcache->last_entry->paddr_index == paddr_index) {
        mapcache->last_entry = NULL;
    }

    entry = &mapcache->entry[paddr_index % mapcache->nr_buckets];
    while (entry && (entry->paddr_index != paddr_index || entry->size != size)) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        DPRINTF("Trying to unmap address %p that is not in the mapcache!\n", buffer);
        return;
    }
    entry->lock--;
    if (entry->lock > 0 || pentry == NULL) {
        return;
    }

    pentry->next = entry->next;
    if (munmap(entry->vaddr_base, entry->size) != 0) {
        perror("unmap fails");
        exit(-1);
    }
    g_free(entry->valid_mapping);
    g_free(entry);
}

void xen_invalidate_map_cache(void)
{
    unsigned long i;
    MapCacheRev *reventry;

    /* Flush pending AIO before destroying the mapcache */
    bdrv_drain_all();

    QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
        DPRINTF("There should be no locked mappings at this time, "
                "but "TARGET_FMT_plx" -> %p is present\n",
                reventry->paddr_index, reventry->vaddr_req);
    }

    mapcache_lock();

    for (i = 0; i < mapcache->nr_buckets; i++) {
        MapCacheEntry *entry = &mapcache->entry[i];

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

    mapcache->last_entry = NULL;

    mapcache_unlock();
}
