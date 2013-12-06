#include "qemu-common.h"
#include "qemu/cache-utils.h"

#if defined(_ARCH_PPC)
struct qemu_cache_conf qemu_cache_conf = {
    .dcache_bsize = 16,
    .icache_bsize = 16
};

#if defined _AIX
#include <sys/systemcfg.h>

void qemu_cache_utils_init(void)
{
    qemu_cache_conf.icache_bsize = _system_configuration.icache_line;
    qemu_cache_conf.dcache_bsize = _system_configuration.dcache_line;
}

#elif defined __linux__
#include "qemu/osdep.h"
#include "elf.h"

void qemu_cache_utils_init(void)
{
    unsigned long dsize = qemu_getauxval(AT_DCACHEBSIZE);
    unsigned long isize = qemu_getauxval(AT_ICACHEBSIZE);

    if (dsize == 0 || isize == 0) {
        if (dsize == 0) {
            fprintf(stderr, "getauxval AT_DCACHEBSIZE failed\n");
        }
        if (isize == 0) {
            fprintf(stderr, "getauxval AT_ICACHEBSIZE failed\n");
        }
        exit(1);

    }
    qemu_cache_conf.dcache_bsize = dsize;
    qemu_cache_conf.icache_bsize = isize;
}

#elif defined __APPLE__
#include <stdio.h>
#include <sys/types.h>
#include <sys/sysctl.h>

void qemu_cache_utils_init(void)
{
    size_t len;
    unsigned cacheline;
    int name[2] = { CTL_HW, HW_CACHELINE };

    len = sizeof(cacheline);
    if (sysctl(name, 2, &cacheline, &len, NULL, 0)) {
        perror("sysctl CTL_HW HW_CACHELINE failed");
    } else {
        qemu_cache_conf.dcache_bsize = cacheline;
        qemu_cache_conf.icache_bsize = cacheline;
    }
}

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>

void qemu_cache_utils_init(void)
{
    size_t len = 4;
    unsigned cacheline;

    if (sysctlbyname ("machdep.cacheline_size", &cacheline, &len, NULL, 0)) {
        fprintf(stderr, "sysctlbyname machdep.cacheline_size failed: %s\n",
                strerror(errno));
        exit(1);
    }

    qemu_cache_conf.dcache_bsize = cacheline;
    qemu_cache_conf.icache_bsize = cacheline;
}
#endif

#endif /* _ARCH_PPC */
