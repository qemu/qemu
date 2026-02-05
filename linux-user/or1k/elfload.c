/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUOpenRISCState *env)
{
    for (int i = 0; i < 32; i++) {
        r->pt.gpr[i] = tswapal(cpu_get_gpr(env, i));
    }
    r->pt.pc = tswapal(env->pc);
    r->pt.sr = tswapal(cpu_get_sr(env));
}
