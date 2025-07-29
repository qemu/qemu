/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "sh7785";
}

enum {
    SH_CPU_HAS_FPU            = 0x0001, /* Hardware FPU support */
    SH_CPU_HAS_P2_FLUSH_BUG   = 0x0002, /* Need to flush the cache in P2 area */
    SH_CPU_HAS_MMU_PAGE_ASSOC = 0x0004, /* SH3: TLB way selection bit support */
    SH_CPU_HAS_DSP            = 0x0008, /* SH-DSP: DSP support */
    SH_CPU_HAS_PERF_COUNTER   = 0x0010, /* Hardware performance counters */
    SH_CPU_HAS_PTEA           = 0x0020, /* PTEA register */
    SH_CPU_HAS_LLSC           = 0x0040, /* movli.l/movco.l */
    SH_CPU_HAS_L2_CACHE       = 0x0080, /* Secondary cache / URAM */
    SH_CPU_HAS_OP32           = 0x0100, /* 32-bit instruction support */
    SH_CPU_HAS_PTEAEX         = 0x0200, /* PTE ASID Extension support */
};

abi_ulong get_elf_hwcap(CPUState *cs)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    abi_ulong hwcap = 0;

    hwcap |= SH_CPU_HAS_FPU;

    if (cpu->env.features & SH_FEATURE_SH4A) {
        hwcap |= SH_CPU_HAS_LLSC;
    }

    return hwcap;
}

#define tswapreg(ptr)   tswapal(ptr)

/* See linux kernel: arch/sh/include/asm/ptrace.h.  */
enum {
    TARGET_REG_PC = 16,
    TARGET_REG_PR = 17,
    TARGET_REG_SR = 18,
    TARGET_REG_GBR = 19,
    TARGET_REG_MACH = 20,
    TARGET_REG_MACL = 21,
    TARGET_REG_SYSCALL = 22
};

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUSH4State *env)
{
    for (int i = 0; i < 16; i++) {
        r->regs[i] = tswapreg(env->gregs[i]);
    }

    r->regs[TARGET_REG_PC] = tswapreg(env->pc);
    r->regs[TARGET_REG_PR] = tswapreg(env->pr);
    r->regs[TARGET_REG_SR] = tswapreg(env->sr);
    r->regs[TARGET_REG_GBR] = tswapreg(env->gbr);
    r->regs[TARGET_REG_MACH] = tswapreg(env->mach);
    r->regs[TARGET_REG_MACL] = tswapreg(env->macl);
    r->regs[TARGET_REG_SYSCALL] = 0; /* FIXME */
}
