/*
 * Flush the host cpu caches.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_CACHEFLUSH_H
#define QEMU_CACHEFLUSH_H

/**
 * flush_idcache_range:
 * @rx: instruction address
 * @rw: data address
 * @len: length to flush
 *
 * Flush @len bytes of the data cache at @rw and the icache at @rx
 * to bring them in sync.  The two addresses may be different virtual
 * mappings of the same physical page(s).
 */

#if defined(__i386__) || defined(__x86_64__) || defined(__s390__)

static inline void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    /* icache is coherent and does not require flushing. */
}

#elif defined(EMSCRIPTEN)

static inline void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    /* Wasm doesn't have executable region of memory. */
}

#else

void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len);

#endif

#endif /* QEMU_CACHEFLUSH_H */
