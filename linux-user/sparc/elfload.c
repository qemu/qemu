/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"


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
