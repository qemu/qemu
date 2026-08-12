/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "max";
}

void elf_core_copy_fpregs(target_elf_fpregset_t *r, const CPURISCVState *env)
{
    for (int i = 0; i < 32; i++) {
        r->fpr[i] = tswap64(env->fpr[i]);
    }
    r->fcsr = tswap32(((uint32_t)env->frm << FSR_RD_SHIFT) |
                      (riscv_cpu_get_fflags((CPURISCVState *)env) << FSR_AEXC_SHIFT));
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPURISCVState *env)
{
    r->pc = tswapal(env->pc);
    for (int i = 0; i < 31; i++) {
        r->regs[i] = tswapal(env->gpr[i + 1]);
    }
}

abi_ulong get_elf_hwcap(CPUState *cs)
{
#define MISA_BIT(EXT) (1 << (EXT - 'A'))
    RISCVCPU *cpu = RISCV_CPU(cs);
    uint32_t mask = MISA_BIT('I') | MISA_BIT('M') | MISA_BIT('A')
                    | MISA_BIT('F') | MISA_BIT('D') | MISA_BIT('C')
                    | MISA_BIT('V');

    return cpu->env.misa_ext & mask;
#undef MISA_BIT
}
