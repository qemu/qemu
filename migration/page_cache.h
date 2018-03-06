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

#ifndef PAGE_CACHE_H
#define PAGE_CACHE_H

/* Page cache for storing guest pages */
typedef struct PageCache PageCache;

/**
 * cache_init: Initialize the page cache
 *
 *
 * Returns new allocated cache or NULL on error
 *
 * @cache_size: cache size in bytes
 * @page_size: cache page size
 * @errp: set *errp if the check failed, with reason
 */
PageCache *cache_init(int64_t cache_size, size_t page_size, Error **errp);
/**
 * cache_fini: free all cache resources
 * @cache pointer to the PageCache struct
 */
void cache_fini(PageCache *cache);

/**
 * cache_is_cached: Checks to see if the page is cached
 *
 * Returns %true if page is cached
 *
 * @cache pointer to the PageCache struct
 * @addr: page addr
 * @current_age: current bitmap generation
 */
bool cache_is_cached(const PageCache *cache, uint64_t addr,
                     uint64_t current_age);

/**
 * get_cached_data: Get the data cached for an addr
 *
 * Returns pointer to the data cached or NULL if not cached
 *
 * @cache pointer to the PageCache struct
 * @addr: page addr
 */
uint8_t *get_cached_data(const PageCache *cache, uint64_t addr);

/**
 * cache_insert: insert the page into the cache. the page cache
 * will dup the data on insert. the previous value will be overwritten
 *
 * Returns -1 when the page isn't inserted into cache
 *
 * @cache pointer to the PageCache struct
 * @addr: page address
 * @pdata: pointer to the page
 * @current_age: current bitmap generation
 */
int cache_insert(PageCache *cache, uint64_t addr, const uint8_t *pdata,
                 uint64_t current_age);

#endif
