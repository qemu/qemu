#ifndef QEMU_CACHE_UTILS_H
#define QEMU_CACHE_UTILS_H

#include "config-host.h"

#ifdef HOST_PPC
struct qemu_cache_conf {
    unsigned long dcache_bsize;
    unsigned long icache_bsize;
};

extern struct qemu_cache_conf qemu_cache_conf;

extern void qemu_cache_utils_init(char **envp);

/* mildly adjusted code from tcg-dyngen.c */
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    unsigned long p, start1, stop1;
    unsigned long dsize = qemu_cache_conf.dcache_bsize;
    unsigned long isize = qemu_cache_conf.icache_bsize;

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

#else
#define qemu_cache_utils_init(envp) do { (void) (envp); } while (0)
#endif

#endif /* QEMU_CACHE_UTILS_H */
