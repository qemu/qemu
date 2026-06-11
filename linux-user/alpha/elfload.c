/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


void elf_core_copy_regs(target_elf_gregset_t *r, const CPUAlphaState *env)
{
    int i;

    for (i = 0; i < 31; i++) {
        r->regs[i] = tswap64(env->ir[i]);
    }
    r->pc = tswap64(env->pc);
    r->unique = tswap64(env->unique);
}

const char *get_elf_cpu_model(uint32_t eflags)
{
    return "ev67";
}
