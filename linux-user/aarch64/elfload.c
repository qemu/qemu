/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu.h"
#include "loader.h"
#include "target/arm/cpu-features.h"
#include "target_elf.h"
#include "elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "any";
}

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
    ARM_HWCAP_A64_PACG          = 1ULL << 31,
    ARM_HWCAP_A64_GCS           = 1ULL << 32,
    ARM_HWCAP_A64_CMPBR         = 1ULL << 33,
    ARM_HWCAP_A64_FPRCVT        = 1ULL << 34,
    ARM_HWCAP_A64_F8MM8         = 1ULL << 35,
    ARM_HWCAP_A64_F8MM4         = 1ULL << 36,
    ARM_HWCAP_A64_SVE_F16MM     = 1ULL << 37,
    ARM_HWCAP_A64_SVE_ELTPERM   = 1ULL << 38,
    ARM_HWCAP_A64_SVE_AES2      = 1ULL << 39,
    ARM_HWCAP_A64_SVE_BFSCALE   = 1ULL << 40,
    ARM_HWCAP_A64_SVE2P2        = 1ULL << 41,
    ARM_HWCAP_A64_SME2P2        = 1ULL << 42,
    ARM_HWCAP_A64_SME_SBITPERM  = 1ULL << 43,
    ARM_HWCAP_A64_SME_AES       = 1ULL << 44,
    ARM_HWCAP_A64_SME_SFEXPA    = 1ULL << 45,
    ARM_HWCAP_A64_SME_STMOP     = 1ULL << 46,
    ARM_HWCAP_A64_SME_SMOP4     = 1ULL << 47,

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
    ARM_HWCAP2_A64_ECV          = 1 << 19,
    ARM_HWCAP2_A64_AFP          = 1 << 20,
    ARM_HWCAP2_A64_RPRES        = 1 << 21,
    ARM_HWCAP2_A64_MTE3         = 1 << 22,
    ARM_HWCAP2_A64_SME          = 1 << 23,
    ARM_HWCAP2_A64_SME_I16I64   = 1 << 24,
    ARM_HWCAP2_A64_SME_F64F64   = 1 << 25,
    ARM_HWCAP2_A64_SME_I8I32    = 1 << 26,
    ARM_HWCAP2_A64_SME_F16F32   = 1 << 27,
    ARM_HWCAP2_A64_SME_B16F32   = 1 << 28,
    ARM_HWCAP2_A64_SME_F32F32   = 1 << 29,
    ARM_HWCAP2_A64_SME_FA64     = 1 << 30,
    ARM_HWCAP2_A64_WFXT         = 1ULL << 31,
    ARM_HWCAP2_A64_EBF16        = 1ULL << 32,
    ARM_HWCAP2_A64_SVE_EBF16    = 1ULL << 33,
    ARM_HWCAP2_A64_CSSC         = 1ULL << 34,
    ARM_HWCAP2_A64_RPRFM        = 1ULL << 35,
    ARM_HWCAP2_A64_SVE2P1       = 1ULL << 36,
    ARM_HWCAP2_A64_SME2         = 1ULL << 37,
    ARM_HWCAP2_A64_SME2P1       = 1ULL << 38,
    ARM_HWCAP2_A64_SME_I16I32   = 1ULL << 39,
    ARM_HWCAP2_A64_SME_BI32I32  = 1ULL << 40,
    ARM_HWCAP2_A64_SME_B16B16   = 1ULL << 41,
    ARM_HWCAP2_A64_SME_F16F16   = 1ULL << 42,
    ARM_HWCAP2_A64_MOPS         = 1ULL << 43,
    ARM_HWCAP2_A64_HBC          = 1ULL << 44,
    ARM_HWCAP2_A64_SVE_B16B16   = 1ULL << 45,
    ARM_HWCAP2_A64_LRCPC3       = 1ULL << 46,
    ARM_HWCAP2_A64_LSE128       = 1ULL << 47,
    ARM_HWCAP2_A64_FPMR         = 1ULL << 48,
    ARM_HWCAP2_A64_LUT          = 1ULL << 49,
    ARM_HWCAP2_A64_FAMINMAX     = 1ULL << 50,
    ARM_HWCAP2_A64_F8CVT        = 1ULL << 51,
    ARM_HWCAP2_A64_F8FMA        = 1ULL << 52,
    ARM_HWCAP2_A64_F8DP4        = 1ULL << 53,
    ARM_HWCAP2_A64_F8DP2        = 1ULL << 54,
    ARM_HWCAP2_A64_F8E4M3       = 1ULL << 55,
    ARM_HWCAP2_A64_F8E5M2       = 1ULL << 56,
    ARM_HWCAP2_A64_SME_LUTV2    = 1ULL << 57,
    ARM_HWCAP2_A64_SME_F8F16    = 1ULL << 58,
    ARM_HWCAP2_A64_SME_F8F32    = 1ULL << 59,
    ARM_HWCAP2_A64_SME_SF8FMA   = 1ULL << 60,
    ARM_HWCAP2_A64_SME_SF8DP4   = 1ULL << 61,
    ARM_HWCAP2_A64_SME_SF8DP2   = 1ULL << 62,
    ARM_HWCAP2_A64_POE          = 1ULL << 63,
};

