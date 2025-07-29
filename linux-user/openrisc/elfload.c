/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}

#define tswapreg(ptr)   tswapal(ptr)

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUOpenRISCState *env)
{
    for (int i = 0; i < 32; i++) {
        r->regs[i] = tswapreg(cpu_get_gpr(env, i));
    }
    r->regs[32] = tswapreg(env->pc);
    r->regs[33] = tswapreg(cpu_get_sr(env));
}
