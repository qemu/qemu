/*
 * ARM AArch64 ELF definitions for bsd-user
 *
 * Copyright (c) 2015 Stacey D. Son
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARCH_ELF_H
#define TARGET_ARCH_ELF_H

#define ELF_START_MMAP 0x80000000
#define ELF_ET_DYN_LOAD_ADDR    0x100000

#define elf_check_arch(x) ((x) == EM_AARCH64)

#define ELF_CLASS       ELFCLASS64
#define ELF_DATA        ELFDATA2LSB
#define ELF_ARCH        EM_AARCH64

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE       4096

enum {
    ARM_HWCAP_A64_FP            = 1 << 0,
    ARM_HWCAP_A64_ASIMD         = 1 << 1,
    ARM_HWCAP_A64_EVTSTRM       = 1 << 2,
    ARM_HWCAP_A64_AES           = 1 << 3,
    ARM_HWCAP_A64_PMULL         = 1 << 4,
    ARM_HWCAP_A64_SHA1          = 1 << 5,
    ARM_HWCAP_A64_SHA2          = 1 << 6,
    ARM_HWCAP_A64_CRC32         = 1 << 7,
    ARM_HWCAP_A64_ATOMICS       = 1 << 8,
    ARM_HWCAP_A64_FPHP          = 1 << 9,
    ARM_HWCAP_A64_ASIMDHP       = 1 << 10,
    ARM_HWCAP_A64_CPUID         = 1 << 11,
    ARM_HWCAP_A64_ASIMDRDM      = 1 << 12,
    ARM_HWCAP_A64_JSCVT         = 1 << 13,
    ARM_HWCAP_A64_FCMA          = 1 << 14,
    ARM_HWCAP_A64_LRCPC         = 1 << 15,
    ARM_HWCAP_A64_DCPOP         = 1 << 16,
    ARM_HWCAP_A64_SHA3          = 1 << 17,
    ARM_HWCAP_A64_SM3           = 1 << 18,
    ARM_HWCAP_A64_SM4           = 1 << 19,
    ARM_HWCAP_A64_ASIMDDP       = 1 << 20,
    ARM_HWCAP_A64_SHA512        = 1 << 21,
    ARM_HWCAP_A64_SVE           = 1 << 22,
    ARM_HWCAP_A64_ASIMDFHM      = 1 << 23,
    ARM_HWCAP_A64_DIT           = 1 << 24,
    ARM_HWCAP_A64_USCAT         = 1 << 25,
    ARM_HWCAP_A64_ILRCPC        = 1 << 26,
    ARM_HWCAP_A64_FLAGM         = 1 << 27,
    ARM_HWCAP_A64_SSBS          = 1 << 28,
    ARM_HWCAP_A64_SB            = 1 << 29,
    ARM_HWCAP_A64_PACA          = 1 << 30,
    ARM_HWCAP_A64_PACG          = 1UL << 31,

    ARM_HWCAP2_A64_DCPODP       = 1 << 0,
    ARM_HWCAP2_A64_SVE2         = 1 << 1,
    ARM_HWCAP2_A64_SVEAES       = 1 << 2,
    ARM_HWCAP2_A64_SVEPMULL     = 1 << 3,
    ARM_HWCAP2_A64_SVEBITPERM   = 1 << 4,
    ARM_HWCAP2_A64_SVESHA3      = 1 << 5,
    ARM_HWCAP2_A64_SVESM4       = 1 << 6,
    ARM_HWCAP2_A64_FLAGM2       = 1 << 7,
    ARM_HWCAP2_A64_FRINT        = 1 << 8,
    ARM_HWCAP2_A64_SVEI8MM      = 1 << 9,
    ARM_HWCAP2_A64_SVEF32MM     = 1 << 10,
    ARM_HWCAP2_A64_SVEF64MM     = 1 << 11,
    ARM_HWCAP2_A64_SVEBF16      = 1 << 12,
    ARM_HWCAP2_A64_I8MM         = 1 << 13,
    ARM_HWCAP2_A64_BF16         = 1 << 14,
    ARM_HWCAP2_A64_DGH          = 1 << 15,
    ARM_HWCAP2_A64_RNG          = 1 << 16,
    ARM_HWCAP2_A64_BTI          = 1 << 17,
    ARM_HWCAP2_A64_MTE          = 1 << 18,
};

#define ELF_HWCAP   get_elf_hwcap()
#define ELF_HWCAP2  get_elf_hwcap2()

#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

static uint32_t get_elf_hwcap(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    hwcaps |= ARM_HWCAP_A64_FP;
    hwcaps |= ARM_HWCAP_A64_ASIMD;
    hwcaps |= ARM_HWCAP_A64_CPUID;

    /* probe for the extra features */

    GET_FEATURE_ID(aa64_aes, ARM_HWCAP_A64_AES);
    GET_FEATURE_ID(aa64_pmull, ARM_HWCAP_A64_PMULL);
    GET_FEATURE_ID(aa64_sha1, ARM_HWCAP_A64_SHA1);
    GET_FEATURE_ID(aa64_sha256, ARM_HWCAP_A64_SHA2);
    GET_FEATURE_ID(aa64_sha512, ARM_HWCAP_A64_SHA512);
    GET_FEATURE_ID(aa64_crc32, ARM_HWCAP_A64_CRC32);
    GET_FEATURE_ID(aa64_sha3, ARM_HWCAP_A64_SHA3);
    GET_FEATURE_ID(aa64_sm3, ARM_HWCAP_A64_SM3);
    GET_FEATURE_ID(aa64_sm4, ARM_HWCAP_A64_SM4);
    GET_FEATURE_ID(aa64_fp16, ARM_HWCAP_A64_FPHP | ARM_HWCAP_A64_ASIMDHP);
    GET_FEATURE_ID(aa64_atomics, ARM_HWCAP_A64_ATOMICS);
    GET_FEATURE_ID(aa64_rdm, ARM_HWCAP_A64_ASIMDRDM);
    GET_FEATURE_ID(aa64_dp, ARM_HWCAP_A64_ASIMDDP);
    GET_FEATURE_ID(aa64_fcma, ARM_HWCAP_A64_FCMA);
    GET_FEATURE_ID(aa64_sve, ARM_HWCAP_A64_SVE);
    GET_FEATURE_ID(aa64_pauth, ARM_HWCAP_A64_PACA | ARM_HWCAP_A64_PACG);
    GET_FEATURE_ID(aa64_fhm, ARM_HWCAP_A64_ASIMDFHM);
    GET_FEATURE_ID(aa64_jscvt, ARM_HWCAP_A64_JSCVT);
    GET_FEATURE_ID(aa64_sb, ARM_HWCAP_A64_SB);
    GET_FEATURE_ID(aa64_condm_4, ARM_HWCAP_A64_FLAGM);
    GET_FEATURE_ID(aa64_dcpop, ARM_HWCAP_A64_DCPOP);
    GET_FEATURE_ID(aa64_rcpc_8_3, ARM_HWCAP_A64_LRCPC);
    GET_FEATURE_ID(aa64_rcpc_8_4, ARM_HWCAP_A64_ILRCPC);

    return hwcaps;
}

