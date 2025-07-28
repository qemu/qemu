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
    static char elf_platform[] = "i386";
    int family = object_property_get_int(OBJECT(cs), "family", NULL);
    if (family > 6) {
        family = 6;
    }
    if (family >= 3) {
        elf_platform[1] = '0' + family;
    }
    return elf_platform;
}
