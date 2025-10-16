/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return TARGET_BIG_ENDIAN ? "any,little-endian=off"
                             : "any,little-endian=on";
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUMBState *env)
{
    for (int i = 0; i < 32; i++) {
        r->pt.r[i] = tswapal(env->regs[i]);
    }

    r->pt.pc = tswapal(env->pc);
    r->pt.msr = tswapal(mb_cpu_read_msr(env));
    r->pt.ear = tswapal(env->ear);
    r->pt.esr = tswapal(env->esr);
}
