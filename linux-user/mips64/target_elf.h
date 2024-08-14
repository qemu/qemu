/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MIPS64_TARGET_ELF_H
#define MIPS64_TARGET_ELF_H
static inline const char *cpu_get_model(uint32_t eflags)
{
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
}
#endif
