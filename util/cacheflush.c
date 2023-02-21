/*
 * Info about, and flushing the host cpu caches.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cacheflush.h"
#include "qemu/cacheinfo.h"
#include "qemu/bitops.h"
#include "qemu/host-utils.h"
#include "qemu/atomic.h"


int qemu_icache_linesize = 0;
int qemu_icache_linesize_log;
int qemu_dcache_linesize = 0;
int qemu_dcache_linesize_log;

/*
 * Operating system specific cache detection mechanisms.
 */

#if defined(_WIN32)

static void sys_cache_info(int *isize, int *dsize)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf;
    DWORD size = 0;
    BOOL success;
    size_t i, n;

    /*
     * Check for the required buffer size first.  Note that if the zero
     * size we use for the probe results in success, then there is no
     * data available; fail in that case.
     */
    success = GetLogicalProcessorInformation(0, &size);
    if (success || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return;
    }

    n = size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    size = n * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    buf = g_new0(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, n);
    if (!GetLogicalProcessorInformation(buf, &size)) {
        goto fail;
    }

    for (i = 0; i < n; i++) {
        if (buf[i].Relationship == RelationCache
            && buf[i].Cache.Level == 1) {
            switch (buf[i].Cache.Type) {
            case CacheUnified:
                *isize = *dsize = buf[i].Cache.LineSize;
                break;
            case CacheInstruction:
                *isize = buf[i].Cache.LineSize;
                break;
            case CacheData:
                *dsize = buf[i].Cache.LineSize;
                break;
            default:
                break;
            }
        }
    }
 fail:
    g_free(buf);
}

#elif defined(CONFIG_DARWIN)
# include <sys/sysctl.h>
static void sys_cache_info(int *isize, int *dsize)
{
    /* There's only a single sysctl for both I/D cache line sizes.  */
    long size;
    size_t len = sizeof(size);
    if (!sysctlbyname("hw.cachelinesize", &size, &len, NULL, 0)) {
        *isize = *dsize = size;
    }
}
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/sysctl.h>
static void sys_cache_info(int *isize, int *dsize)
{
    /* There's only a single sysctl for both I/D cache line sizes.  */
    int size;
    size_t len = sizeof(size);
    if (!sysctlbyname("machdep.cacheline_size", &size, &len, NULL, 0)) {
        *isize = *dsize = size;
    }
}
#else
/* POSIX */

static void sys_cache_info(int *isize, int *dsize)
{
# ifdef _SC_LEVEL1_ICACHE_LINESIZE
    int tmp_isize = (int) sysconf(_SC_LEVEL1_ICACHE_LINESIZE);
    if (tmp_isize > 0) {
        *isize = tmp_isize;
    }
# endif
# ifdef _SC_LEVEL1_DCACHE_LINESIZE
    int tmp_dsize = (int) sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (tmp_dsize > 0) {
        *dsize = tmp_dsize;
    }
# endif
}
#endif /* sys_cache_info */


/*
 * Architecture (+ OS) specific cache detection mechanisms.
 */

#if defined(__powerpc__)
static bool have_coherent_icache;
#endif

#if defined(__aarch64__) && !defined(CONFIG_DARWIN) && !defined(CONFIG_WIN32)
/*
 * Apple does not expose CTR_EL0, so we must use system interfaces.
 * Windows neither, but we use a generic implementation of flush_idcache_range
 * in this case.
 */
static uint64_t save_ctr_el0;
static void arch_cache_info(int *isize, int *dsize)
{
    uint64_t ctr;

    /*
     * The real cache geometry is in CCSIDR_EL1/CLIDR_EL1/CSSELR_EL1,
     * but (at least under Linux) these are marked protected by the
     * kernel.  However, CTR_EL0 contains the minimum linesize in the
     * entire hierarchy, and is used by userspace cache flushing.
     *
     * We will also use this value in flush_idcache_range.
     */
    asm volatile("mrs\t%0, ctr_el0" : "=r"(ctr));
    save_ctr_el0 = ctr;

    if (*isize == 0 || *dsize == 0) {
        if (*isize == 0) {
            *isize = 4 << (ctr & 0xf);
        }
        if (*dsize == 0) {
            *dsize = 4 << ((ctr >> 16) & 0xf);
        }
    }
}

