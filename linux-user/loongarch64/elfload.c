/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "la464";
}

/* See arch/loongarch/include/uapi/asm/hwcap.h */
enum {
    HWCAP_LOONGARCH_CPUCFG   = (1 << 0),
    HWCAP_LOONGARCH_LAM      = (1 << 1),
    HWCAP_LOONGARCH_UAL      = (1 << 2),
    HWCAP_LOONGARCH_FPU      = (1 << 3),
    HWCAP_LOONGARCH_LSX      = (1 << 4),
    HWCAP_LOONGARCH_LASX     = (1 << 5),
    HWCAP_LOONGARCH_CRC32    = (1 << 6),
    HWCAP_LOONGARCH_COMPLEX  = (1 << 7),
    HWCAP_LOONGARCH_CRYPTO   = (1 << 8),
    HWCAP_LOONGARCH_LVZ      = (1 << 9),
    HWCAP_LOONGARCH_LBT_X86  = (1 << 10),
    HWCAP_LOONGARCH_LBT_ARM  = (1 << 11),
    HWCAP_LOONGARCH_LBT_MIPS = (1 << 12),
};

abi_ulong get_elf_hwcap(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    abi_ulong hwcaps = 0;

    hwcaps |= HWCAP_LOONGARCH_CRC32;

    if (FIELD_EX32(cpu->env.cpucfg[1], CPUCFG1, UAL)) {
        hwcaps |= HWCAP_LOONGARCH_UAL;
    }

    if (FIELD_EX32(cpu->env.cpucfg[2], CPUCFG2, FP)) {
        hwcaps |= HWCAP_LOONGARCH_FPU;
    }

    if (FIELD_EX32(cpu->env.cpucfg[2], CPUCFG2, LAM)) {
        hwcaps |= HWCAP_LOONGARCH_LAM;
    }

    if (FIELD_EX32(cpu->env.cpucfg[2], CPUCFG2, LSX)) {
        hwcaps |= HWCAP_LOONGARCH_LSX;
    }

    if (FIELD_EX32(cpu->env.cpucfg[2], CPUCFG2, LASX)) {
        hwcaps |= HWCAP_LOONGARCH_LASX;
    }

    return hwcaps;
}

const char *get_elf_platform(CPUState *cs)
{
    return "loongarch";
}

#define tswapreg(ptr)   tswapal(ptr)

void elf_core_copy_regs(target_elf_gregset_t *r, const CPULoongArchState *env)
{
    r->pt.regs[0] = 0;

    for (int i = 1; i < ARRAY_SIZE(env->gpr); i++) {
        r->pt.regs[i] = tswapreg(env->gpr[i]);
    }

    r->pt.csr_era = tswapreg(env->pc);
    r->pt.csr_badv = tswapreg(env->CSR_BADV);
}
