/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
#ifdef TARGET_PPC64
    return "POWER9";
#else
    return "750";
#endif
}

/*
 * Feature masks for the Aux Vector Hardware Capabilities (AT_HWCAP).
 * See arch/powerpc/include/asm/cputable.h.
 */
enum {
    QEMU_PPC_FEATURE_32 = 0x80000000,
    QEMU_PPC_FEATURE_64 = 0x40000000,
    QEMU_PPC_FEATURE_601_INSTR = 0x20000000,
    QEMU_PPC_FEATURE_HAS_ALTIVEC = 0x10000000,
    QEMU_PPC_FEATURE_HAS_FPU = 0x08000000,
    QEMU_PPC_FEATURE_HAS_MMU = 0x04000000,
    QEMU_PPC_FEATURE_HAS_4xxMAC = 0x02000000,
    QEMU_PPC_FEATURE_UNIFIED_CACHE = 0x01000000,
    QEMU_PPC_FEATURE_HAS_SPE = 0x00800000,
    QEMU_PPC_FEATURE_HAS_EFP_SINGLE = 0x00400000,
    QEMU_PPC_FEATURE_HAS_EFP_DOUBLE = 0x00200000,
    QEMU_PPC_FEATURE_NO_TB = 0x00100000,
    QEMU_PPC_FEATURE_POWER4 = 0x00080000,
    QEMU_PPC_FEATURE_POWER5 = 0x00040000,
    QEMU_PPC_FEATURE_POWER5_PLUS = 0x00020000,
    QEMU_PPC_FEATURE_CELL = 0x00010000,
    QEMU_PPC_FEATURE_BOOKE = 0x00008000,
    QEMU_PPC_FEATURE_SMT = 0x00004000,
    QEMU_PPC_FEATURE_ICACHE_SNOOP = 0x00002000,
    QEMU_PPC_FEATURE_ARCH_2_05 = 0x00001000,
    QEMU_PPC_FEATURE_PA6T = 0x00000800,
    QEMU_PPC_FEATURE_HAS_DFP = 0x00000400,
    QEMU_PPC_FEATURE_POWER6_EXT = 0x00000200,
    QEMU_PPC_FEATURE_ARCH_2_06 = 0x00000100,
    QEMU_PPC_FEATURE_HAS_VSX = 0x00000080,
    QEMU_PPC_FEATURE_PSERIES_PERFMON_COMPAT = 0x00000040,

    QEMU_PPC_FEATURE_TRUE_LE = 0x00000002,
    QEMU_PPC_FEATURE_PPC_LE = 0x00000001,

    /* Feature definitions in AT_HWCAP2.  */
    QEMU_PPC_FEATURE2_ARCH_2_07 = 0x80000000, /* ISA 2.07 */
    QEMU_PPC_FEATURE2_HAS_HTM = 0x40000000, /* Hardware Transactional Memory */
    QEMU_PPC_FEATURE2_HAS_DSCR = 0x20000000, /* Data Stream Control Register */
    QEMU_PPC_FEATURE2_HAS_EBB = 0x10000000, /* Event Base Branching */
    QEMU_PPC_FEATURE2_HAS_ISEL = 0x08000000, /* Integer Select */
    QEMU_PPC_FEATURE2_HAS_TAR = 0x04000000, /* Target Address Register */
    QEMU_PPC_FEATURE2_VEC_CRYPTO = 0x02000000,
    QEMU_PPC_FEATURE2_HTM_NOSC = 0x01000000,
    QEMU_PPC_FEATURE2_ARCH_3_00 = 0x00800000, /* ISA 3.00 */
    QEMU_PPC_FEATURE2_HAS_IEEE128 = 0x00400000, /* VSX IEEE Bin Float 128-bit */
    QEMU_PPC_FEATURE2_DARN = 0x00200000, /* darn random number insn */
    QEMU_PPC_FEATURE2_SCV = 0x00100000, /* scv syscall */
    QEMU_PPC_FEATURE2_HTM_NO_SUSPEND = 0x00080000, /* TM w/o suspended state */
    QEMU_PPC_FEATURE2_ARCH_3_1 = 0x00040000, /* ISA 3.1 */
    QEMU_PPC_FEATURE2_MMA = 0x00020000, /* Matrix-Multiply Assist */
};

