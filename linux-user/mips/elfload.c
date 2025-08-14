/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "elf.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
#ifdef TARGET_MIPS64
    switch (eflags & EF_MIPS_MACH) {
    case EF_MIPS_MACH_OCTEON:
    case EF_MIPS_MACH_OCTEON2:
    case EF_MIPS_MACH_OCTEON3:
        return "Octeon68XX";
    case EF_MIPS_MACH_LS2E:
        return "Loongson-2E";
    case EF_MIPS_MACH_LS2F:
        return "Loongson-2F";
    case EF_MIPS_MACH_LS3A:
        return "Loongson-3A1000";
    default:
        break;
    }
    switch (eflags & EF_MIPS_ARCH) {
    case EF_MIPS_ARCH_64R6:
        return "I6400";
    case EF_MIPS_ARCH_64R2:
        return "MIPS64R2-generic";
    default:
        break;
    }
    return "5KEf";
#else
    if ((eflags & EF_MIPS_ARCH) == EF_MIPS_ARCH_32R6) {
        return "mips32r6-generic";
    }
    if ((eflags & EF_MIPS_ARCH_ASE) == EF_MIPS_ARCH_ASE_MICROMIPS) {
        return "M14Kc";
    }
    if ((eflags & EF_MIPS_ARCH_ASE) == EF_MIPS_ARCH_ASE_M16) {
        return "74Kf";
    }
    if (eflags & EF_MIPS_NAN2008) {
        return "P5600";
    }
    return "24Kf";
#endif
}

/* See arch/mips/include/uapi/asm/hwcap.h.  */
enum {
    HWCAP_MIPS_R6           = (1 << 0),
    HWCAP_MIPS_MSA          = (1 << 1),
    HWCAP_MIPS_CRC32        = (1 << 2),
    HWCAP_MIPS_MIPS16       = (1 << 3),
    HWCAP_MIPS_MDMX         = (1 << 4),
    HWCAP_MIPS_MIPS3D       = (1 << 5),
    HWCAP_MIPS_SMARTMIPS    = (1 << 6),
    HWCAP_MIPS_DSP          = (1 << 7),
    HWCAP_MIPS_DSP2         = (1 << 8),
    HWCAP_MIPS_DSP3         = (1 << 9),
    HWCAP_MIPS_MIPS16E2     = (1 << 10),
    HWCAP_LOONGSON_MMI      = (1 << 11),
    HWCAP_LOONGSON_EXT      = (1 << 12),
    HWCAP_LOONGSON_EXT2     = (1 << 13),
    HWCAP_LOONGSON_CPUCFG   = (1 << 14),
};

#define GET_FEATURE_INSN(_flag, _hwcap) \
    do { if (cpu->env.insn_flags & (_flag)) { hwcaps |= _hwcap; } } while (0)

#define GET_FEATURE_REG_SET(_reg, _mask, _hwcap) \
    do { if (cpu->env._reg & (_mask)) { hwcaps |= _hwcap; } } while (0)

#define GET_FEATURE_REG_EQU(_reg, _start, _length, _val, _hwcap) \
    do { \
        if (extract32(cpu->env._reg, (_start), (_length)) == (_val)) { \
            hwcaps |= _hwcap; \
        } \
    } while (0)

abi_ulong get_elf_hwcap(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    abi_ulong hwcaps = 0;

    GET_FEATURE_REG_EQU(CP0_Config0, CP0C0_AR, CP0C0_AR_LENGTH,
                        2, HWCAP_MIPS_R6);
    GET_FEATURE_REG_SET(CP0_Config3, 1 << CP0C3_MSAP, HWCAP_MIPS_MSA);
    GET_FEATURE_INSN(ASE_LMMI, HWCAP_LOONGSON_MMI);
    GET_FEATURE_INSN(ASE_LEXT, HWCAP_LOONGSON_EXT);

    return hwcaps;
}

#undef GET_FEATURE_REG_EQU
#undef GET_FEATURE_REG_SET
#undef GET_FEATURE_INSN

#define MATCH_PLATFORM_INSN(_flags, _base_platform)      \
    do { if ((cpu->env.insn_flags & (_flags)) == _flags) \
    { return _base_platform; } } while (0)

const char *get_elf_base_platform(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);

    /* 64 bit ISAs goes first */
    MATCH_PLATFORM_INSN(CPU_MIPS64R6, "mips64r6");
    MATCH_PLATFORM_INSN(CPU_MIPS64R5, "mips64r5");
    MATCH_PLATFORM_INSN(CPU_MIPS64R2, "mips64r2");
    MATCH_PLATFORM_INSN(CPU_MIPS64R1, "mips64");
    MATCH_PLATFORM_INSN(CPU_MIPS5, "mips5");
    MATCH_PLATFORM_INSN(CPU_MIPS4, "mips4");
    MATCH_PLATFORM_INSN(CPU_MIPS3, "mips3");

    /* 32 bit ISAs */
    MATCH_PLATFORM_INSN(CPU_MIPS32R6, "mips32r6");
    MATCH_PLATFORM_INSN(CPU_MIPS32R5, "mips32r5");
    MATCH_PLATFORM_INSN(CPU_MIPS32R2, "mips32r2");
    MATCH_PLATFORM_INSN(CPU_MIPS32R1, "mips32");
    MATCH_PLATFORM_INSN(CPU_MIPS2, "mips2");

    /* Fallback */
    return "mips";
}

#undef MATCH_PLATFORM_INSN

/* See linux kernel: arch/mips/kernel/process.c:elf_dump_regs.  */
void elf_core_copy_regs(target_elf_gregset_t *r, const CPUMIPSState *env)
{
    for (int i = 1; i < ARRAY_SIZE(env->active_tc.gpr); i++) {
        r->pt.regs[i] = tswapl(env->active_tc.gpr[i]);
    }

    r->pt.regs[26] = 0;
    r->pt.regs[27] = 0;
    r->pt.lo = tswapl(env->active_tc.LO[0]);
    r->pt.hi = tswapl(env->active_tc.HI[0]);
    r->pt.cp0_epc = tswapl(env->active_tc.PC);
    r->pt.cp0_badvaddr = tswapl(env->CP0_BadVAddr);
    r->pt.cp0_status = tswapl(env->CP0_Status);
    r->pt.cp0_cause = tswapl(env->CP0_Cause);
}
