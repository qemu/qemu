/*
 * Process-global memory barriers
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 */

#include "qemu/osdep.h"
#include "qemu/sys_membarrier.h"
#include "qemu/error-report.h"

#ifdef CONFIG_LINUX
#include <linux/membarrier.h>
#include <sys/syscall.h>

static int
membarrier(int cmd, int flags)
{
    return syscall(__NR_membarrier, cmd, flags);
}
#endif

void smp_mb_global(void)
{
#if defined CONFIG_WIN32
    FlushProcessWriteBuffers();
#elif defined CONFIG_LINUX
    membarrier(MEMBARRIER_CMD_SHARED, 0);
#else
#error --enable-membarrier is not supported on this operating system.
#endif
}

void smp_mb_global_init(void)
{
#ifdef CONFIG_LINUX
    int ret = membarrier(MEMBARRIER_CMD_QUERY, 0);
    if (ret < 0) {
        error_report("This QEMU binary requires the membarrier system call.");
        error_report("Please upgrade your system to a newer version of Linux");
        exit(1);
    }
    if (!(ret & MEMBARRIER_CMD_SHARED)) {
        error_report("This QEMU binary requires MEMBARRIER_CMD_SHARED support.");
        error_report("Please upgrade your system to a newer version of Linux");
        exit(1);
    }
#endif
}
