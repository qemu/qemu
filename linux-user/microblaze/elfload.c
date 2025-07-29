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

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUMBState *env)
{
    for (int i = 0; i < 32; i++) {
        r->regs[i] = tswapreg(env->regs[i]);
    }

    r->regs[32] = tswapreg(env->pc);
    r->regs[33] = tswapreg(mb_cpu_read_msr(env));
    r->regs[34] = 0;
    r->regs[35] = tswapreg(env->ear);
    r->regs[36] = 0;
    r->regs[37] = tswapreg(env->esr);
}
