/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"
#include "target_elf.h"


void elf_core_copy_regs(target_elf_gregset_t *r, const CPUArchState *env)
{
    CPUSPARCState *e = (CPUSPARCState *)env;
    int i;

    memset(r, 0, sizeof(*r));

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    /* Linux kernel layout for sparc64 (arch/sparc/include/asm/elf_64.h):
     *   [0..7]   G0-G7
     *   [8..15]  O0-O7
     *   [16..23] L0-L7
     *   [24..31] I0-I7
     *   [32]     TSTATE
     *   [33]     TPC
     *   [34]     TNPC
     *   [35]     Y
     */
    for (i = 0; i < 8; i++) {
        r->regs[i]      = tswap64(env->gregs[i]);
        r->regs[8 + i]  = tswap64(env->regwptr[WREG_O0 + i]);
        r->regs[16 + i] = tswap64(env->regwptr[WREG_L0 + i]);
        r->regs[24 + i] = tswap64(env->regwptr[WREG_I0 + i]);
    }
    r->regs[32] = tswap64(sparc64_tstate(e));
    r->regs[33] = tswap64(env->pc);
    r->regs[34] = tswap64(env->npc);
    r->regs[35] = tswap64(env->y);
#else
    /* Linux kernel layout for sparc32 (arch/sparc/include/asm/elf_32.h):
     *   [0]      PSR
     *   [1]      PC
     *   [2]      NPC
     *   [3]      Y
     *   [4..11]  G0-G7
     *   [12..19] O0-O7
     *   [20..27] L0-L7
     *   [28..35] I0-I7
     *   [36..37] reserved (stack_check)
     */
    r->regs[0] = tswap32(cpu_get_psr(e));
    r->regs[1] = tswap32(env->pc);
    r->regs[2] = tswap32(env->npc);
    r->regs[3] = tswap32(env->y);
    for (i = 0; i < 8; i++) {
        r->regs[4 + i]  = tswap32(env->gregs[i]);
        r->regs[12 + i] = tswap32(env->regwptr[WREG_O0 + i]);
        r->regs[20 + i] = tswap32(env->regwptr[WREG_L0 + i]);
        r->regs[28 + i] = tswap32(env->regwptr[WREG_I0 + i]);
    }
#endif
}

const char *get_elf_cpu_model(uint32_t eflags)
{
#ifdef TARGET_SPARC64
    return "TI UltraSparc II";
#else
    return "Fujitsu MB86904";
#endif
}

abi_ulong get_elf_hwcap(CPUState *cs)
{
    /* There are not many sparc32 hwcap bits -- we have all of them. */
    uint32_t r = HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR |
                 HWCAP_SPARC_SWAP | HWCAP_SPARC_MULDIV;

#ifdef TARGET_SPARC64
    CPUSPARCState *env = cpu_env(cs);
    uint32_t features = env->def.features;

    r |= HWCAP_SPARC_V9 | HWCAP_SPARC_V8PLUS;
    /* 32x32 multiply and divide are efficient. */
    r |= HWCAP_SPARC_MUL32 | HWCAP_SPARC_DIV32;
    /* We don't have an internal feature bit for this. */
    r |= HWCAP_SPARC_POPC;
    r |= features & CPU_FEATURE_FSMULD ? HWCAP_SPARC_FSMULD : 0;
    r |= features & CPU_FEATURE_VIS1 ? HWCAP_SPARC_VIS : 0;
    r |= features & CPU_FEATURE_VIS2 ? HWCAP_SPARC_VIS2 : 0;
    r |= features & CPU_FEATURE_FMAF ? HWCAP_SPARC_FMAF : 0;
    r |= features & CPU_FEATURE_VIS3 ? HWCAP_SPARC_VIS3 : 0;
    r |= features & CPU_FEATURE_IMA ? HWCAP_SPARC_IMA : 0;
#endif

    return r;
}
