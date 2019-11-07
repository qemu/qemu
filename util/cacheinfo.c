/*
 * cacheinfo.c - helpers to query the host about its caches
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/atomic.h"

int qemu_icache_linesize = 0;
int qemu_icache_linesize_log;
int qemu_dcache_linesize = 0;
int qemu_dcache_linesize_log;

/*
 * Operating system specific detection mechanisms.
 */

#if defined(_WIN32)

static void sys_cache_info(int *isize, int *dsize)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf;
    DWORD size = 0;
    BOOL success;
    size_t i, n;

    /* Check for the required buffer size first.  Note that if the zero
       size we use for the probe results in success, then there is no
       data available; fail in that case.  */
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

#elif defined(__APPLE__)
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
    *isize = sysconf(_SC_LEVEL1_ICACHE_LINESIZE);
# endif
# ifdef _SC_LEVEL1_DCACHE_LINESIZE
    *dsize = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
# endif
}
#endif /* sys_cache_info */

/*
 * Architecture (+ OS) specific detection mechanisms.
 */

#if defined(__aarch64__)

static void arch_cache_info(int *isize, int *dsize)
{
    if (*isize == 0 || *dsize == 0) {
        uint64_t ctr;

        /* The real cache geometry is in CCSIDR_EL1/CLIDR_EL1/CSSELR_EL1,
           but (at least under Linux) these are marked protected by the
           kernel.  However, CTR_EL0 contains the minimum linesize in the
           entire hierarchy, and is used by userspace cache flushing.  */
        asm volatile("mrs\t%0, ctr_el0" : "=r"(ctr));
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
        /* For PPC, we're going to use the icache size computed for
           flush_icache_range.  Which means that we must use the
           architecture minimum.  */
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

    atomic64_init();
}
