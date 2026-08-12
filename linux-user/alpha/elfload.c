/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


abi_ulong get_elf_hwcap(CPUState *cs)
{
    /*
     * The Linux kernel computes ELF_HWCAP as ~amask(-1), which clears a bit
     * for each supported ISA extension.  env->amask stores exactly those bits
     * set for the extensions supported by the emulated CPU model, matching
     * the kernel's convention: bit set in AT_HWCAP ↔ extension present.
     */
    return cpu_env(cs)->amask;
}

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

void elf_core_copy_fpregs(target_elf_fpregset_t *r, const CPUAlphaState *env)
{
    int i;

    for (i = 0; i < 31; i++) {
        r->fpr[i] = tswap64(env->fir[i]);
    }
    r->fpcr = tswap64(cpu_alpha_load_fpcr((CPUAlphaState *)env));
}
