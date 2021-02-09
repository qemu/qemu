/*
 *  Memory access templates for MemoryRegionCache
 *
 *  Copyright (c) 2018 Red Hat, Inc.
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

#define ADDRESS_SPACE_LD_CACHED(size) \
    glue(glue(address_space_ld, size), glue(ENDIANNESS, _cached))
#define ADDRESS_SPACE_LD_CACHED_SLOW(size) \
    glue(glue(address_space_ld, size), glue(ENDIANNESS, _cached_slow))
#define LD_P(size) \
    glue(glue(ld, size), glue(ENDIANNESS, _p))

static inline uint32_t ADDRESS_SPACE_LD_CACHED(l)(MemoryRegionCache *cache,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 4 <= cache->len - addr);
    fuzz_dma_read_cb(cache->xlat + addr, 4, cache->mrs.mr);
    if (likely(cache->ptr)) {
        return LD_P(l)(cache->ptr + addr);
    } else {
        return ADDRESS_SPACE_LD_CACHED_SLOW(l)(cache, addr, attrs, result);
    }
}

static inline uint64_t ADDRESS_SPACE_LD_CACHED(q)(MemoryRegionCache *cache,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 8 <= cache->len - addr);
    fuzz_dma_read_cb(cache->xlat + addr, 8, cache->mrs.mr);
    if (likely(cache->ptr)) {
        return LD_P(q)(cache->ptr + addr);
    } else {
        return ADDRESS_SPACE_LD_CACHED_SLOW(q)(cache, addr, attrs, result);
    }
}

static inline uint32_t ADDRESS_SPACE_LD_CACHED(uw)(MemoryRegionCache *cache,
    hwaddr addr, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 2 <= cache->len - addr);
    fuzz_dma_read_cb(cache->xlat + addr, 2, cache->mrs.mr);
    if (likely(cache->ptr)) {
        return LD_P(uw)(cache->ptr + addr);
    } else {
        return ADDRESS_SPACE_LD_CACHED_SLOW(uw)(cache, addr, attrs, result);
    }
}

#undef ADDRESS_SPACE_LD_CACHED
#undef ADDRESS_SPACE_LD_CACHED_SLOW
#undef LD_P

#define ADDRESS_SPACE_ST_CACHED(size) \
    glue(glue(address_space_st, size), glue(ENDIANNESS, _cached))
#define ADDRESS_SPACE_ST_CACHED_SLOW(size) \
    glue(glue(address_space_st, size), glue(ENDIANNESS, _cached_slow))
#define ST_P(size) \
    glue(glue(st, size), glue(ENDIANNESS, _p))

static inline void ADDRESS_SPACE_ST_CACHED(l)(MemoryRegionCache *cache,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 4 <= cache->len - addr);
    if (likely(cache->ptr)) {
        ST_P(l)(cache->ptr + addr, val);
    } else {
        ADDRESS_SPACE_ST_CACHED_SLOW(l)(cache, addr, val, attrs, result);
    }
}

static inline void ADDRESS_SPACE_ST_CACHED(w)(MemoryRegionCache *cache,
    hwaddr addr, uint32_t val, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 2 <= cache->len - addr);
    if (likely(cache->ptr)) {
        ST_P(w)(cache->ptr + addr, val);
    } else {
        ADDRESS_SPACE_ST_CACHED_SLOW(w)(cache, addr, val, attrs, result);
    }
}

static inline void ADDRESS_SPACE_ST_CACHED(q)(MemoryRegionCache *cache,
    hwaddr addr, uint64_t val, MemTxAttrs attrs, MemTxResult *result)
{
    assert(addr < cache->len && 8 <= cache->len - addr);
    if (likely(cache->ptr)) {
        ST_P(q)(cache->ptr + addr, val);
    } else {
        ADDRESS_SPACE_ST_CACHED_SLOW(q)(cache, addr, val, attrs, result);
    }
}

#undef ADDRESS_SPACE_ST_CACHED
#undef ADDRESS_SPACE_ST_CACHED_SLOW
#undef ST_P

#undef ENDIANNESS