abi_ulong get_elf_hwcap(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    uint32_t features = 0;

    /*
     * We don't have to be terribly complete here; the high points are
     * Altivec/FP/SPE support.  Anything else is just a bonus.
     */
#define GET_FEATURE(flag, feature)                                      \
    do { if (cpu->env.insns_flags & flag) { features |= feature; } } while (0)
#define GET_FEATURE2(flags, feature) \
    do { \
        if ((cpu->env.insns_flags2 & flags) == flags) { \
            features |= feature; \
        } \
    } while (0)
    GET_FEATURE(PPC_64B, QEMU_PPC_FEATURE_64);
    GET_FEATURE(PPC_FLOAT, QEMU_PPC_FEATURE_HAS_FPU);
    GET_FEATURE(PPC_ALTIVEC, QEMU_PPC_FEATURE_HAS_ALTIVEC);
    GET_FEATURE(PPC_SPE, QEMU_PPC_FEATURE_HAS_SPE);
    GET_FEATURE(PPC_SPE_SINGLE, QEMU_PPC_FEATURE_HAS_EFP_SINGLE);
    GET_FEATURE(PPC_SPE_DOUBLE, QEMU_PPC_FEATURE_HAS_EFP_DOUBLE);
    GET_FEATURE(PPC_BOOKE, QEMU_PPC_FEATURE_BOOKE);
    GET_FEATURE(PPC_405_MAC, QEMU_PPC_FEATURE_HAS_4xxMAC);
    GET_FEATURE2(PPC2_DFP, QEMU_PPC_FEATURE_HAS_DFP);
    GET_FEATURE2(PPC2_VSX, QEMU_PPC_FEATURE_HAS_VSX);
    GET_FEATURE2((PPC2_PERM_ISA206 | PPC2_DIVE_ISA206 | PPC2_ATOMIC_ISA206 |
                  PPC2_FP_CVT_ISA206 | PPC2_FP_TST_ISA206),
                  QEMU_PPC_FEATURE_ARCH_2_06);

#undef GET_FEATURE
#undef GET_FEATURE2

    return features;
}

abi_ulong get_elf_hwcap2(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    uint32_t features = 0;

#define GET_FEATURE(flag, feature)                                      \
    do { if (cpu->env.insns_flags & flag) { features |= feature; } } while (0)
#define GET_FEATURE2(flag, feature)                                      \
    do { if (cpu->env.insns_flags2 & flag) { features |= feature; } } while (0)

    GET_FEATURE(PPC_ISEL, QEMU_PPC_FEATURE2_HAS_ISEL);
    GET_FEATURE2(PPC2_BCTAR_ISA207, QEMU_PPC_FEATURE2_HAS_TAR);
    GET_FEATURE2((PPC2_BCTAR_ISA207 | PPC2_LSQ_ISA207 | PPC2_ALTIVEC_207 |
                  PPC2_ISA207S), QEMU_PPC_FEATURE2_ARCH_2_07 |
                  QEMU_PPC_FEATURE2_VEC_CRYPTO);
    GET_FEATURE2(PPC2_ISA300, QEMU_PPC_FEATURE2_ARCH_3_00 |
                 QEMU_PPC_FEATURE2_DARN | QEMU_PPC_FEATURE2_HAS_IEEE128);
    GET_FEATURE2(PPC2_ISA310, QEMU_PPC_FEATURE2_ARCH_3_1 |
                 QEMU_PPC_FEATURE2_MMA);

#undef GET_FEATURE
#undef GET_FEATURE2

    return features;
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUPPCState *env)
{
    for (int i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        r->pt.gpr[i] = tswapal(env->gpr[i]);
    }

    r->pt.nip = tswapal(env->nip);
    r->pt.msr = tswapal(env->msr);
    r->pt.ctr = tswapal(env->ctr);
    r->pt.link = tswapal(env->lr);
    r->pt.xer = tswapal(cpu_read_xer(env));
    r->pt.ccr = tswapal(ppc_get_cr(env));
}
