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
    if ((eflags & EF_MIPS_ARCH) == EF_MIPS_ARCH_64R6) {
        return "I6400";
    }
    return "5KEf";
}
#endif
