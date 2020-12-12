/*
 * Flush the host cpu caches.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cacheflush.h"


#if defined(__i386__) || defined(__x86_64__) || defined(__s390__)

/* Caches are coherent and do not require flushing; symbol inline. */

#elif defined(__mips__)

#ifdef __OpenBSD__
#include <machine/sysarch.h>
#else
#include <sys/cachectl.h>
#endif

void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    if (rx != rw) {
        cacheflush((void *)rw, len, DCACHE);
    }
    cacheflush((void *)rx, len, ICACHE);
}

#elif defined(__powerpc__)

void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    uintptr_t p, b, e;
    size_t dsize = qemu_dcache_linesize;
    size_t isize = qemu_icache_linesize;

    b = rw & ~(dsize - 1);
    e = (rw + len + dsize - 1) & ~(dsize - 1);
    for (p = b; p < e; p += dsize) {
        asm volatile ("dcbst 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");

    b = rx & ~(isize - 1);
    e = (rx + len + isize - 1) & ~(isize - 1);
    for (p = b; p < e; p += isize) {
        asm volatile ("icbi 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");
    asm volatile ("isync" : : : "memory");
}

#elif defined(__sparc__)

void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    /* No additional data flush to the RW virtual address required. */
    uintptr_t p, end = (rx + len + 7) & -8;
    for (p = rx & -8; p < end; p += 8) {
        __asm__ __volatile__("flush\t%0" : : "r" (p));
    }
}

#else

void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    if (rw != rx) {
        __builtin___clear_cache((char *)rw, (char *)rw + len);
    }
    __builtin___clear_cache((char *)rx, (char *)rx + len);
}

#endif
