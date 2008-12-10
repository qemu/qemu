#include "cache-utils.h"

#ifdef __powerpc__
struct qemu_cache_conf qemu_cache_conf = {
    .dcache_bsize = 16,
    .icache_bsize = 16
};

#if defined _AIX
#include <sys/systemcfg.h>

static void ppc_init_cacheline_sizes(void)
{
    qemu_cache_conf.icache_bsize = _system_configuration.icache_line;
    qemu_cache_conf.dcache_bsize = _system_configuration.dcache_line;
}

#elif defined __linux__
#include <linux/auxvec.h>

static void ppc_init_cacheline_sizes(char **envp)
{
    unsigned long *auxv;

    while (*envp++);

    for (auxv = (unsigned long *) envp; *auxv != AT_NULL; auxv += 2) {
        switch (*auxv) {
        case AT_DCACHEBSIZE: qemu_cache_conf.dcache_bsize = auxv[1]; break;
        case AT_ICACHEBSIZE: qemu_cache_conf.icache_bsize = auxv[1]; break;
        default: break;
        }
    }
}

#elif defined __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>

static void ppc_init_cacheline_sizes(void)
{
    size_t len;
    unsigned cacheline;
    int name[2] = { CTL_HW, HW_CACHELINE };

    if (sysctl(name, 2, &cacheline, &len, NULL, 0)) {
        perror("sysctl CTL_HW HW_CACHELINE failed");
    } else {
        qemu_cache_conf.dcache_bsize = cacheline;
        qemu_cache_conf.icache_bsize = cacheline;
    }
}
#endif

#ifdef __linux__
void qemu_cache_utils_init(char **envp)
{
    ppc_init_cacheline_sizes(envp);
}
#else
void qemu_cache_utils_init(char **envp)
{
    (void) envp;
    ppc_init_cacheline_sizes();
}
#endif

#endif /* __powerpc__ */
