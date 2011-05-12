/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "config.h"

#include <sys/resource.h>

#include "hw/xen_backend.h"
#include "blockdev.h"
#include "bitmap.h"

#include <xen/hvm/params.h>
#include <sys/mman.h>

#include "xen-mapcache.h"
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

typedef struct MapCacheEntry {
    target_phys_addr_t paddr_index;
    uint8_t *vaddr_base;
    DECLARE_BITMAP(valid_mapping, MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT);
    uint8_t lock;
    struct MapCacheEntry *next;
} MapCacheEntry;

typedef struct MapCacheRev {
    uint8_t *vaddr_req;
    target_phys_addr_t paddr_index;
    QTAILQ_ENTRY(MapCacheRev) next;
} MapCacheRev;

typedef struct MapCache {
    MapCacheEntry *entry;
    unsigned long nr_buckets;
    QTAILQ_HEAD(map_cache_head, MapCacheRev) locked_entries;

    /* For most cases (>99.9%), the page address is the same. */
    target_phys_addr_t last_address_index;
    uint8_t *last_address_vaddr;
    unsigned long max_mcache_size;
    unsigned int mcache_bucket_shift;
} MapCache;

static MapCache *mapcache;

void qemu_map_cache_init(void)
{
    unsigned long size;
    struct rlimit rlimit_as;

    mapcache = qemu_mallocz(sizeof (MapCache));

    QTAILQ_INIT(&mapcache->locked_entries);
    mapcache->last_address_index = -1;

    getrlimit(RLIMIT_AS, &rlimit_as);
    if (rlimit_as.rlim_max < MCACHE_MAX_SIZE) {
        rlimit_as.rlim_cur = rlimit_as.rlim_max;
    } else {
        rlimit_as.rlim_cur = MCACHE_MAX_SIZE;
    }

    setrlimit(RLIMIT_AS, &rlimit_as);
    mapcache->max_mcache_size = rlimit_as.rlim_cur;

    mapcache->nr_buckets =
        (((mapcache->max_mcache_size >> XC_PAGE_SHIFT) +
          (1UL << (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT)) - 1) >>
         (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT));

    size = mapcache->nr_buckets * sizeof (MapCacheEntry);
    size = (size + XC_PAGE_SIZE - 1) & ~(XC_PAGE_SIZE - 1);
    DPRINTF("qemu_map_cache_init, nr_buckets = %lx size %lu\n", mapcache->nr_buckets, size);
    mapcache->entry = qemu_mallocz(size);
}

static void qemu_remap_bucket(MapCacheEntry *entry,
                              target_phys_addr_t size,
                              target_phys_addr_t address_index)
{
    uint8_t *vaddr_base;
    xen_pfn_t *pfns;
    int *err;
    unsigned int i;
    target_phys_addr_t nb_pfn = size >> XC_PAGE_SHIFT;

    trace_qemu_remap_bucket(address_index);

    pfns = qemu_mallocz(nb_pfn * sizeof (xen_pfn_t));
    err = qemu_mallocz(nb_pfn * sizeof (int));

    if (entry->vaddr_base != NULL) {
        if (munmap(entry->vaddr_base, size) != 0) {
            perror("unmap fails");
            exit(-1);
        }
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

    bitmap_zero(entry->valid_mapping, nb_pfn);
    for (i = 0; i < nb_pfn; i++) {
        if (!err[i]) {
            bitmap_set(entry->valid_mapping, i, 1);
        }
    }

    qemu_free(pfns);
    qemu_free(err);
}

uint8_t *qemu_map_cache(target_phys_addr_t phys_addr, target_phys_addr_t size, uint8_t lock)
{
    MapCacheEntry *entry, *pentry = NULL;
    target_phys_addr_t address_index  = phys_addr >> MCACHE_BUCKET_SHIFT;
    target_phys_addr_t address_offset = phys_addr & (MCACHE_BUCKET_SIZE - 1);

    trace_qemu_map_cache(phys_addr);

    if (address_index == mapcache->last_address_index && !lock) {
        trace_qemu_map_cache_return(mapcache->last_address_vaddr + address_offset);
        return mapcache->last_address_vaddr + address_offset;
    }

    entry = &mapcache->entry[address_index % mapcache->nr_buckets];

    while (entry && entry->lock && entry->paddr_index != address_index && entry->vaddr_base) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        entry = qemu_mallocz(sizeof (MapCacheEntry));
        pentry->next = entry;
        qemu_remap_bucket(entry, size ? : MCACHE_BUCKET_SIZE, address_index);
    } else if (!entry->lock) {
        if (!entry->vaddr_base || entry->paddr_index != address_index ||
            !test_bit(address_offset >> XC_PAGE_SHIFT, entry->valid_mapping)) {
            qemu_remap_bucket(entry, size ? : MCACHE_BUCKET_SIZE, address_index);
        }
    }

    if (!test_bit(address_offset >> XC_PAGE_SHIFT, entry->valid_mapping)) {
        mapcache->last_address_index = -1;
        trace_qemu_map_cache_return(NULL);
        return NULL;
    }

    mapcache->last_address_index = address_index;
    mapcache->last_address_vaddr = entry->vaddr_base;
    if (lock) {
        MapCacheRev *reventry = qemu_mallocz(sizeof(MapCacheRev));
        entry->lock++;
        reventry->vaddr_req = mapcache->last_address_vaddr + address_offset;
        reventry->paddr_index = mapcache->last_address_index;
        QTAILQ_INSERT_HEAD(&mapcache->locked_entries, reventry, next);
    }

    trace_qemu_map_cache_return(mapcache->last_address_vaddr + address_offset);
    return mapcache->last_address_vaddr + address_offset;
}