#elif defined(_ARCH_PPC) && defined(__linux__)
# include "elf.h"

static void arch_cache_info(int *isize, int *dsize)
{
    if (*isize == 0) {
        *isize = qemu_getauxval(AT_ICACHEBSIZE);
    }
    if (*dsize == 0) {
        *dsize = qemu_getauxval(AT_DCACHEBSIZE);
    }
    have_coherent_icache = qemu_getauxval(AT_HWCAP) & PPC_FEATURE_ICACHE_SNOOP;
}

#else
static void arch_cache_info(int *isize, int *dsize) { }
#endif /* arch_cache_info */

/*
 * ... and if all else fails ...
 */

static void fallback_cache_info(int *isize, int *dsize)
{
    /* If we can only find one of the two, assume they're the same.  */
    if (*isize) {
        if (*dsize) {
            /* Success! */
        } else {
            *dsize = *isize;
        }
    } else if (*dsize) {
        *isize = *dsize;
    } else {
#if defined(_ARCH_PPC)
        /*
         * For PPC, we're going to use the cache sizes computed for
         * flush_idcache_range.  Which means that we must use the
         * architecture minimum.
         */
        *isize = *dsize = 16;
#else
        /* Otherwise, 64 bytes is not uncommon.  */
        *isize = *dsize = 64;
#endif
    }
}

static void __attribute__((constructor)) init_cache_info(void)
{
    int isize = 0, dsize = 0;

    sys_cache_info(&isize, &dsize);
    arch_cache_info(&isize, &dsize);
    fallback_cache_info(&isize, &dsize);

    assert((isize & (isize - 1)) == 0);
    assert((dsize & (dsize - 1)) == 0);

    qemu_icache_linesize = isize;
    qemu_icache_linesize_log = ctz32(isize);
    qemu_dcache_linesize = dsize;
    qemu_dcache_linesize_log = ctz32(dsize);

    qatomic64_init();
}


/*
 * Architecture (+ OS) specific cache flushing mechanisms.
 */

#if defined(__i386__) || defined(__x86_64__) || defined(__s390__)

/* Caches are coherent and do not require flushing; symbol inline. */

#elif defined(__aarch64__) && !defined(CONFIG_WIN32)
/*
 * For Windows, we use generic implementation of flush_idcache_range, that
 * performs a call to FlushInstructionCache, through __builtin___clear_cache.
 */

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
 * This is a copy of gcc's __aarch64_sync_cache_range, modified
 * to fit this three-operand interface.
 */
void flush_idcache_range(uintptr_t rx, uintptr_t rw, size_t len)
{
    const unsigned CTR_IDC = 1u << 28;
    const unsigned CTR_DIC = 1u << 29;
    const uint64_t ctr_el0 = save_ctr_el0;
    const uintptr_t icache_lsize = qemu_icache_linesize;
    const uintptr_t dcache_lsize = qemu_dcache_linesize;
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
    size_t dsize, isize;

    /*
     * Some processors have coherent caches and support a simplified
     * flushing procedure.  See
     *   POWER9 UM, 4.6.2.2 Instruction Cache Block Invalidate (icbi) 
     *   https://ibm.ent.box.com/s/tmklq90ze7aj8f4n32er1mu3sy9u8k3k
     */
    if (have_coherent_icache) {
        asm volatile ("sync\n\t"
                      "icbi 0,%0\n\t"
                      "isync"
                      : : "r"(rx) : "memory");
        return;
    }

    dsize = qemu_dcache_linesize;
    isize = qemu_icache_linesize;

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
