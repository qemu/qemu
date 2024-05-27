/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for LoongArch.
 */

#include "qemu/osdep.h"
#include "host/cpuinfo.h"

#ifdef CONFIG_GETAUXVAL
# include <sys/auxv.h>
#else
# include "elf.h"
#endif
#include <asm/hwcap.h>

unsigned cpuinfo;

/* Called both as constructor and (possibly) via other constructors. */
unsigned __attribute__((constructor)) cpuinfo_init(void)
{
    unsigned info = cpuinfo;
    unsigned long hwcap;

    if (info) {
        return info;
    }

    hwcap = qemu_getauxval(AT_HWCAP);

    info = CPUINFO_ALWAYS;
    info |= (hwcap & HWCAP_LOONGARCH_LSX ? CPUINFO_LSX : 0);
    info |= (hwcap & HWCAP_LOONGARCH_LASX ? CPUINFO_LASX : 0);

    cpuinfo = info;
    return info;
}
