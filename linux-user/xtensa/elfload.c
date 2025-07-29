/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return XTENSA_DEFAULT_CPU_MODEL;
}

#define tswapreg(ptr)   tswapal(ptr)

enum {
    TARGET_REG_PC,
    TARGET_REG_PS,
    TARGET_REG_LBEG,
    TARGET_REG_LEND,
    TARGET_REG_LCOUNT,
    TARGET_REG_SAR,
    TARGET_REG_WINDOWSTART,
    TARGET_REG_WINDOWBASE,
    TARGET_REG_THREADPTR,
    TARGET_REG_AR0 = 64,
};

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUXtensaState *env)
{
    unsigned i;

    r->regs[TARGET_REG_PC] = tswapreg(env->pc);
    r->regs[TARGET_REG_PS] = tswapreg(env->sregs[PS] & ~PS_EXCM);
    r->regs[TARGET_REG_LBEG] = tswapreg(env->sregs[LBEG]);
    r->regs[TARGET_REG_LEND] = tswapreg(env->sregs[LEND]);
    r->regs[TARGET_REG_LCOUNT] = tswapreg(env->sregs[LCOUNT]);
    r->regs[TARGET_REG_SAR] = tswapreg(env->sregs[SAR]);
    r->regs[TARGET_REG_WINDOWSTART] = tswapreg(env->sregs[WINDOW_START]);
    r->regs[TARGET_REG_WINDOWBASE] = tswapreg(env->sregs[WINDOW_BASE]);
    r->regs[TARGET_REG_THREADPTR] = tswapreg(env->uregs[THREADPTR]);
    xtensa_sync_phys_from_window((CPUXtensaState *)env);
    for (i = 0; i < env->config->nareg; ++i) {
        r->regs[TARGET_REG_AR0 + i] = tswapreg(env->phys_regs[i]);
    }
}
