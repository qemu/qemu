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
    return "x86_64";
}
