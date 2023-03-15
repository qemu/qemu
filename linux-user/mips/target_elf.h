/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef MIPS_TARGET_ELF_H
#define MIPS_TARGET_ELF_H
static inline const char *cpu_get_model(uint32_t eflags)
{
    if ((eflags & EF_MIPS_ARCH) == EF_MIPS_ARCH_32R6) {
        return "mips32r6-generic";
    }
    if ((eflags & EF_MIPS_MACH) == EF_MIPS_MACH_5900) {
        return "R5900";
    }
    if (eflags & EF_MIPS_NAN2008) {
        return "P5600";
    }
    return "24Kf";
}
#endif
