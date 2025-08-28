/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return XTENSA_DEFAULT_CPU_MODEL;
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUXtensaState *env)
{
    r->pt.pc = tswap32(env->pc);
    r->pt.ps = tswap32(env->sregs[PS] & ~PS_EXCM);
    r->pt.lbeg = tswap32(env->sregs[LBEG]);
    r->pt.lend = tswap32(env->sregs[LEND]);
    r->pt.lcount = tswap32(env->sregs[LCOUNT]);
    r->pt.sar = tswap32(env->sregs[SAR]);
    r->pt.windowstart = tswap32(env->sregs[WINDOW_START]);
    r->pt.windowbase = tswap32(env->sregs[WINDOW_BASE]);
    r->pt.threadptr = tswap32(env->uregs[THREADPTR]);

    xtensa_sync_phys_from_window((CPUXtensaState *)env);

    for (unsigned i = 0; i < env->config->nareg; ++i) {
        r->pt.a[i] = tswap32(env->phys_regs[i]);
    }
}
