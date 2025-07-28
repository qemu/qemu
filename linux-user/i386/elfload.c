/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "max";
}

abi_ulong get_elf_hwcap(CPUState *cs)
{
    return cpu_env(cs)->features[FEAT_1_EDX];
}

const char *get_elf_platform(CPUState *cs)
{
    static const char elf_platform[4][5] = { "i386", "i486", "i586", "i686" };
    int family = object_property_get_int(OBJECT(cs), "family", NULL);

    family = MAX(MIN(family, 6), 3);
    return elf_platform[family - 3];
}
