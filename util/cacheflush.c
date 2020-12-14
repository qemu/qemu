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

void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    cacheflush((void *)start, stop - start, ICACHE);
}

#elif defined(__powerpc__)

void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    uintptr_t p, start1, stop1;
    size_t dsize = qemu_dcache_linesize;
    size_t isize = qemu_icache_linesize;

    start1 = start & ~(dsize - 1);
    stop1 = (stop + dsize - 1) & ~(dsize - 1);
    for (p = start1; p < stop1; p += dsize) {
        asm volatile ("dcbst 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");

    start &= start & ~(isize - 1);
    stop1 = (stop + isize - 1) & ~(isize - 1);
    for (p = start1; p < stop1; p += isize) {
        asm volatile ("icbi 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");
    asm volatile ("isync" : : : "memory");
}

#elif defined(__sparc__)

void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    uintptr_t p;

    for (p = start & -8; p < ((stop + 7) & -8); p += 8) {
        __asm__ __volatile__("flush\t%0" : : "r" (p));
    }
}

#else

void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    __builtin___clear_cache((char *)start, (char *)stop);
}

#endif