#define GET_FEATURE_ID(feat, hwcap) \
    do { if (cpu_isar_feature(feat, cpu)) { hwcaps |= hwcap; } } while (0)

abi_ulong get_elf_hwcap(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    abi_ulong hwcaps = 0;

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
    GET_FEATURE_ID(aa64_lse, ARM_HWCAP_A64_ATOMICS);
    GET_FEATURE_ID(aa64_lse2, ARM_HWCAP_A64_USCAT);
    GET_FEATURE_ID(aa64_rdm, ARM_HWCAP_A64_ASIMDRDM);
    GET_FEATURE_ID(aa64_dp, ARM_HWCAP_A64_ASIMDDP);
    GET_FEATURE_ID(aa64_fcma, ARM_HWCAP_A64_FCMA);
    GET_FEATURE_ID(aa64_sve, ARM_HWCAP_A64_SVE);
    GET_FEATURE_ID(aa64_pauth, ARM_HWCAP_A64_PACA | ARM_HWCAP_A64_PACG);
    GET_FEATURE_ID(aa64_fhm, ARM_HWCAP_A64_ASIMDFHM);
    GET_FEATURE_ID(aa64_dit, ARM_HWCAP_A64_DIT);
    GET_FEATURE_ID(aa64_jscvt, ARM_HWCAP_A64_JSCVT);
    GET_FEATURE_ID(aa64_sb, ARM_HWCAP_A64_SB);
    GET_FEATURE_ID(aa64_condm_4, ARM_HWCAP_A64_FLAGM);
    GET_FEATURE_ID(aa64_dcpop, ARM_HWCAP_A64_DCPOP);
    GET_FEATURE_ID(aa64_rcpc_8_3, ARM_HWCAP_A64_LRCPC);
    GET_FEATURE_ID(aa64_rcpc_8_4, ARM_HWCAP_A64_ILRCPC);
    GET_FEATURE_ID(aa64_gcs, ARM_HWCAP_A64_GCS);

    return hwcaps;
}

abi_ulong get_elf_hwcap2(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    abi_ulong hwcaps = 0;

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
    GET_FEATURE_ID(aa64_mte3, ARM_HWCAP2_A64_MTE3);
    GET_FEATURE_ID(aa64_sme, (ARM_HWCAP2_A64_SME |
                              ARM_HWCAP2_A64_SME_F32F32 |
                              ARM_HWCAP2_A64_SME_B16F32 |
                              ARM_HWCAP2_A64_SME_F16F32 |
                              ARM_HWCAP2_A64_SME_I8I32));
    GET_FEATURE_ID(aa64_sme_f64f64, ARM_HWCAP2_A64_SME_F64F64);
    GET_FEATURE_ID(aa64_sme_i16i64, ARM_HWCAP2_A64_SME_I16I64);
    GET_FEATURE_ID(aa64_sme_fa64, ARM_HWCAP2_A64_SME_FA64);
    GET_FEATURE_ID(aa64_hbc, ARM_HWCAP2_A64_HBC);
    GET_FEATURE_ID(aa64_mops, ARM_HWCAP2_A64_MOPS);
    GET_FEATURE_ID(aa64_sve2p1, ARM_HWCAP2_A64_SVE2P1);
    GET_FEATURE_ID(aa64_sme2, (ARM_HWCAP2_A64_SME2 |
                               ARM_HWCAP2_A64_SME_I16I32 |
                               ARM_HWCAP2_A64_SME_BI32I32));
    GET_FEATURE_ID(aa64_sme2p1, ARM_HWCAP2_A64_SME2P1);
    GET_FEATURE_ID(aa64_sme_b16b16, ARM_HWCAP2_A64_SME_B16B16);
    GET_FEATURE_ID(aa64_sme_f16f16, ARM_HWCAP2_A64_SME_F16F16);
    GET_FEATURE_ID(aa64_sve_b16b16, ARM_HWCAP2_A64_SVE_B16B16);
    GET_FEATURE_ID(aa64_cssc, ARM_HWCAP2_A64_CSSC);
    GET_FEATURE_ID(aa64_lse128, ARM_HWCAP2_A64_LSE128);

    return hwcaps;
}

