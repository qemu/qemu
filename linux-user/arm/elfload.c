/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "user-internals.h"
#include "target_elf.h"
#include "target/arm/cpu-features.h"
#include "target_elf.h"
#include "elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}

enum
{
    ARM_HWCAP_ARM_SWP       = 1 << 0,
    ARM_HWCAP_ARM_HALF      = 1 << 1,
    ARM_HWCAP_ARM_THUMB     = 1 << 2,
    ARM_HWCAP_ARM_26BIT     = 1 << 3,
    ARM_HWCAP_ARM_FAST_MULT = 1 << 4,
    ARM_HWCAP_ARM_FPA       = 1 << 5,
    ARM_HWCAP_ARM_VFP       = 1 << 6,
    ARM_HWCAP_ARM_EDSP      = 1 << 7,
    ARM_HWCAP_ARM_JAVA      = 1 << 8,
    ARM_HWCAP_ARM_IWMMXT    = 1 << 9,
    ARM_HWCAP_ARM_CRUNCH    = 1 << 10,
    ARM_HWCAP_ARM_THUMBEE   = 1 << 11,
    ARM_HWCAP_ARM_NEON      = 1 << 12,
    ARM_HWCAP_ARM_VFPv3     = 1 << 13,
    ARM_HWCAP_ARM_VFPv3D16  = 1 << 14,
    ARM_HWCAP_ARM_TLS       = 1 << 15,
    ARM_HWCAP_ARM_VFPv4     = 1 << 16,
    ARM_HWCAP_ARM_IDIVA     = 1 << 17,
    ARM_HWCAP_ARM_IDIVT     = 1 << 18,
    ARM_HWCAP_ARM_VFPD32    = 1 << 19,
    ARM_HWCAP_ARM_LPAE      = 1 << 20,
    ARM_HWCAP_ARM_EVTSTRM   = 1 << 21,
    ARM_HWCAP_ARM_FPHP      = 1 << 22,
    ARM_HWCAP_ARM_ASIMDHP   = 1 << 23,
    ARM_HWCAP_ARM_ASIMDDP   = 1 << 24,
    ARM_HWCAP_ARM_ASIMDFHM  = 1 << 25,
    ARM_HWCAP_ARM_ASIMDBF16 = 1 << 26,
    ARM_HWCAP_ARM_I8MM      = 1 << 27,
};

enum {
    ARM_HWCAP2_ARM_AES      = 1 << 0,
    ARM_HWCAP2_ARM_PMULL    = 1 << 1,
    ARM_HWCAP2_ARM_SHA1     = 1 << 2,
    ARM_HWCAP2_ARM_SHA2     = 1 << 3,
    ARM_HWCAP2_ARM_CRC32    = 1 << 4,
    ARM_HWCAP2_ARM_SB       = 1 << 5,
    ARM_HWCAP2_ARM_SSBS     = 1 << 6,
};

abi_ulong get_elf_hwcap(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    abi_ulong hwcaps = 0;

    hwcaps |= ARM_HWCAP_ARM_SWP;
    hwcaps |= ARM_HWCAP_ARM_HALF;
    hwcaps |= ARM_HWCAP_ARM_THUMB;
    hwcaps |= ARM_HWCAP_ARM_FAST_MULT;

    /* probe for the extra features */
#define GET_FEATURE(feat, hwcap) \
    do { if (arm_feature(&cpu->env, feat)) { hwcaps |= hwcap; } } while (0)

#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

    /* EDSP is in v5TE and above, but all our v5 CPUs are v5TE */
    GET_FEATURE(ARM_FEATURE_V5, ARM_HWCAP_ARM_EDSP);
    GET_FEATURE(ARM_FEATURE_THUMB2EE, ARM_HWCAP_ARM_THUMBEE);
    GET_FEATURE(ARM_FEATURE_NEON, ARM_HWCAP_ARM_NEON);
    GET_FEATURE(ARM_FEATURE_V6K, ARM_HWCAP_ARM_TLS);
    GET_FEATURE(ARM_FEATURE_LPAE, ARM_HWCAP_ARM_LPAE);
    GET_FEATURE_ID(aa32_arm_div, ARM_HWCAP_ARM_IDIVA);
    GET_FEATURE_ID(aa32_thumb_div, ARM_HWCAP_ARM_IDIVT);
    GET_FEATURE_ID(aa32_vfp, ARM_HWCAP_ARM_VFP);

    if (cpu_isar_feature(aa32_fpsp_v3, cpu) ||
        cpu_isar_feature(aa32_fpdp_v3, cpu)) {
        hwcaps |= ARM_HWCAP_ARM_VFPv3;
        if (cpu_isar_feature(aa32_simd_r32, cpu)) {
            hwcaps |= ARM_HWCAP_ARM_VFPD32;
        } else {
            hwcaps |= ARM_HWCAP_ARM_VFPv3D16;
        }
    }
    GET_FEATURE_ID(aa32_simdfmac, ARM_HWCAP_ARM_VFPv4);
    /*
     * MVFR1.FPHP and .SIMDHP must be in sync, and QEMU uses the same
     * isar_feature function for both. The kernel reports them as two hwcaps.
     */
    GET_FEATURE_ID(aa32_fp16_arith, ARM_HWCAP_ARM_FPHP);
    GET_FEATURE_ID(aa32_fp16_arith, ARM_HWCAP_ARM_ASIMDHP);
    GET_FEATURE_ID(aa32_dp, ARM_HWCAP_ARM_ASIMDDP);
    GET_FEATURE_ID(aa32_fhm, ARM_HWCAP_ARM_ASIMDFHM);
    GET_FEATURE_ID(aa32_bf16, ARM_HWCAP_ARM_ASIMDBF16);
    GET_FEATURE_ID(aa32_i8mm, ARM_HWCAP_ARM_I8MM);

    return hwcaps;
}

