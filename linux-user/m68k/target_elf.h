/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef M68K_TARGET_ELF_H
#define M68K_TARGET_ELF_H
static inline const char *cpu_get_model(uint32_t eflags)
{
    if (eflags == 0 || (eflags & EF_M68K_M68000)) {
        /* 680x0 */
        return "m68040";
    }

    /* Coldfire */
    return "any";
}
#endif