const char *elf_hwcap_str(uint32_t bit)
{
    static const char * const hwcap_str[] = {
    [__builtin_ctz(ARM_HWCAP_A64_FP      )] = "fp",
    [__builtin_ctz(ARM_HWCAP_A64_ASIMD   )] = "asimd",
    [__builtin_ctz(ARM_HWCAP_A64_EVTSTRM )] = "evtstrm",
    [__builtin_ctz(ARM_HWCAP_A64_AES     )] = "aes",
    [__builtin_ctz(ARM_HWCAP_A64_PMULL   )] = "pmull",
    [__builtin_ctz(ARM_HWCAP_A64_SHA1    )] = "sha1",
    [__builtin_ctz(ARM_HWCAP_A64_SHA2    )] = "sha2",
    [__builtin_ctz(ARM_HWCAP_A64_CRC32   )] = "crc32",
    [__builtin_ctz(ARM_HWCAP_A64_ATOMICS )] = "atomics",
    [__builtin_ctz(ARM_HWCAP_A64_FPHP    )] = "fphp",
    [__builtin_ctz(ARM_HWCAP_A64_ASIMDHP )] = "asimdhp",
    [__builtin_ctz(ARM_HWCAP_A64_CPUID   )] = "cpuid",
    [__builtin_ctz(ARM_HWCAP_A64_ASIMDRDM)] = "asimdrdm",
    [__builtin_ctz(ARM_HWCAP_A64_JSCVT   )] = "jscvt",
    [__builtin_ctz(ARM_HWCAP_A64_FCMA    )] = "fcma",
    [__builtin_ctz(ARM_HWCAP_A64_LRCPC   )] = "lrcpc",
    [__builtin_ctz(ARM_HWCAP_A64_DCPOP   )] = "dcpop",
    [__builtin_ctz(ARM_HWCAP_A64_SHA3    )] = "sha3",
    [__builtin_ctz(ARM_HWCAP_A64_SM3     )] = "sm3",
    [__builtin_ctz(ARM_HWCAP_A64_SM4     )] = "sm4",
    [__builtin_ctz(ARM_HWCAP_A64_ASIMDDP )] = "asimddp",
    [__builtin_ctz(ARM_HWCAP_A64_SHA512  )] = "sha512",
    [__builtin_ctz(ARM_HWCAP_A64_SVE     )] = "sve",
    [__builtin_ctz(ARM_HWCAP_A64_ASIMDFHM)] = "asimdfhm",
    [__builtin_ctz(ARM_HWCAP_A64_DIT     )] = "dit",
    [__builtin_ctz(ARM_HWCAP_A64_USCAT   )] = "uscat",
    [__builtin_ctz(ARM_HWCAP_A64_ILRCPC  )] = "ilrcpc",
    [__builtin_ctz(ARM_HWCAP_A64_FLAGM   )] = "flagm",
    [__builtin_ctz(ARM_HWCAP_A64_SSBS    )] = "ssbs",
    [__builtin_ctz(ARM_HWCAP_A64_SB      )] = "sb",
    [__builtin_ctz(ARM_HWCAP_A64_PACA    )] = "paca",
    [__builtin_ctz(ARM_HWCAP_A64_PACG    )] = "pacg",
    [__builtin_ctzll(ARM_HWCAP_A64_GCS   )] = "gcs",
    [__builtin_ctzll(ARM_HWCAP_A64_CMPBR )] = "cmpbr",
    [__builtin_ctzll(ARM_HWCAP_A64_FPRCVT)] = "fprcvt",
    [__builtin_ctzll(ARM_HWCAP_A64_F8MM8 )] = "f8mm8",
    [__builtin_ctzll(ARM_HWCAP_A64_F8MM4 )] = "f8mm4",
    [__builtin_ctzll(ARM_HWCAP_A64_SVE_F16MM)] = "svef16mm",
    [__builtin_ctzll(ARM_HWCAP_A64_SVE_ELTPERM)] = "sveeltperm",
    [__builtin_ctzll(ARM_HWCAP_A64_SVE_AES2)] = "sveaes2",
    [__builtin_ctzll(ARM_HWCAP_A64_SVE_BFSCALE)] = "svebfscale",
    [__builtin_ctzll(ARM_HWCAP_A64_SVE2P2)] = "sve2p2",
    [__builtin_ctzll(ARM_HWCAP_A64_SME2P2)] = "sme2p2",
    [__builtin_ctzll(ARM_HWCAP_A64_SME_SBITPERM)] = "smesbitperm",
    [__builtin_ctzll(ARM_HWCAP_A64_SME_AES)] = "smeaes",
    [__builtin_ctzll(ARM_HWCAP_A64_SME_SFEXPA)] = "smesfexpa",
    [__builtin_ctzll(ARM_HWCAP_A64_SME_STMOP)] = "smestmop",
    [__builtin_ctzll(ARM_HWCAP_A64_SME_SMOP4)] = "smesmop4",
    };

    return bit < ARRAY_SIZE(hwcap_str) ? hwcap_str[bit] : NULL;
}

