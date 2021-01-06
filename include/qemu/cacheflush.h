/*
 * Flush the host cpu caches.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_CACHEFLUSH_H
#define QEMU_CACHEFLUSH_H

#if defined(__i386__) || defined(__x86_64__) || defined(__s390__)

static inline void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    /* icache is coherent and does not require flushing. */
}

#else

void flush_icache_range(uintptr_t start, uintptr_t stop);

#endif

#endif /* QEMU_CACHEFLUSH_H */
