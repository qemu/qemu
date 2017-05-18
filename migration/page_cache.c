/*
 * Page cache for QEMU
 * The cache is base on a hash of the page address
 *
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Orit Wasserman  <owasserm@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qemu/host-utils.h"
#include "migration/page_cache.h"

#ifdef DEBUG_CACHE
#define DPRINTF(fmt, ...) \
    do { fprintf(stdout, "cache: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/* the page in cache will not be replaced in two cycles */
#define CACHED_PAGE_LIFETIME 2

typedef struct CacheItem CacheItem;

struct CacheItem {
    uint64_t it_addr;
    uint64_t it_age;
    uint8_t *it_data;
};

struct PageCache {
    CacheItem *page_cache;
    unsigned int page_size;
    int64_t max_num_items;
    uint64_t max_item_age;
    int64_t num_items;
};

PageCache *cache_init(int64_t num_pages, unsigned int page_size)
{
    int64_t i;

    PageCache *cache;

    if (num_pages <= 0) {
        DPRINTF("invalid number of pages\n");
        return NULL;
    }

    /* We prefer not to abort if there is no memory */
    cache = g_try_malloc(sizeof(*cache));
    if (!cache) {
        DPRINTF("Failed to allocate cache\n");
        return NULL;
    }
    /* round down to the nearest power of 2 */
    if (!is_power_of_2(num_pages)) {
        num_pages = pow2floor(num_pages);
        DPRINTF("rounding down to %" PRId64 "\n", num_pages);
    }
    cache->page_size = page_size;
    cache->num_items = 0;
    cache->max_item_age = 0;
    cache->max_num_items = num_pages;

    DPRINTF("Setting cache buckets to %" PRId64 "\n", cache->max_num_items);

    /* We prefer not to abort if there is no memory */
    cache->page_cache = g_try_malloc((cache->max_num_items) *
                                     sizeof(*cache->page_cache));
    if (!cache->page_cache) {
        DPRINTF("Failed to allocate cache->page_cache\n");
        g_free(cache);
        return NULL;
    }

    for (i = 0; i < cache->max_num_items; i++) {
        cache->page_cache[i].it_data = NULL;
        cache->page_cache[i].it_age = 0;
        cache->page_cache[i].it_addr = -1;
    }

    return cache;
}

void cache_fini(PageCache *cache)
{
    int64_t i;

    g_assert(cache);
    g_assert(cache->page_cache);

    for (i = 0; i < cache->max_num_items; i++) {
        g_free(cache->page_cache[i].it_data);
    }

    g_free(cache->page_cache);
    cache->page_cache = NULL;
    g_free(cache);
}

static size_t cache_get_cache_pos(const PageCache *cache,
                                  uint64_t address)
{
    g_assert(cache->max_num_items);
    return (address / cache->page_size) & (cache->max_num_items - 1);
}

static CacheItem *cache_get_by_addr(const PageCache *cache, uint64_t addr)
{
    size_t pos;

    g_assert(cache);
    g_assert(cache->page_cache);

    pos = cache_get_cache_pos(cache, addr);

    return &cache->page_cache[pos];
}

uint8_t *get_cached_data(const PageCache *cache, uint64_t addr)
{
    return cache_get_by_addr(cache, addr)->it_data;
}

bool cache_is_cached(const PageCache *cache, uint64_t addr,
                     uint64_t current_age)
{
    CacheItem *it;

    it = cache_get_by_addr(cache, addr);

    if (it->it_addr == addr) {
        /* update the it_age when the cache hit */
        it->it_age = current_age;
        return true;
    }
    return false;
}

int cache_insert(PageCache *cache, uint64_t addr, const uint8_t *pdata,
                 uint64_t current_age)
{

    CacheItem *it;

    /* actual update of entry */
    it = cache_get_by_addr(cache, addr);

    if (it->it_data && it->it_addr != addr &&
        it->it_age + CACHED_PAGE_LIFETIME > current_age) {
        /* the cache page is fresh, don't replace it */
        return -1;
    }
    /* allocate page */
    if (!it->it_data) {
        it->it_data = g_try_malloc(cache->page_size);
        if (!it->it_data) {
            DPRINTF("Error allocating page\n");
            return -1;
        }
        cache->num_items++;
    }

    memcpy(it->it_data, pdata, cache->page_size);

    it->it_age = current_age;
    it->it_addr = addr;

    return 0;
}

int64_t cache_resize(PageCache *cache, int64_t new_num_pages)
{
    PageCache *new_cache;
    int64_t i;

    CacheItem *old_it, *new_it;

    g_assert(cache);

    /* cache was not inited */
    if (cache->page_cache == NULL) {
        return -1;
    }

    /* same size */
    if (pow2floor(new_num_pages) == cache->max_num_items) {
        return cache->max_num_items;
    }

    new_cache = cache_init(new_num_pages, cache->page_size);
    if (!(new_cache)) {
        DPRINTF("Error creating new cache\n");
        return -1;
    }

    /* move all data from old cache */
    for (i = 0; i < cache->max_num_items; i++) {
        old_it = &cache->page_cache[i];
        if (old_it->it_addr != -1) {
            /* check for collision, if there is, keep MRU page */
            new_it = cache_get_by_addr(new_cache, old_it->it_addr);
            if (new_it->it_data && new_it->it_age >= old_it->it_age) {
                /* keep the MRU page */
                g_free(old_it->it_data);
            } else {
                if (!new_it->it_data) {
                    new_cache->num_items++;
                }
                g_free(new_it->it_data);
                new_it->it_data = old_it->it_data;
                new_it->it_age = old_it->it_age;
                new_it->it_addr = old_it->it_addr;
            }
        }
    }

    g_free(cache->page_cache);
    cache->page_cache = new_cache->page_cache;
    cache->max_num_items = new_cache->max_num_items;
    cache->num_items = new_cache->num_items;

    g_free(new_cache);

    return cache->max_num_items;
}