abi_ulong get_elf_hwcap2(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    abi_ulong hwcaps = 0;

    GET_FEATURE_ID(aa32_aes, ARM_HWCAP2_ARM_AES);
    GET_FEATURE_ID(aa32_pmull, ARM_HWCAP2_ARM_PMULL);
    GET_FEATURE_ID(aa32_sha1, ARM_HWCAP2_ARM_SHA1);
    GET_FEATURE_ID(aa32_sha2, ARM_HWCAP2_ARM_SHA2);
    GET_FEATURE_ID(aa32_crc32, ARM_HWCAP2_ARM_CRC32);
    GET_FEATURE_ID(aa32_sb, ARM_HWCAP2_ARM_SB);
    GET_FEATURE_ID(aa32_ssbs, ARM_HWCAP2_ARM_SSBS);
    return hwcaps;
}

const char *elf_hwcap_str(uint32_t bit)
{
    static const char *hwcap_str[] = {
    [__builtin_ctz(ARM_HWCAP_ARM_SWP      )] = "swp",
    [__builtin_ctz(ARM_HWCAP_ARM_HALF     )] = "half",
    [__builtin_ctz(ARM_HWCAP_ARM_THUMB    )] = "thumb",
    [__builtin_ctz(ARM_HWCAP_ARM_26BIT    )] = "26bit",
    [__builtin_ctz(ARM_HWCAP_ARM_FAST_MULT)] = "fast_mult",
    [__builtin_ctz(ARM_HWCAP_ARM_FPA      )] = "fpa",
    [__builtin_ctz(ARM_HWCAP_ARM_VFP      )] = "vfp",
    [__builtin_ctz(ARM_HWCAP_ARM_EDSP     )] = "edsp",
    [__builtin_ctz(ARM_HWCAP_ARM_JAVA     )] = "java",
    [__builtin_ctz(ARM_HWCAP_ARM_IWMMXT   )] = "iwmmxt",
    [__builtin_ctz(ARM_HWCAP_ARM_CRUNCH   )] = "crunch",
    [__builtin_ctz(ARM_HWCAP_ARM_THUMBEE  )] = "thumbee",
    [__builtin_ctz(ARM_HWCAP_ARM_NEON     )] = "neon",
    [__builtin_ctz(ARM_HWCAP_ARM_VFPv3    )] = "vfpv3",
    [__builtin_ctz(ARM_HWCAP_ARM_VFPv3D16 )] = "vfpv3d16",
    [__builtin_ctz(ARM_HWCAP_ARM_TLS      )] = "tls",
    [__builtin_ctz(ARM_HWCAP_ARM_VFPv4    )] = "vfpv4",
    [__builtin_ctz(ARM_HWCAP_ARM_IDIVA    )] = "idiva",
    [__builtin_ctz(ARM_HWCAP_ARM_IDIVT    )] = "idivt",
    [__builtin_ctz(ARM_HWCAP_ARM_VFPD32   )] = "vfpd32",
    [__builtin_ctz(ARM_HWCAP_ARM_LPAE     )] = "lpae",
    [__builtin_ctz(ARM_HWCAP_ARM_EVTSTRM  )] = "evtstrm",
    [__builtin_ctz(ARM_HWCAP_ARM_FPHP     )] = "fphp",
    [__builtin_ctz(ARM_HWCAP_ARM_ASIMDHP  )] = "asimdhp",
    [__builtin_ctz(ARM_HWCAP_ARM_ASIMDDP  )] = "asimddp",
    [__builtin_ctz(ARM_HWCAP_ARM_ASIMDFHM )] = "asimdfhm",
    [__builtin_ctz(ARM_HWCAP_ARM_ASIMDBF16)] = "asimdbf16",
    [__builtin_ctz(ARM_HWCAP_ARM_I8MM     )] = "i8mm",
    };

    return bit < ARRAY_SIZE(hwcap_str) ? hwcap_str[bit] : NULL;
}