void qemu_map_cache_unlock(void *buffer)
{
    MapCacheEntry *entry = NULL, *pentry = NULL;
    MapCacheRev *reventry;
    target_phys_addr_t paddr_index;
    int found = 0;

    QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
        if (reventry->vaddr_req == buffer) {
            paddr_index = reventry->paddr_index;
            found = 1;
            break;
        }
    }
    if (!found) {
        return;
    }
    QTAILQ_REMOVE(&mapcache->locked_entries, reventry, next);
    qemu_free(reventry);

    entry = &mapcache->entry[paddr_index % mapcache->nr_buckets];
    while (entry && entry->paddr_index != paddr_index) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        return;
    }
    if (entry->lock > 0) {
        entry->lock--;
    }
}

ram_addr_t qemu_ram_addr_from_mapcache(void *ptr)
{
    MapCacheRev *reventry;
    target_phys_addr_t paddr_index;
    int found = 0;

    QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
        if (reventry->vaddr_req == ptr) {
            paddr_index = reventry->paddr_index;
            found = 1;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "qemu_ram_addr_from_mapcache, could not find %p\n", ptr);
        QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
            DPRINTF("   "TARGET_FMT_plx" -> %p is present\n", reventry->paddr_index,
                    reventry->vaddr_req);
        }
        abort();
        return 0;
    }

    return paddr_index << MCACHE_BUCKET_SHIFT;
}

void qemu_invalidate_entry(uint8_t *buffer)
{
    MapCacheEntry *entry = NULL, *pentry = NULL;
    MapCacheRev *reventry;
    target_phys_addr_t paddr_index;
    int found = 0;

    if (mapcache->last_address_vaddr == buffer) {
        mapcache->last_address_index = -1;
    }

    QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
        if (reventry->vaddr_req == buffer) {
            paddr_index = reventry->paddr_index;
            found = 1;
            break;
        }
    }
    if (!found) {
        DPRINTF("qemu_invalidate_entry, could not find %p\n", buffer);
        QTAILQ_FOREACH(reventry, &mapcache->locked_entries, next) {
            DPRINTF("   "TARGET_FMT_plx" -> %p is present\n", reventry->paddr_index, reventry->vaddr_req);
        }
        return;
    }
    QTAILQ_REMOVE(&mapcache->locked_entries, reventry, next);
    qemu_free(reventry);

    entry = &mapcache->entry[paddr_index % mapcache->nr_buckets];
    while (entry && entry->paddr_index != paddr_index) {
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
    if (munmap(entry->vaddr_base, MCACHE_BUCKET_SIZE) != 0) {
        perror("unmap fails");
        exit(-1);
    }
    qemu_free(entry);
}

void qemu_invalidate_map_cache(void)
{
    unsigned long i;
    MapCacheRev *reventry;

    /* Flush pending AIO before destroying the mapcache */
    qemu_aio_flush();

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

        if (munmap(entry->vaddr_base, MCACHE_BUCKET_SIZE) != 0) {
            perror("unmap fails");
            exit(-1);
        }

        entry->paddr_index = 0;
        entry->vaddr_base = NULL;
    }

    mapcache->last_address_index = -1;
    mapcache->last_address_vaddr = NULL;

    mapcache_unlock();
}

uint8_t *xen_map_block(target_phys_addr_t phys_addr, target_phys_addr_t size)
{
    uint8_t *vaddr_base;
    xen_pfn_t *pfns;
    int *err;
    unsigned int i;
    target_phys_addr_t nb_pfn = size >> XC_PAGE_SHIFT;

    trace_xen_map_block(phys_addr, size);
    phys_addr >>= XC_PAGE_SHIFT;

    pfns = qemu_mallocz(nb_pfn * sizeof (xen_pfn_t));
    err = qemu_mallocz(nb_pfn * sizeof (int));

    for (i = 0; i < nb_pfn; i++) {
        pfns[i] = phys_addr + i;
    }

    vaddr_base = xc_map_foreign_bulk(xen_xc, xen_domid, PROT_READ|PROT_WRITE,
                                     pfns, err, nb_pfn);
    if (vaddr_base == NULL) {
        perror("xc_map_foreign_bulk");
        exit(-1);
    }

    qemu_free(pfns);
    qemu_free(err);

    return vaddr_base;
}