static uint32_t get_elf_hwcap2(void)
{
    ARMCPU *cpu = ARM_CPU(thread_cpu);
    uint32_t hwcaps = 0;

    GET_FEATURE_ID(aa64_dcpodp, ARM_HWCAP2_A64_DCPODP);
    GET_FEATURE_ID(aa64_sve2, ARM_HWCAP2_A64_SVE2);
    GET_FEATURE_ID(aa64_sve2_aes, ARM_HWCAP2_A64_SVEAES);
    GET_FEATURE_ID(aa64_sve2_pmull128, ARM_HWCAP2_A64_SVEPMULL);
    GET_FEATURE_ID(aa64_sve2_bitperm, ARM_HWCAP2_A64_SVEBITPERM);
    GET_FEATURE_ID(aa64_sve2_sha3, ARM_HWCAP2_A64_SVESHA3);
    GET_FEATURE_ID(aa64_sve2_sm4, ARM_HWCAP2_A64_SVESM4);
    GET_FEATURE_ID(aa64_condm_5, ARM_HWCAP2_A64_FLAGM2);
    GET_FEATURE_ID(aa64_frint, ARM_HWCAP2_A64_FRINT);
    GET_FEATURE_ID(aa64_sve_i8mm, ARM_HWCAP2_A64_SVEI8MM);
    GET_FEATURE_ID(aa64_sve_f32mm, ARM_HWCAP2_A64_SVEF32MM);
    GET_FEATURE_ID(aa64_sve_f64mm, ARM_HWCAP2_A64_SVEF64MM);
    GET_FEATURE_ID(aa64_sve_bf16, ARM_HWCAP2_A64_SVEBF16);
    GET_FEATURE_ID(aa64_i8mm, ARM_HWCAP2_A64_I8MM);
    GET_FEATURE_ID(aa64_bf16, ARM_HWCAP2_A64_BF16);
    GET_FEATURE_ID(aa64_rndr, ARM_HWCAP2_A64_RNG);
    GET_FEATURE_ID(aa64_bti, ARM_HWCAP2_A64_BTI);
    GET_FEATURE_ID(aa64_mte, ARM_HWCAP2_A64_MTE);

    return hwcaps;
}

#undef GET_FEATURE_ID

#endif /* TARGET_ARCH_ELF_H */
