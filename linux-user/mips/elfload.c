/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
#ifdef TARGET_MIPS64
    switch (eflags & EF_MIPS_MACH) {
    case EF_MIPS_MACH_OCTEON:
    case EF_MIPS_MACH_OCTEON2:
    case EF_MIPS_MACH_OCTEON3:
        return "Octeon68XX";
    case EF_MIPS_MACH_LS2E:
        return "Loongson-2E";
    case EF_MIPS_MACH_LS2F:
        return "Loongson-2F";
    case EF_MIPS_MACH_LS3A:
        return "Loongson-3A1000";
    default:
        break;
    }
    switch (eflags & EF_MIPS_ARCH) {
    case EF_MIPS_ARCH_64R6:
        return "I6400";
    case EF_MIPS_ARCH_64R2:
        return "MIPS64R2-generic";
    default:
        break;
    }
    return "5KEf";
#else
    if ((eflags & EF_MIPS_ARCH) == EF_MIPS_ARCH_32R6) {
        return "mips32r6-generic";
    }
    if (eflags & EF_MIPS_NAN2008) {
        return "P5600";
    }
    return "24Kf";
#endif
}
