/*
 * Flush the host cpu caches.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cacheflush.h"
#include "qemu/cacheinfo.h"
#include "qemu/bitops.h"


#if defined(__i386__) || defined(__x86_64__) || defined(__s390__)

/* Caches are coherent and do not require flushing; symbol inline. */

#elif defined(__aarch64__)

#ifdef CONFIG_DARWIN
/* Apple does not expose CTR_EL0, so we must use system interfaces. */
extern void sys_icache_invalidate(void *start, size_t len);
extern void sys_dcache_flush(void *start, size_t len);
void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    sys_dcache_flush((void *)rw, len);
    sys_icache_invalidate((void *)rx, len);
}
#else

/*
 * TODO: unify this with cacheinfo.c.
 * We want to save the whole contents of CTR_EL0, so that we
 * have more than the linesize, but also IDC and DIC.
 */
static uint64_t save_ctr_el0;
static void __attribute__((constructor)) init_ctr_el0(void)
{
    asm volatile("mrs\t%0, ctr_el0" : "=r"(save_ctr_el0));
}

/*
 * This is a copy of gcc's __aarch64_sync_cache_range, modified
 * to fit this three-operand interface.
 */
void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    const unsigned CTR_IDC = 1u << 28;
    const unsigned CTR_DIC = 1u << 29;
    const uint64_t ctr_el0 = save_ctr_el0;
    const uintptr_t icache_lsize = 4 << extract64(ctr_el0, 0, 4);
    const uintptr_t dcache_lsize = 4 << extract64(ctr_el0, 16, 4);
    uintptr_t p;

    /*
     * If CTR_EL0.IDC is enabled, Data cache clean to the Point of Unification
     * is not required for instruction to data coherence.
     */
    if (!(ctr_el0 & CTR_IDC)) {
        /*
         * Loop over the address range, clearing one cache line at once.
         * Data cache must be flushed to unification first to make sure
         * the instruction cache fetches the updated data.
         */
        for (p = rw & -dcache_lsize; p < rw + len; p += dcache_lsize) {
            asm volatile("dc\tcvau, %0" : : "r" (p) : "memory");
        }
        asm volatile("dsb\tish" : : : "memory");
    }

    /*
     * If CTR_EL0.DIC is enabled, Instruction cache cleaning to the Point
     * of Unification is not required for instruction to data coherence.
     */
    if (!(ctr_el0 & CTR_DIC)) {
        for (p = rx & -icache_lsize; p < rx + len; p += icache_lsize) {
            asm volatile("ic\tivau, %0" : : "r"(p) : "memory");
        }
        asm volatile ("dsb\tish" : : : "memory");
    }

    asm volatile("isb" : : : "memory");
}
#endif /* CONFIG_DARWIN */

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