const char *elf_hwcap2_str(uint32_t bit)
{
    static const char *hwcap_str[] = {
    [__builtin_ctz(ARM_HWCAP2_ARM_AES  )] = "aes",
    [__builtin_ctz(ARM_HWCAP2_ARM_PMULL)] = "pmull",
    [__builtin_ctz(ARM_HWCAP2_ARM_SHA1 )] = "sha1",
    [__builtin_ctz(ARM_HWCAP2_ARM_SHA2 )] = "sha2",
    [__builtin_ctz(ARM_HWCAP2_ARM_CRC32)] = "crc32",
    [__builtin_ctz(ARM_HWCAP2_ARM_SB   )] = "sb",
    [__builtin_ctz(ARM_HWCAP2_ARM_SSBS )] = "ssbs",
    };

    return bit < ARRAY_SIZE(hwcap_str) ? hwcap_str[bit] : NULL;
}

const char *get_elf_platform(CPUState *cs)
{
    CPUARMState *env = cpu_env(cs);

#if TARGET_BIG_ENDIAN
# define END  "b"
#else
# define END  "l"
#endif

    if (arm_feature(env, ARM_FEATURE_V8)) {
        return "v8" END;
    } else if (arm_feature(env, ARM_FEATURE_V7)) {
        if (arm_feature(env, ARM_FEATURE_M)) {
            return "v7m" END;
        } else {
            return "v7" END;
        }
    } else if (arm_feature(env, ARM_FEATURE_V6)) {
        return "v6" END;
    } else if (arm_feature(env, ARM_FEATURE_V5)) {
        return "v5" END;
    } else {
        return "v4" END;
    }

#undef END
}

bool init_guest_commpage(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    int host_page_size = qemu_real_host_page_size();
    abi_ptr commpage;
    void *want;
    void *addr;

    /*
     * M-profile allocates maximum of 2GB address space, so can never
     * allocate the commpage.  Skip it.
     */
    if (arm_feature(&cpu->env, ARM_FEATURE_M)) {
        return true;
    }

    commpage = HI_COMMPAGE & -host_page_size;
    want = g2h_untagged(commpage);
    addr = mmap(want, host_page_size, PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE |
                (commpage < reserved_va ? MAP_FIXED : MAP_FIXED_NOREPLACE),
                -1, 0);

    if (addr == MAP_FAILED) {
        perror("Allocating guest commpage");
        exit(EXIT_FAILURE);
    }
    if (addr != want) {
        return false;
    }

    /* Set kernel helper versions; rest of page is 0.  */
    __put_user(5, (uint32_t *)g2h_untagged(0xffff0ffcu));

    if (mprotect(addr, host_page_size, PROT_READ)) {
        perror("Protecting guest commpage");
        exit(EXIT_FAILURE);
    }

    page_set_flags(commpage, commpage | (host_page_size - 1),
                   PAGE_READ | PAGE_EXEC | PAGE_VALID, PAGE_VALID);
    return true;
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUARMState *env)
{
    for (int i = 0; i < 16; ++i) {
        r->pt.regs[i] = tswapal(env->regs[i]);
    }
    r->pt.cpsr = tswapal(cpsr_read((CPUARMState *)env));
    r->pt.orig_r0 = tswapal(env->regs[0]); /* FIXME */
}

#if TARGET_BIG_ENDIAN
# include "vdso-be8.c.inc"
# include "vdso-be32.c.inc"
#else
# include "vdso-le.c.inc"
#endif

const VdsoImageInfo *get_vdso_image_info(uint32_t elf_flags)
{
#if TARGET_BIG_ENDIAN
    return (EF_ARM_EABI_VERSION(elf_flags) >= EF_ARM_EABI_VER4
            && (elf_flags & EF_ARM_BE8)
            ? &vdso_be8_image_info
            : &vdso_be32_image_info);
#else
    return &vdso_image_info;
#endif
}
