/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    if (eflags == 0 || (eflags & EF_M68K_M68000)) {
        /* 680x0 */
        return "m68040";
    }

    /* Coldfire */
    return "any";
}