const char *elf_hwcap2_str(uint32_t bit)
{
    static const char * const hwcap_str[] = {
    [__builtin_ctz(ARM_HWCAP2_A64_DCPODP       )] = "dcpodp",
    [__builtin_ctz(ARM_HWCAP2_A64_SVE2         )] = "sve2",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEAES       )] = "sveaes",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEPMULL     )] = "svepmull",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEBITPERM   )] = "svebitperm",
    [__builtin_ctz(ARM_HWCAP2_A64_SVESHA3      )] = "svesha3",
    [__builtin_ctz(ARM_HWCAP2_A64_SVESM4       )] = "svesm4",
    [__builtin_ctz(ARM_HWCAP2_A64_FLAGM2       )] = "flagm2",
    [__builtin_ctz(ARM_HWCAP2_A64_FRINT        )] = "frint",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEI8MM      )] = "svei8mm",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEF32MM     )] = "svef32mm",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEF64MM     )] = "svef64mm",
    [__builtin_ctz(ARM_HWCAP2_A64_SVEBF16      )] = "svebf16",
    [__builtin_ctz(ARM_HWCAP2_A64_I8MM         )] = "i8mm",
    [__builtin_ctz(ARM_HWCAP2_A64_BF16         )] = "bf16",
    [__builtin_ctz(ARM_HWCAP2_A64_DGH          )] = "dgh",
    [__builtin_ctz(ARM_HWCAP2_A64_RNG          )] = "rng",
    [__builtin_ctz(ARM_HWCAP2_A64_BTI          )] = "bti",
    [__builtin_ctz(ARM_HWCAP2_A64_MTE          )] = "mte",
    [__builtin_ctz(ARM_HWCAP2_A64_ECV          )] = "ecv",
    [__builtin_ctz(ARM_HWCAP2_A64_AFP          )] = "afp",
    [__builtin_ctz(ARM_HWCAP2_A64_RPRES        )] = "rpres",
    [__builtin_ctz(ARM_HWCAP2_A64_MTE3         )] = "mte3",
    [__builtin_ctz(ARM_HWCAP2_A64_SME          )] = "sme",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_I16I64   )] = "smei16i64",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_F64F64   )] = "smef64f64",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_I8I32    )] = "smei8i32",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_F16F32   )] = "smef16f32",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_B16F32   )] = "smeb16f32",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_F32F32   )] = "smef32f32",
    [__builtin_ctz(ARM_HWCAP2_A64_SME_FA64     )] = "smefa64",
    [__builtin_ctz(ARM_HWCAP2_A64_WFXT         )] = "wfxt",
    [__builtin_ctzll(ARM_HWCAP2_A64_EBF16      )] = "ebf16",
    [__builtin_ctzll(ARM_HWCAP2_A64_SVE_EBF16  )] = "sveebf16",
    [__builtin_ctzll(ARM_HWCAP2_A64_CSSC       )] = "cssc",
    [__builtin_ctzll(ARM_HWCAP2_A64_RPRFM      )] = "rprfm",
    [__builtin_ctzll(ARM_HWCAP2_A64_SVE2P1     )] = "sve2p1",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME2       )] = "sme2",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME2P1     )] = "sme2p1",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_I16I32 )] = "smei16i32",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_BI32I32)] = "smebi32i32",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_B16B16 )] = "smeb16b16",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_F16F16 )] = "smef16f16",
    [__builtin_ctzll(ARM_HWCAP2_A64_MOPS       )] = "mops",
    [__builtin_ctzll(ARM_HWCAP2_A64_HBC        )] = "hbc",
    [__builtin_ctzll(ARM_HWCAP2_A64_SVE_B16B16 )] = "sveb16b16",
    [__builtin_ctzll(ARM_HWCAP2_A64_LRCPC3     )] = "lrcpc3",
    [__builtin_ctzll(ARM_HWCAP2_A64_LSE128     )] = "lse128",
    [__builtin_ctzll(ARM_HWCAP2_A64_FPMR       )] = "fpmr",
    [__builtin_ctzll(ARM_HWCAP2_A64_LUT        )] = "lut",
    [__builtin_ctzll(ARM_HWCAP2_A64_FAMINMAX   )] = "faminmax",
    [__builtin_ctzll(ARM_HWCAP2_A64_F8CVT      )] = "f8cvt",
    [__builtin_ctzll(ARM_HWCAP2_A64_F8FMA      )] = "f8fma",
    [__builtin_ctzll(ARM_HWCAP2_A64_F8DP4      )] = "f8dp4",
    [__builtin_ctzll(ARM_HWCAP2_A64_F8DP2      )] = "f8dp2",
    [__builtin_ctzll(ARM_HWCAP2_A64_F8E4M3     )] = "f8e4m3",
    [__builtin_ctzll(ARM_HWCAP2_A64_F8E5M2     )] = "f8e5m2",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_LUTV2  )] = "smelutv2",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_F8F16  )] = "smef8f16",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_F8F32  )] = "smef8f32",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_SF8DP4 )] = "smesf8dp4",
    [__builtin_ctzll(ARM_HWCAP2_A64_SME_SF8DP2 )] = "smesf8dp2",
    [__builtin_ctzll(ARM_HWCAP2_A64_POE        )] = "poe",
    };

    return bit < ARRAY_SIZE(hwcap_str) ? hwcap_str[bit] : NULL;
}

const char *get_elf_platform(CPUState *cs)
{
    return TARGET_BIG_ENDIAN ? "aarch64_be" : "aarch64";
}

bool arch_parse_elf_property(uint32_t pr_type, uint32_t pr_datasz,
                             const uint32_t *data,
                             struct image_info *info,
                             Error **errp)
{
    if (pr_type == GNU_PROPERTY_AARCH64_FEATURE_1_AND) {
        if (pr_datasz != sizeof(uint32_t)) {
            error_setg(errp, "Ill-formed GNU_PROPERTY_AARCH64_FEATURE_1_AND");
            return false;
        }
        /* We will extract GNU_PROPERTY_AARCH64_FEATURE_1_BTI later. */
        info->note_flags = *data;
    }
    return true;
}

void elf_core_copy_regs(target_elf_gregset_t *r, const CPUARMState *env)
{
    for (int i = 0; i < 31; i++) {
        r->pt.regs[i] = tswap64(env->xregs[i]);
    }
    r->pt.sp = tswap64(env->xregs[31]);
    r->pt.pc = tswap64(env->pc);
    r->pt.pstate = tswap64(pstate_read((CPUARMState *)env));
}
