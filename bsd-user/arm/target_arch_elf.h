/*
 *  arm ELF definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_ELF_H
#define TARGET_ARCH_ELF_H

#define ELF_ET_DYN_LOAD_ADDR    0x500000

#define elf_check_arch(x) ((x) == EM_ARM)

#define ELF_CLASS       ELFCLASS32
#define ELF_DATA        ELFDATA2LSB
#define ELF_ARCH        EM_ARM

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

#define ELF_HWCAP get_elf_hwcap()
#define ELF_HWCAP2 get_elf_hwcap2()

#define GET_FEATURE(feat, hwcap) \
    do { if (arm_feature(&cpu->env, feat)) { hwcaps |= hwcap; } } while (0)

#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

enum {
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
};

enum {
    ARM_HWCAP2_ARM_AES      = 1 << 0,
    ARM_HWCAP2_ARM_PMULL    = 1 << 1,
    ARM_HWCAP2_ARM_SHA1     = 1 << 2,
    ARM_HWCAP2_ARM_SHA2     = 1 << 3,
    ARM_HWCAP2_ARM_CRC32    = 1 << 4,
};

static uint32_t get_elf_hwcap(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    hwcaps |= ARM_HWCAP_ARM_SWP;
    hwcaps |= ARM_HWCAP_ARM_HALF;
    hwcaps |= ARM_HWCAP_ARM_THUMB;
    hwcaps |= ARM_HWCAP_ARM_FAST_MULT;

    /* probe for the extra features */
    /* EDSP is in v5TE and above */
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

    return hwcaps;
}

static uint32_t get_elf_hwcap2(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    GET_FEATURE_ID(aa32_aes, ARM_HWCAP2_ARM_AES);
    GET_FEATURE_ID(aa32_pmull, ARM_HWCAP2_ARM_PMULL);
    GET_FEATURE_ID(aa32_sha1, ARM_HWCAP2_ARM_SHA1);
    GET_FEATURE_ID(aa32_sha2, ARM_HWCAP2_ARM_SHA2);
    GET_FEATURE_ID(aa32_crc32, ARM_HWCAP2_ARM_CRC32);
    return hwcaps;
}

#undef GET_FEATURE
#undef GET_FEATURE_ID

#endif /* TARGET_ARCH_ELF_H */
