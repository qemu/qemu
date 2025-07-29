/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
#ifdef TARGET_SPARC64
    return "TI UltraSparc II";
#else
    return "Fujitsu MB86904";
#endif
}
