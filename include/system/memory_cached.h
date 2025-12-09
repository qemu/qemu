/*
 * Physical memory management API
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM_MEMORY_CACHED_H
#define SYSTEM_MEMORY_CACHED_H

#include "exec/hwaddr.h"
#include "system/memory.h"

struct MemoryRegionCache {
    uint8_t *ptr;
    hwaddr xlat;
    hwaddr len;
    FlatView *fv;
    MemoryRegionSection mrs;
    bool is_write;
};

/**
 * address_space_ld*_cached: load from a cached #MemoryRegion
 * address_space_st*_cached: store into a cached #MemoryRegion
 *
 * These functions perform a load or store of the byte, word,
 * longword or quad to the specified address.  The address is
 * a physical address in the AddressSpace, but it must lie within
 * a #MemoryRegion that was mapped with address_space_cache_init.
 *
 * The _le suffixed functions treat the data as little endian;
 * _be indicates big endian; no suffix indicates "same endianness
 * as guest CPU".
 *
 * The "guest CPU endianness" accessors are deprecated for use outside
 * target-* code; devices should be CPU-agnostic and use either the LE
 * or the BE accessors.
 *
 * @cache: previously initialized #MemoryRegionCache to be accessed
 * @addr: address within the address space
 * @val: data value, for stores
 * @attrs: memory transaction attributes
 * @result: location to write the success/failure of the transaction;
 *          if NULL, this information is discarded
 */

#define SUFFIX       _cached_slow
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#include "system/memory_ldst.h.inc"

/* Inline fast path for direct RAM access.  */
static inline
uint8_t address_space_ldub_cached(MemoryRegionCache *cache, hwaddr addr,
                                  MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len);
    if (likely(cache->ptr)) {
        return ldub_p(cache->ptr + addr);
    } else {
        return address_space_ldub_cached_slow(cache, addr, attrs, result);
    }
}

static inline
void address_space_stb_cached(MemoryRegionCache *cache,
                              hwaddr addr, uint8_t val,
                              MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len);
    if (likely(cache->ptr)) {
        stb_p(cache->ptr + addr, val);
    } else {
        address_space_stb_cached_slow(cache, addr, val, attrs, result);
    }
}

#ifndef TARGET_NOT_USING_LEGACY_NATIVE_ENDIAN_API
#define ENDIANNESS
#include "system/memory_ldst_cached.h.inc"
#endif

#define ENDIANNESS   _le
#include "system/memory_ldst_cached.h.inc"

#define ENDIANNESS   _be
#include "system/memory_ldst_cached.h.inc"

#define SUFFIX       _cached
#define ARG1         cache
#define ARG1_DECL    MemoryRegionCache *cache
#include "system/memory_ldst_phys.h.inc"

/**
 * address_space_cache_init: prepare for repeated access to a physical
 *                           memory region
 *
 * @cache: #MemoryRegionCache to be filled
 * @as: #AddressSpace to be accessed
 * @addr: address within that address space
 * @len: length of buffer
 * @is_write: indicates the transfer direction
 *
 * Will only work with RAM, and may map a subset of the requested range by
 * returning a value that is less than @len.  On failure, return a negative
 * errno value.
 *
 * Because it only works with RAM, this function can be used for
 * read-modify-write operations.  In this case, is_write should be %true.
 *
 * Note that addresses passed to the address_space_*_cached functions
 * are relative to @addr.
 */
int64_t address_space_cache_init(MemoryRegionCache *cache,
                                 AddressSpace *as,
                                 hwaddr addr,
                                 hwaddr len,
                                 bool is_write);

/**
 * address_space_cache_init_empty: Initialize empty #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache to operate on.
 *
 * Initializes #MemoryRegionCache structure without memory region attached.
 * Cache initialized this way can only be safely destroyed, but not used.
 */
static inline void address_space_cache_init_empty(MemoryRegionCache *cache)
{
    cache->mrs.mr = NULL;
    /* There is no real need to initialize fv, but it makes Coverity happy. */
    cache->fv = NULL;
}

/**
 * address_space_cache_invalidate: complete a write to a #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache to operate on.
 * @addr: The first physical address that was written, relative to the
 * address that was passed to @address_space_cache_init.
 * @access_len: The number of bytes that were written starting at @addr.
 */
void address_space_cache_invalidate(MemoryRegionCache *cache,
                                    hwaddr addr,
                                    hwaddr access_len);

/**
 * address_space_cache_destroy: free a #MemoryRegionCache
 *
 * @cache: The #MemoryRegionCache whose memory should be released.
 */
void address_space_cache_destroy(MemoryRegionCache *cache);

/*
 * Internal functions, part of the implementation of address_space_read_cached
 * and address_space_write_cached.
 */
MemTxResult address_space_read_cached_slow(MemoryRegionCache *cache,
                                           hwaddr addr, void *buf, hwaddr len);
MemTxResult address_space_write_cached_slow(MemoryRegionCache *cache,
                                            hwaddr addr, const void *buf,
                                            hwaddr len);

/**
 * address_space_read_cached: read from a cached RAM region
 *
 * @cache: Cached region to be addressed
 * @addr: address relative to the base of the RAM region
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult
address_space_read_cached(MemoryRegionCache *cache, hwaddr addr,
                          void *buf, hwaddr len)
{
    assert(addr < cache->len && len <= cache->len - addr);
    fuzz_dma_read_cb(cache->xlat + addr, len, cache->mrs.mr);
    if (likely(cache->ptr)) {
        memcpy(buf, cache->ptr + addr, len);
        return MEMTX_OK;
    } else {
        return address_space_read_cached_slow(cache, addr, buf, len);
    }
}

/**
 * address_space_write_cached: write to a cached RAM region
 *
 * @cache: Cached region to be addressed
 * @addr: address relative to the base of the RAM region
 * @buf: buffer with the data transferred
 * @len: length of the data transferred
 */
static inline MemTxResult
address_space_write_cached(MemoryRegionCache *cache, hwaddr addr,
                           const void *buf, hwaddr len)
{
    assert(addr < cache->len && len <= cache->len - addr);
    if (likely(cache->ptr)) {
        memcpy(cache->ptr + addr, buf, len);
        return MEMTX_OK;
    } else {
        return address_space_write_cached_slow(cache, addr, buf, len);
    }
}

#endif
