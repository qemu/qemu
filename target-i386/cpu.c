/*
 *  i386 CPUID helper functions
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "sysemu/kvm.h"
#include "sysemu/cpus.h"
#include "kvm_i386.h"
#include "topology.h"

#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qerror.h"

#include "qapi-types.h"
#include "qapi-visit.h"
#include "qapi/visitor.h"
#include "sysemu/arch_init.h"

#include "hw/hw.h"
#if defined(CONFIG_KVM)
#include <linux/kvm_para.h>
#endif

#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/cpu/icc_bus.h"
#ifndef CONFIG_USER_ONLY
#include "hw/xen/xen.h"
#include "hw/i386/apic_internal.h"
#endif


/* Cache topology CPUID constants: */

/* CPUID Leaf 2 Descriptors */

#define CPUID_2_L1D_32KB_8WAY_64B 0x2c
#define CPUID_2_L1I_32KB_8WAY_64B 0x30
#define CPUID_2_L2_2MB_8WAY_64B   0x7d


/* CPUID Leaf 4 constants: */

/* EAX: */
#define CPUID_4_TYPE_DCACHE  1
#define CPUID_4_TYPE_ICACHE  2
#define CPUID_4_TYPE_UNIFIED 3

#define CPUID_4_LEVEL(l)          ((l) << 5)

#define CPUID_4_SELF_INIT_LEVEL (1 << 8)
#define CPUID_4_FULLY_ASSOC     (1 << 9)

/* EDX: */
#define CPUID_4_NO_INVD_SHARING (1 << 0)
#define CPUID_4_INCLUSIVE       (1 << 1)
#define CPUID_4_COMPLEX_IDX     (1 << 2)

#define ASSOC_FULL 0xFF

/* AMD associativity encoding used on CPUID Leaf 0x80000006: */
#define AMD_ENC_ASSOC(a) (a <=   1 ? a   : \
                          a ==   2 ? 0x2 : \
                          a ==   4 ? 0x4 : \
                          a ==   8 ? 0x6 : \
                          a ==  16 ? 0x8 : \
                          a ==  32 ? 0xA : \
                          a ==  48 ? 0xB : \
                          a ==  64 ? 0xC : \
                          a ==  96 ? 0xD : \
                          a == 128 ? 0xE : \
                          a == ASSOC_FULL ? 0xF : \
                          0 /* invalid value */)


/* Definitions of the hardcoded cache entries we expose: */

/* L1 data cache: */
#define L1D_LINE_SIZE         64
#define L1D_ASSOCIATIVITY      8
#define L1D_SETS              64
#define L1D_PARTITIONS         1
/* Size = LINE_SIZE*ASSOCIATIVITY*SETS*PARTITIONS = 32KiB */
#define L1D_DESCRIPTOR CPUID_2_L1D_32KB_8WAY_64B
/*FIXME: CPUID leaf 0x80000005 is inconsistent with leaves 2 & 4 */
#define L1D_LINES_PER_TAG      1
#define L1D_SIZE_KB_AMD       64
#define L1D_ASSOCIATIVITY_AMD  2

/* L1 instruction cache: */
#define L1I_LINE_SIZE         64
#define L1I_ASSOCIATIVITY      8
#define L1I_SETS              64
#define L1I_PARTITIONS         1
/* Size = LINE_SIZE*ASSOCIATIVITY*SETS*PARTITIONS = 32KiB */
#define L1I_DESCRIPTOR CPUID_2_L1I_32KB_8WAY_64B
/*FIXME: CPUID leaf 0x80000005 is inconsistent with leaves 2 & 4 */
#define L1I_LINES_PER_TAG      1
#define L1I_SIZE_KB_AMD       64
#define L1I_ASSOCIATIVITY_AMD  2

/* Level 2 unified cache: */
#define L2_LINE_SIZE          64
#define L2_ASSOCIATIVITY      16
#define L2_SETS             4096
#define L2_PARTITIONS          1
/* Size = LINE_SIZE*ASSOCIATIVITY*SETS*PARTITIONS = 4MiB */
/*FIXME: CPUID leaf 2 descriptor is inconsistent with CPUID leaf 4 */
#define L2_DESCRIPTOR CPUID_2_L2_2MB_8WAY_64B
/*FIXME: CPUID leaf 0x80000006 is inconsistent with leaves 2 & 4 */
#define L2_LINES_PER_TAG       1
#define L2_SIZE_KB_AMD       512

/* No L3 cache: */
#define L3_SIZE_KB             0 /* disabled */
#define L3_ASSOCIATIVITY       0 /* disabled */
#define L3_LINES_PER_TAG       0 /* disabled */
#define L3_LINE_SIZE           0 /* disabled */

/* TLB definitions: */

#define L1_DTLB_2M_ASSOC       1
#define L1_DTLB_2M_ENTRIES   255
#define L1_DTLB_4K_ASSOC       1
#define L1_DTLB_4K_ENTRIES   255

#define L1_ITLB_2M_ASSOC       1
#define L1_ITLB_2M_ENTRIES   255
#define L1_ITLB_4K_ASSOC       1
#define L1_ITLB_4K_ENTRIES   255

#define L2_DTLB_2M_ASSOC       0 /* disabled */
#define L2_DTLB_2M_ENTRIES     0 /* disabled */
#define L2_DTLB_4K_ASSOC       4
#define L2_DTLB_4K_ENTRIES   512

#define L2_ITLB_2M_ASSOC       0 /* disabled */
#define L2_ITLB_2M_ENTRIES     0 /* disabled */
#define L2_ITLB_4K_ASSOC       4
#define L2_ITLB_4K_ENTRIES   512



static void x86_cpu_vendor_words2str(char *dst, uint32_t vendor1,
                                     uint32_t vendor2, uint32_t vendor3)
{
    int i;
    for (i = 0; i < 4; i++) {
        dst[i] = vendor1 >> (8 * i);
        dst[i + 4] = vendor2 >> (8 * i);
        dst[i + 8] = vendor3 >> (8 * i);
    }
    dst[CPUID_VENDOR_SZ] = '\0';
}

/* feature flags taken from "Intel Processor Identification and the CPUID
 * Instruction" and AMD's "CPUID Specification".  In cases of disagreement
 * between feature naming conventions, aliases may be added.
 */
static const char *feature_name[] = {
    "fpu", "vme", "de", "pse",
    "tsc", "msr", "pae", "mce",
    "cx8", "apic", NULL, "sep",
    "mtrr", "pge", "mca", "cmov",
    "pat", "pse36", "pn" /* Intel psn */, "clflush" /* Intel clfsh */,
    NULL, "ds" /* Intel dts */, "acpi", "mmx",
    "fxsr", "sse", "sse2", "ss",
    "ht" /* Intel htt */, "tm", "ia64", "pbe",
};
static const char *ext_feature_name[] = {
    "pni|sse3" /* Intel,AMD sse3 */, "pclmulqdq|pclmuldq", "dtes64", "monitor",
    "ds_cpl", "vmx", "smx", "est",
    "tm2", "ssse3", "cid", NULL,
    "fma", "cx16", "xtpr", "pdcm",
    NULL, "pcid", "dca", "sse4.1|sse4_1",
    "sse4.2|sse4_2", "x2apic", "movbe", "popcnt",
    "tsc-deadline", "aes", "xsave", "osxsave",
    "avx", "f16c", "rdrand", "hypervisor",
};
/* Feature names that are already defined on feature_name[] but are set on
 * CPUID[8000_0001].EDX on AMD CPUs don't have their names on
 * ext2_feature_name[]. They are copied automatically to cpuid_ext2_features
 * if and only if CPU vendor is AMD.
 */
static const char *ext2_feature_name[] = {
    NULL /* fpu */, NULL /* vme */, NULL /* de */, NULL /* pse */,
    NULL /* tsc */, NULL /* msr */, NULL /* pae */, NULL /* mce */,
    NULL /* cx8 */ /* AMD CMPXCHG8B */, NULL /* apic */, NULL, "syscall",
    NULL /* mtrr */, NULL /* pge */, NULL /* mca */, NULL /* cmov */,
    NULL /* pat */, NULL /* pse36 */, NULL, NULL /* Linux mp */,
    "nx|xd", NULL, "mmxext", NULL /* mmx */,
    NULL /* fxsr */, "fxsr_opt|ffxsr", "pdpe1gb" /* AMD Page1GB */, "rdtscp",
    NULL, "lm|i64", "3dnowext", "3dnow",
};
static const char *ext3_feature_name[] = {
    "lahf_lm" /* AMD LahfSahf */, "cmp_legacy", "svm", "extapic" /* AMD ExtApicSpace */,
    "cr8legacy" /* AMD AltMovCr8 */, "abm", "sse4a", "misalignsse",
    "3dnowprefetch", "osvw", "ibs", "xop",
    "skinit", "wdt", NULL, "lwp",
    "fma4", "tce", NULL, "nodeid_msr",
    NULL, "tbm", "topoext", "perfctr_core",
    "perfctr_nb", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *ext4_feature_name[] = {
    NULL, NULL, "xstore", "xstore-en",
    NULL, NULL, "xcrypt", "xcrypt-en",
    "ace2", "ace2-en", "phe", "phe-en",
    "pmm", "pmm-en", NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *kvm_feature_name[] = {
    "kvmclock", "kvm_nopiodelay", "kvm_mmu", "kvmclock",
    "kvm_asyncpf", "kvm_steal_time", "kvm_pv_eoi", "kvm_pv_unhalt",
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *svm_feature_name[] = {
    "npt", "lbrv", "svm_lock", "nrip_save",
    "tsc_scale", "vmcb_clean",  "flushbyasid", "decodeassists",
    NULL, NULL, "pause_filter", NULL,
    "pfthreshold", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *cpuid_7_0_ebx_feature_name[] = {
    "fsgsbase", NULL, NULL, "bmi1", "hle", "avx2", NULL, "smep",
    "bmi2", "erms", "invpcid", "rtm", NULL, NULL, NULL, NULL,
    NULL, NULL, "rdseed", "adx", "smap", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

static const char *cpuid_apm_edx_feature_name[] = {
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    "invtsc", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

#define I486_FEATURES (CPUID_FP87 | CPUID_VME | CPUID_PSE)
#define PENTIUM_FEATURES (I486_FEATURES | CPUID_DE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_MMX | CPUID_APIC)
#define PENTIUM2_FEATURES (PENTIUM_FEATURES | CPUID_PAE | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_FXSR)
#define PENTIUM3_FEATURES (PENTIUM2_FEATURES | CPUID_SSE)
#define PPRO_FEATURES (CPUID_FP87 | CPUID_DE | CPUID_PSE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_PGE | CPUID_CMOV | \
          CPUID_PAT | CPUID_FXSR | CPUID_MMX | CPUID_SSE | CPUID_SSE2 | \
          CPUID_PAE | CPUID_SEP | CPUID_APIC)

#define TCG_FEATURES (CPUID_FP87 | CPUID_PSE | CPUID_TSC | CPUID_MSR | \
          CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_CLFLUSH | CPUID_ACPI | CPUID_MMX | \
          CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS)
          /* partly implemented:
          CPUID_MTRR, CPUID_MCA, CPUID_CLFLUSH (needed for Win64) */
          /* missing:
          CPUID_VME, CPUID_DTS, CPUID_SS, CPUID_HT, CPUID_TM, CPUID_PBE */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | \
          CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | \
          CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_POPCNT | \
          CPUID_EXT_MOVBE | CPUID_EXT_AES | CPUID_EXT_HYPERVISOR)
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_SMX,
          CPUID_EXT_EST, CPUID_EXT_TM2, CPUID_EXT_CID, CPUID_EXT_FMA,
          CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_PCID, CPUID_EXT_DCA,
          CPUID_EXT_X2APIC, CPUID_EXT_TSC_DEADLINE_TIMER, CPUID_EXT_XSAVE,
          CPUID_EXT_OSXSAVE, CPUID_EXT_AVX, CPUID_EXT_F16C,
          CPUID_EXT_RDRAND */

#ifdef TARGET_X86_64
#define TCG_EXT2_X86_64_FEATURES (CPUID_EXT2_SYSCALL | CPUID_EXT2_LM)
#else
#define TCG_EXT2_X86_64_FEATURES 0
#endif

#define TCG_EXT2_FEATURES ((TCG_FEATURES & CPUID_EXT2_AMD_ALIASES) | \
          CPUID_EXT2_NX | CPUID_EXT2_MMXEXT | CPUID_EXT2_RDTSCP | \
          CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_PDPE1GB | \
          TCG_EXT2_X86_64_FEATURES)
#define TCG_EXT3_FEATURES (CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM | \
          CPUID_EXT3_CR8LEG | CPUID_EXT3_ABM | CPUID_EXT3_SSE4A)
#define TCG_EXT4_FEATURES 0
#define TCG_SVM_FEATURES 0
#define TCG_KVM_FEATURES 0
#define TCG_7_0_EBX_FEATURES (CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_SMAP | \
          CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ADX)
          /* missing:
          CPUID_7_0_EBX_FSGSBASE, CPUID_7_0_EBX_HLE, CPUID_7_0_EBX_AVX2,
          CPUID_7_0_EBX_ERMS, CPUID_7_0_EBX_INVPCID, CPUID_7_0_EBX_RTM,
          CPUID_7_0_EBX_RDSEED */
#define TCG_APM_FEATURES 0


typedef struct FeatureWordInfo {
    const char **feat_names;
    uint32_t cpuid_eax;   /* Input EAX for CPUID */
    bool cpuid_needs_ecx; /* CPUID instruction uses ECX as input */
    uint32_t cpuid_ecx;   /* Input ECX value for CPUID */
    int cpuid_reg;        /* output register (R_* constant) */
    uint32_t tcg_features; /* Feature flags supported by TCG */
    uint32_t unmigratable_flags; /* Feature flags known to be unmigratable */
} FeatureWordInfo;

static FeatureWordInfo feature_word_info[FEATURE_WORDS] = {
    [FEAT_1_EDX] = {
        .feat_names = feature_name,
        .cpuid_eax = 1, .cpuid_reg = R_EDX,
        .tcg_features = TCG_FEATURES,
    },
    [FEAT_1_ECX] = {
        .feat_names = ext_feature_name,
        .cpuid_eax = 1, .cpuid_reg = R_ECX,
        .tcg_features = TCG_EXT_FEATURES,
    },
    [FEAT_8000_0001_EDX] = {
        .feat_names = ext2_feature_name,
        .cpuid_eax = 0x80000001, .cpuid_reg = R_EDX,
        .tcg_features = TCG_EXT2_FEATURES,
    },
    [FEAT_8000_0001_ECX] = {
        .feat_names = ext3_feature_name,
        .cpuid_eax = 0x80000001, .cpuid_reg = R_ECX,
        .tcg_features = TCG_EXT3_FEATURES,
    },
    [FEAT_C000_0001_EDX] = {
        .feat_names = ext4_feature_name,
        .cpuid_eax = 0xC0000001, .cpuid_reg = R_EDX,
        .tcg_features = TCG_EXT4_FEATURES,
    },
    [FEAT_KVM] = {
        .feat_names = kvm_feature_name,
        .cpuid_eax = KVM_CPUID_FEATURES, .cpuid_reg = R_EAX,
        .tcg_features = TCG_KVM_FEATURES,
    },
    [FEAT_SVM] = {
        .feat_names = svm_feature_name,
        .cpuid_eax = 0x8000000A, .cpuid_reg = R_EDX,
        .tcg_features = TCG_SVM_FEATURES,
    },
    [FEAT_7_0_EBX] = {
        .feat_names = cpuid_7_0_ebx_feature_name,
        .cpuid_eax = 7,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_EBX,
        .tcg_features = TCG_7_0_EBX_FEATURES,
    },
    [FEAT_8000_0007_EDX] = {
        .feat_names = cpuid_apm_edx_feature_name,
        .cpuid_eax = 0x80000007,
        .cpuid_reg = R_EDX,
        .tcg_features = TCG_APM_FEATURES,
        .unmigratable_flags = CPUID_APM_INVTSC,
    },
};

typedef struct X86RegisterInfo32 {
    /* Name of register */
    const char *name;
    /* QAPI enum value register */
    X86CPURegister32 qapi_enum;
} X86RegisterInfo32;

#define REGISTER(reg) \
    [R_##reg] = { .name = #reg, .qapi_enum = X86_CPU_REGISTER32_##reg }
static const X86RegisterInfo32 x86_reg_info_32[CPU_NB_REGS32] = {
    REGISTER(EAX),
    REGISTER(ECX),
    REGISTER(EDX),
    REGISTER(EBX),
    REGISTER(ESP),
    REGISTER(EBP),
    REGISTER(ESI),
    REGISTER(EDI),
};
#undef REGISTER

typedef struct ExtSaveArea {
    uint32_t feature, bits;
    uint32_t offset, size;
} ExtSaveArea;

static const ExtSaveArea ext_save_areas[] = {
    [2] = { .feature = FEAT_1_ECX, .bits = CPUID_EXT_AVX,
            .offset = 0x240, .size = 0x100 },
    [3] = { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .offset = 0x3c0, .size = 0x40  },
    [4] = { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .offset = 0x400, .size = 0x40  },
};

const char *get_register_name_32(unsigned int reg)
{
    if (reg >= CPU_NB_REGS32) {
        return NULL;
    }
    return x86_reg_info_32[reg].name;
}

/* collects per-function cpuid data
 */
typedef struct model_features_t {
    uint32_t *guest_feat;
    uint32_t *host_feat;
    FeatureWord feat_word;
} model_features_t;

/* KVM-specific features that are automatically added to all CPU models
 * when KVM is enabled.
 */
static uint32_t kvm_default_features[FEATURE_WORDS] = {
    [FEAT_KVM] = (1 << KVM_FEATURE_CLOCKSOURCE) |
        (1 << KVM_FEATURE_NOP_IO_DELAY) |
        (1 << KVM_FEATURE_CLOCKSOURCE2) |
        (1 << KVM_FEATURE_ASYNC_PF) |
        (1 << KVM_FEATURE_STEAL_TIME) |
        (1 << KVM_FEATURE_PV_EOI) |
        (1 << KVM_FEATURE_CLOCKSOURCE_STABLE_BIT),
    [FEAT_1_ECX] = CPUID_EXT_X2APIC,
};

/* Features that are not added by default to any CPU model when KVM is enabled.
 */
static uint32_t kvm_default_unset_features[FEATURE_WORDS] = {
    [FEAT_1_ECX] = CPUID_EXT_MONITOR,
};

void x86_cpu_compat_disable_kvm_features(FeatureWord w, uint32_t features)
{
    kvm_default_features[w] &= ~features;
}

/*
 * Returns the set of feature flags that are supported and migratable by
 * QEMU, for a given FeatureWord.
 */
static uint32_t x86_cpu_get_migratable_flags(FeatureWord w)
{
    FeatureWordInfo *wi = &feature_word_info[w];
    uint32_t r = 0;
    int i;

    for (i = 0; i < 32; i++) {
        uint32_t f = 1U << i;
        /* If the feature name is unknown, it is not supported by QEMU yet */
        if (!wi->feat_names[i]) {
            continue;
        }
        /* Skip features known to QEMU, but explicitly marked as unmigratable */
        if (wi->unmigratable_flags & f) {
            continue;
        }
        r |= f;
    }
    return r;
}

void host_cpuid(uint32_t function, uint32_t count,
                uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    uint32_t vec[4];

#ifdef __x86_64__
    asm volatile("cpuid"
                 : "=a"(vec[0]), "=b"(vec[1]),
                   "=c"(vec[2]), "=d"(vec[3])
                 : "0"(function), "c"(count) : "cc");
#elif defined(__i386__)
    asm volatile("pusha \n\t"
                 "cpuid \n\t"
                 "mov %%eax, 0(%2) \n\t"
                 "mov %%ebx, 4(%2) \n\t"
                 "mov %%ecx, 8(%2) \n\t"
                 "mov %%edx, 12(%2) \n\t"
                 "popa"
                 : : "a"(function), "c"(count), "S"(vec)
                 : "memory", "cc");
#else
    abort();
#endif

    if (eax)
        *eax = vec[0];
    if (ebx)
        *ebx = vec[1];
    if (ecx)
        *ecx = vec[2];
    if (edx)
        *edx = vec[3];
}

#define iswhite(c) ((c) && ((c) <= ' ' || '~' < (c)))

/* general substring compare of *[s1..e1) and *[s2..e2).  sx is start of
 * a substring.  ex if !NULL points to the first char after a substring,
 * otherwise the string is assumed to sized by a terminating nul.
 * Return lexical ordering of *s1:*s2.
 */
static int sstrcmp(const char *s1, const char *e1, const char *s2,
    const char *e2)
{
    for (;;) {
        if (!*s1 || !*s2 || *s1 != *s2)
            return (*s1 - *s2);
        ++s1, ++s2;
        if (s1 == e1 && s2 == e2)
            return (0);
        else if (s1 == e1)
            return (*s2);
        else if (s2 == e2)
            return (*s1);
    }
}

/* compare *[s..e) to *altstr.  *altstr may be a simple string or multiple
 * '|' delimited (possibly empty) strings in which case search for a match
 * within the alternatives proceeds left to right.  Return 0 for success,
 * non-zero otherwise.
 */
static int altcmp(const char *s, const char *e, const char *altstr)
{
    const char *p, *q;

    for (q = p = altstr; ; ) {
        while (*p && *p != '|')
            ++p;
        if ((q == p && !*s) || (q != p && !sstrcmp(s, e, q, p)))
            return (0);
        if (!*p)
            return (1);
        else
            q = ++p;
    }
}

/* search featureset for flag *[s..e), if found set corresponding bit in
 * *pval and return true, otherwise return false
 */
static bool lookup_feature(uint32_t *pval, const char *s, const char *e,
                           const char **featureset)
{
    uint32_t mask;
    const char **ppc;
    bool found = false;

    for (mask = 1, ppc = featureset; mask; mask <<= 1, ++ppc) {
        if (*ppc && !altcmp(s, e, *ppc)) {
            *pval |= mask;
            found = true;
        }
    }
    return found;
}

static void add_flagname_to_bitmaps(const char *flagname,
                                    FeatureWordArray words)
{
    FeatureWord w;
    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *wi = &feature_word_info[w];
        if (wi->feat_names &&
            lookup_feature(&words[w], flagname, NULL, wi->feat_names)) {
            break;
        }
    }
    if (w == FEATURE_WORDS) {
        fprintf(stderr, "CPU feature %s not found\n", flagname);
    }
}

/* CPU class name definitions: */

#define X86_CPU_TYPE_SUFFIX "-" TYPE_X86_CPU
#define X86_CPU_TYPE_NAME(name) (name X86_CPU_TYPE_SUFFIX)

/* Return type name for a given CPU model name
 * Caller is responsible for freeing the returned string.
 */
static char *x86_cpu_type_name(const char *model_name)
{
    return g_strdup_printf(X86_CPU_TYPE_NAME("%s"), model_name);
}

static ObjectClass *x86_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = x86_cpu_type_name(cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

struct X86CPUDefinition {
    const char *name;
    uint32_t level;
    uint32_t xlevel;
    uint32_t xlevel2;
    /* vendor is zero-terminated, 12 character ASCII string */
    char vendor[CPUID_VENDOR_SZ + 1];
    int family;
    int model;
    int stepping;
    FeatureWordArray features;
    char model_id[48];
    bool cache_info_passthrough;
};

static X86CPUDefinition builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 4,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 6,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16 | CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .xlevel = 0x8000000A,
    },
    {
        .name = "phenom",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_HT,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_LBRV,
        .xlevel = 0x8000001A,
        .model_id = "AMD Phenom(tm) 9550 Quad-Core Processor"
    },
    {
        .name = "core2duo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 11,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_DTS | CPUID_ACPI | CPUID_SS |
            CPUID_HT | CPUID_TM | CPUID_PBE,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DTES64 | CPUID_EXT_DSCPL | CPUID_EXT_VMX | CPUID_EXT_EST |
            CPUID_EXT_TM2 | CPUID_EXT_CX16 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Core(TM)2 Duo CPU     T7700  @ 2.40GHz",
    },
    {
        .name = "kvm64",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        /* Missing: CPUID_VME, CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .features[FEAT_8000_0001_ECX] =
            0,
        .xlevel = 0x80000008,
        .model_id = "Common KVM processor"
    },
    {
        .name = "qemu32",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 6,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_POPCNT,
        .xlevel = 0x80000004,
    },
    {
        .name = "kvm32",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES,
        .features[FEAT_8000_0001_ECX] =
            0,
        .xlevel = 0x80000008,
        .model_id = "Common 32-bit KVM processor"
    },
    {
        .name = "coreduo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 14,
        .stepping = 8,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_DTS | CPUID_ACPI |
            CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_VMX |
            CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 4,
        .model = 8,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            I486_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 5,
        .model = 4,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium2",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 5,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            PENTIUM2_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium3",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 7,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM3_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "athlon",
        .level = 2,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_PSE36 | CPUID_VME | CPUID_MTRR |
            CPUID_MCA,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
        .xlevel = 0x80000008,
    },
    {
        .name = "n270",
        /* original is on level 10 */
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 28,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME | CPUID_DTS |
            CPUID_ACPI | CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
            /* Some CPUs got no CPUID_SEP */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DSCPL | CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR |
            CPUID_EXT_MOVBE,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
    {
        .name = "Conroe",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Celeron_4x0 (Conroe/Merom Class Core 2)",
    },
    {
        .name = "Penryn",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 23,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core 2 Duo P9xxx (Penryn Class Core 2)",
    },
    {
        .name = "Nehalem",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 26,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core i7 9xx (Nehalem Class Core i7)",
    },
    {
        .name = "Westmere",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 44,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Westmere E56xx/L56xx/X56xx (Nehalem-C)",
    },
    {
        .name = "SandyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 42,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
            CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Xeon E312xx (Sandy Bridge)",
    },
    {
        .name = "Haswell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 60,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core Processor (Haswell)",
    },
    {
        .name = "Broadwell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 61,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core Processor (Broadwell)",
    },
    {
        .name = "Opteron_G1",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
            CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
            CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
            CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
            CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
            CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 240 (Gen 1 Class Opteron)",
    },
    {
        .name = "Opteron_G2",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_CX16 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_FXSR |
            CPUID_EXT2_MMX | CPUID_EXT2_NX | CPUID_EXT2_PSE36 |
            CPUID_EXT2_PAT | CPUID_EXT2_CMOV | CPUID_EXT2_MCA |
            CPUID_EXT2_PGE | CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL |
            CPUID_EXT2_APIC | CPUID_EXT2_CX8 | CPUID_EXT2_MCE |
            CPUID_EXT2_PAE | CPUID_EXT2_MSR | CPUID_EXT2_TSC | CPUID_EXT2_PSE |
            CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 22xx (Gen 2 Class Opteron)",
    },
    {
        .name = "Opteron_G3",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_CX16 | CPUID_EXT_MONITOR |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_FXSR |
            CPUID_EXT2_MMX | CPUID_EXT2_NX | CPUID_EXT2_PSE36 |
            CPUID_EXT2_PAT | CPUID_EXT2_CMOV | CPUID_EXT2_MCA |
            CPUID_EXT2_PGE | CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL |
            CPUID_EXT2_APIC | CPUID_EXT2_CX8 | CPUID_EXT2_MCE |
            CPUID_EXT2_PAE | CPUID_EXT2_MSR | CPUID_EXT2_TSC | CPUID_EXT2_PSE |
            CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A |
            CPUID_EXT3_ABM | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 23xx (Gen 3 Class Opteron)",
    },
    {
        .name = "Opteron_G4",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 1,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_PDPE1GB | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
            CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
            CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
            CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
            CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
            CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 62xx class CPU",
    },
    {
        .name = "Opteron_G5",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 2,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_F16C | CPUID_EXT_AVX | CPUID_EXT_XSAVE |
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_FMA |
            CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_PDPE1GB | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
            CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
            CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
            CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
            CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
            CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_TBM | CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 63xx class CPU",
    },
};

/**
 * x86_cpu_compat_set_features:
 * @cpu_model: CPU model name to be changed. If NULL, all CPU models are changed
 * @w: Identifies the feature word to be changed.
 * @feat_add: Feature bits to be added to feature word
 * @feat_remove: Feature bits to be removed from feature word
 *
 * Change CPU model feature bits for compatibility.
 *
 * This function may be used by machine-type compatibility functions
 * to enable or disable feature bits on specific CPU models.
 */
void x86_cpu_compat_set_features(const char *cpu_model, FeatureWord w,
                                 uint32_t feat_add, uint32_t feat_remove)
{
    X86CPUDefinition *def;
    int i;
    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        def = &builtin_x86_defs[i];
        if (!cpu_model || !strcmp(cpu_model, def->name)) {
            def->features[w] |= feat_add;
            def->features[w] &= ~feat_remove;
        }
    }
}

#ifdef CONFIG_KVM

static int cpu_x86_fill_model_id(char *str)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    int i;

    for (i = 0; i < 3; i++) {
        host_cpuid(0x80000002 + i, 0, &eax, &ebx, &ecx, &edx);
        memcpy(str + i * 16 +  0, &eax, 4);
        memcpy(str + i * 16 +  4, &ebx, 4);
        memcpy(str + i * 16 +  8, &ecx, 4);
        memcpy(str + i * 16 + 12, &edx, 4);
    }
    return 0;
}

static X86CPUDefinition host_cpudef;

static Property host_x86_cpu_properties[] = {
    DEFINE_PROP_BOOL("migratable", X86CPU, migratable, true),
    DEFINE_PROP_END_OF_LIST()
};

/* class_init for the "host" CPU model
 *
 * This function may be called before KVM is initialized.
 */
static void host_x86_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    xcc->kvm_required = true;

    host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_vendor_words2str(host_cpudef.vendor, ebx, edx, ecx);

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    host_cpudef.family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
    host_cpudef.model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
    host_cpudef.stepping = eax & 0x0F;

    cpu_x86_fill_model_id(host_cpudef.model_id);

    xcc->cpu_def = &host_cpudef;
    host_cpudef.cache_info_passthrough = true;

    /* level, xlevel, xlevel2, and the feature words are initialized on
     * instance_init, because they require KVM to be initialized.
     */

    dc->props = host_x86_cpu_properties;
}

static uint32_t x86_cpu_get_supported_feature_word(FeatureWord w,
                                                   bool migratable_only);

static void host_x86_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    KVMState *s = kvm_state;
    FeatureWord w;

    assert(kvm_enabled());

    env->cpuid_level = kvm_arch_get_supported_cpuid(s, 0x0, 0, R_EAX);
    env->cpuid_xlevel = kvm_arch_get_supported_cpuid(s, 0x80000000, 0, R_EAX);
    env->cpuid_xlevel2 = kvm_arch_get_supported_cpuid(s, 0xC0000000, 0, R_EAX);

    for (w = 0; w < FEATURE_WORDS; w++) {
        env->features[w] =
            x86_cpu_get_supported_feature_word(w, cpu->migratable);
    }
    object_property_set_bool(OBJECT(cpu), true, "pmu", &error_abort);
}

static const TypeInfo host_x86_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("host"),
    .parent = TYPE_X86_CPU,
    .instance_init = host_x86_cpu_initfn,
    .class_init = host_x86_cpu_class_init,
};

#endif

static void report_unavailable_features(FeatureWord w, uint32_t mask)
{
    FeatureWordInfo *f = &feature_word_info[w];
    int i;

    for (i = 0; i < 32; ++i) {
        if (1 << i & mask) {
            const char *reg = get_register_name_32(f->cpuid_reg);
            assert(reg);
            fprintf(stderr, "warning: %s doesn't support requested feature: "
                "CPUID.%02XH:%s%s%s [bit %d]\n",
                kvm_enabled() ? "host" : "TCG",
                f->cpuid_eax, reg,
                f->feat_names[i] ? "." : "",
                f->feat_names[i] ? f->feat_names[i] : "", i);
        }
    }
}

static void x86_cpuid_version_get_family(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 8) & 0xf;
    if (value == 0xf) {
        value += (env->cpuid_version >> 20) & 0xff;
    }
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_version_set_family(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff + 0xf;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xff00f00;
    if (value > 0x0f) {
        env->cpuid_version |= 0xf00 | ((value - 0x0f) << 20);
    } else {
        env->cpuid_version |= value << 8;
    }
}

static void x86_cpuid_version_get_model(Object *obj, Visitor *v, void *opaque,
                                        const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 4) & 0xf;
    value |= ((env->cpuid_version >> 16) & 0xf) << 4;
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_version_set_model(Object *obj, Visitor *v, void *opaque,
                                        const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf00f0;
    env->cpuid_version |= ((value & 0xf) << 4) | ((value >> 4) << 16);
}

static void x86_cpuid_version_get_stepping(Object *obj, Visitor *v,
                                           void *opaque, const char *name,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = env->cpuid_version & 0xf;
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_version_set_stepping(Object *obj, Visitor *v,
                                           void *opaque, const char *name,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xf;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf;
    env->cpuid_version |= value & 0xf;
}

static void x86_cpuid_get_level(Object *obj, Visitor *v, void *opaque,
                                const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);

    visit_type_uint32(v, &cpu->env.cpuid_level, name, errp);
}

static void x86_cpuid_set_level(Object *obj, Visitor *v, void *opaque,
                                const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);

    visit_type_uint32(v, &cpu->env.cpuid_level, name, errp);
}

static void x86_cpuid_get_xlevel(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);

    visit_type_uint32(v, &cpu->env.cpuid_xlevel, name, errp);
}

static void x86_cpuid_set_xlevel(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);

    visit_type_uint32(v, &cpu->env.cpuid_xlevel, name, errp);
}

static char *x86_cpuid_get_vendor(Object *obj, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;

    value = (char *)g_malloc(CPUID_VENDOR_SZ + 1);
    x86_cpu_vendor_words2str(value, env->cpuid_vendor1, env->cpuid_vendor2,
                             env->cpuid_vendor3);
    return value;
}

static void x86_cpuid_set_vendor(Object *obj, const char *value,
                                 Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int i;

    if (strlen(value) != CPUID_VENDOR_SZ) {
        error_set(errp, QERR_PROPERTY_VALUE_BAD, "",
                  "vendor", value);
        return;
    }

    env->cpuid_vendor1 = 0;
    env->cpuid_vendor2 = 0;
    env->cpuid_vendor3 = 0;
    for (i = 0; i < 4; i++) {
        env->cpuid_vendor1 |= ((uint8_t)value[i    ]) << (8 * i);
        env->cpuid_vendor2 |= ((uint8_t)value[i + 4]) << (8 * i);
        env->cpuid_vendor3 |= ((uint8_t)value[i + 8]) << (8 * i);
    }
}

static char *x86_cpuid_get_model_id(Object *obj, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;
    int i;

    value = g_malloc(48 + 1);
    for (i = 0; i < 48; i++) {
        value[i] = env->cpuid_model[i >> 2] >> (8 * (i & 3));
    }
    value[48] = '\0';
    return value;
}

static void x86_cpuid_set_model_id(Object *obj, const char *model_id,
                                   Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int c, len, i;

    if (model_id == NULL) {
        model_id = "";
    }
    len = strlen(model_id);
    memset(env->cpuid_model, 0, 48);
    for (i = 0; i < 48; i++) {
        if (i >= len) {
            c = '\0';
        } else {
            c = (uint8_t)model_id[i];
        }
        env->cpuid_model[i >> 2] |= c << (8 * (i & 3));
    }
}

static void x86_cpuid_get_tsc_freq(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value;

    value = cpu->env.tsc_khz * 1000;
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_set_tsc_freq(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    const int64_t min = 0;
    const int64_t max = INT64_MAX;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    cpu->env.tsc_khz = value / 1000;
}

static void x86_cpuid_get_apic_id(Object *obj, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value = cpu->env.cpuid_apic_id;

    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_set_apic_id(Object *obj, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    DeviceState *dev = DEVICE(obj);
    const int64_t min = 0;
    const int64_t max = UINT32_MAX;
    Error *error = NULL;
    int64_t value;

    if (dev->realized) {
        error_setg(errp, "Attempt to set property '%s' on '%s' after "
                   "it was realized", name, object_get_typename(obj));
        return;
    }

    visit_type_int(v, &value, name, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, "Property %s.%s doesn't take value %" PRId64
                   " (minimum: %" PRId64 ", maximum: %" PRId64 ")" ,
                   object_get_typename(obj), name, value, min, max);
        return;
    }

    if ((value != cpu->env.cpuid_apic_id) && cpu_exists(value)) {
        error_setg(errp, "CPU with APIC ID %" PRIi64 " exists", value);
        return;
    }
    cpu->env.cpuid_apic_id = value;
}

/* Generic getter for "feature-words" and "filtered-features" properties */
static void x86_cpu_get_feature_words(Object *obj, Visitor *v, void *opaque,
                                      const char *name, Error **errp)
{
    uint32_t *array = (uint32_t *)opaque;
    FeatureWord w;
    Error *err = NULL;
    X86CPUFeatureWordInfo word_infos[FEATURE_WORDS] = { };
    X86CPUFeatureWordInfoList list_entries[FEATURE_WORDS] = { };
    X86CPUFeatureWordInfoList *list = NULL;

    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *wi = &feature_word_info[w];
        X86CPUFeatureWordInfo *qwi = &word_infos[w];
        qwi->cpuid_input_eax = wi->cpuid_eax;
        qwi->has_cpuid_input_ecx = wi->cpuid_needs_ecx;
        qwi->cpuid_input_ecx = wi->cpuid_ecx;
        qwi->cpuid_register = x86_reg_info_32[wi->cpuid_reg].qapi_enum;
        qwi->features = array[w];

        /* List will be in reverse order, but order shouldn't matter */
        list_entries[w].next = list;
        list_entries[w].value = &word_infos[w];
        list = &list_entries[w];
    }

    visit_type_X86CPUFeatureWordInfoList(v, &list, "feature-words", &err);
    error_propagate(errp, err);
}

static void x86_get_hv_spinlocks(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value = cpu->hyperv_spinlock_attempts;

    visit_type_int(v, &value, name, errp);
}

static void x86_set_hv_spinlocks(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    const int64_t min = 0xFFF;
    const int64_t max = UINT_MAX;
    X86CPU *cpu = X86_CPU(obj);
    Error *err = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (value < min || value > max) {
        error_setg(errp, "Property %s.%s doesn't take value %" PRId64
                  " (minimum: %" PRId64 ", maximum: %" PRId64 ")",
                  object_get_typename(obj), name ? name : "null",
                  value, min, max);
        return;
    }
    cpu->hyperv_spinlock_attempts = value;
}

static PropertyInfo qdev_prop_spinlocks = {
    .name  = "int",
    .get   = x86_get_hv_spinlocks,
    .set   = x86_set_hv_spinlocks,
};

/* Convert all '_' in a feature string option name to '-', to make feature
 * name conform to QOM property naming rule, which uses '-' instead of '_'.
 */
static inline void feat2prop(char *s)
{
    while ((s = strchr(s, '_'))) {
        *s = '-';
    }
}

/* Parse "+feature,-feature,feature=foo" CPU feature string
 */
static void x86_cpu_parse_featurestr(CPUState *cs, char *features,
                                     Error **errp)
{
    X86CPU *cpu = X86_CPU(cs);
    char *featurestr; /* Single 'key=value" string being parsed */
    FeatureWord w;
    /* Features to be added */
    FeatureWordArray plus_features = { 0 };
    /* Features to be removed */
    FeatureWordArray minus_features = { 0 };
    uint32_t numvalue;
    CPUX86State *env = &cpu->env;
    Error *local_err = NULL;

    featurestr = features ? strtok(features, ",") : NULL;

    while (featurestr) {
        char *val;
        if (featurestr[0] == '+') {
            add_flagname_to_bitmaps(featurestr + 1, plus_features);
        } else if (featurestr[0] == '-') {
            add_flagname_to_bitmaps(featurestr + 1, minus_features);
        } else if ((val = strchr(featurestr, '='))) {
            *val = 0; val++;
            feat2prop(featurestr);
            if (!strcmp(featurestr, "xlevel")) {
                char *err;
                char num[32];

                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    error_setg(errp, "bad numerical value %s", val);
                    return;
                }
                if (numvalue < 0x80000000) {
                    error_report("xlevel value shall always be >= 0x80000000"
                                 ", fixup will be removed in future versions");
                    numvalue += 0x80000000;
                }
                snprintf(num, sizeof(num), "%" PRIu32, numvalue);
                object_property_parse(OBJECT(cpu), num, featurestr, &local_err);
            } else if (!strcmp(featurestr, "tsc-freq")) {
                int64_t tsc_freq;
                char *err;
                char num[32];

                tsc_freq = strtosz_suffix_unit(val, &err,
                                               STRTOSZ_DEFSUFFIX_B, 1000);
                if (tsc_freq < 0 || *err) {
                    error_setg(errp, "bad numerical value %s", val);
                    return;
                }
                snprintf(num, sizeof(num), "%" PRId64, tsc_freq);
                object_property_parse(OBJECT(cpu), num, "tsc-frequency",
                                      &local_err);
            } else if (!strcmp(featurestr, "hv-spinlocks")) {
                char *err;
                const int min = 0xFFF;
                char num[32];
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    error_setg(errp, "bad numerical value %s", val);
                    return;
                }
                if (numvalue < min) {
                    error_report("hv-spinlocks value shall always be >= 0x%x"
                            ", fixup will be removed in future versions",
                            min);
                    numvalue = min;
                }
                snprintf(num, sizeof(num), "%" PRId32, numvalue);
                object_property_parse(OBJECT(cpu), num, featurestr, &local_err);
            } else {
                object_property_parse(OBJECT(cpu), val, featurestr, &local_err);
            }
        } else {
            feat2prop(featurestr);
            object_property_parse(OBJECT(cpu), "on", featurestr, &local_err);
        }
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        featurestr = strtok(NULL, ",");
    }

    for (w = 0; w < FEATURE_WORDS; w++) {
        env->features[w] |= plus_features[w];
        env->features[w] &= ~minus_features[w];
    }
}

/* generate a composite string into buf of all cpuid names in featureset
 * selected by fbits.  indicate truncation at bufsize in the event of overflow.
 * if flags, suppress names undefined in featureset.
 */
static void listflags(char *buf, int bufsize, uint32_t fbits,
    const char **featureset, uint32_t flags)
{
    const char **p = &featureset[31];
    char *q, *b, bit;
    int nc;

    b = 4 <= bufsize ? buf + (bufsize -= 3) - 1 : NULL;
    *buf = '\0';
    for (q = buf, bit = 31; fbits && bufsize; --p, fbits &= ~(1 << bit), --bit)
        if (fbits & 1 << bit && (*p || !flags)) {
            if (*p)
                nc = snprintf(q, bufsize, "%s%s", q == buf ? "" : " ", *p);
            else
                nc = snprintf(q, bufsize, "%s[%d]", q == buf ? "" : " ", bit);
            if (bufsize <= nc) {
                if (b) {
                    memcpy(b, "...", sizeof("..."));
                }
                return;
            }
            q += nc;
            bufsize -= nc;
        }
}

/* generate CPU information. */
void x86_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    X86CPUDefinition *def;
    char buf[256];
    int i;

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        def = &builtin_x86_defs[i];
        snprintf(buf, sizeof(buf), "%s", def->name);
        (*cpu_fprintf)(f, "x86 %16s  %-48s\n", buf, def->model_id);
    }
#ifdef CONFIG_KVM
    (*cpu_fprintf)(f, "x86 %16s  %-48s\n", "host",
                   "KVM processor with all supported host features "
                   "(only available in KVM mode)");
#endif

    (*cpu_fprintf)(f, "\nRecognized CPUID flags:\n");
    for (i = 0; i < ARRAY_SIZE(feature_word_info); i++) {
        FeatureWordInfo *fw = &feature_word_info[i];

        listflags(buf, sizeof(buf), (uint32_t)~0, fw->feat_names, 1);
        (*cpu_fprintf)(f, "  %s\n", buf);
    }
}

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    X86CPUDefinition *def;
    int i;

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        CpuDefinitionInfoList *entry;
        CpuDefinitionInfo *info;

        def = &builtin_x86_defs[i];
        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(def->name);

        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = cpu_list;
        cpu_list = entry;
    }

    return cpu_list;
}

static uint32_t x86_cpu_get_supported_feature_word(FeatureWord w,
                                                   bool migratable_only)
{
    FeatureWordInfo *wi = &feature_word_info[w];
    uint32_t r;

    if (kvm_enabled()) {
        r = kvm_arch_get_supported_cpuid(kvm_state, wi->cpuid_eax,
                                                    wi->cpuid_ecx,
                                                    wi->cpuid_reg);
    } else if (tcg_enabled()) {
        r = wi->tcg_features;
    } else {
        return ~0;
    }
    if (migratable_only) {
        r &= x86_cpu_get_migratable_flags(w);
    }
    return r;
}

/*
 * Filters CPU feature words based on host availability of each feature.
 *
 * Returns: 0 if all flags are supported by the host, non-zero otherwise.
 */
static int x86_cpu_filter_features(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    FeatureWord w;
    int rv = 0;

    for (w = 0; w < FEATURE_WORDS; w++) {
        uint32_t host_feat =
            x86_cpu_get_supported_feature_word(w, cpu->migratable);
        uint32_t requested_features = env->features[w];
        env->features[w] &= host_feat;
        cpu->filtered_features[w] = requested_features & ~env->features[w];
        if (cpu->filtered_features[w]) {
            if (cpu->check_cpuid || cpu->enforce_cpuid) {
                report_unavailable_features(w, cpu->filtered_features[w]);
            }
            rv = 1;
        }
    }

    return rv;
}

/* Load data from X86CPUDefinition
 */
static void x86_cpu_load_def(X86CPU *cpu, X86CPUDefinition *def, Error **errp)
{
    CPUX86State *env = &cpu->env;
    const char *vendor;
    char host_vendor[CPUID_VENDOR_SZ + 1];
    FeatureWord w;

    object_property_set_int(OBJECT(cpu), def->level, "level", errp);
    object_property_set_int(OBJECT(cpu), def->family, "family", errp);
    object_property_set_int(OBJECT(cpu), def->model, "model", errp);
    object_property_set_int(OBJECT(cpu), def->stepping, "stepping", errp);
    object_property_set_int(OBJECT(cpu), def->xlevel, "xlevel", errp);
    env->cpuid_xlevel2 = def->xlevel2;
    cpu->cache_info_passthrough = def->cache_info_passthrough;
    object_property_set_str(OBJECT(cpu), def->model_id, "model-id", errp);
    for (w = 0; w < FEATURE_WORDS; w++) {
        env->features[w] = def->features[w];
    }

    /* Special cases not set in the X86CPUDefinition structs: */
    if (kvm_enabled()) {
        FeatureWord w;
        for (w = 0; w < FEATURE_WORDS; w++) {
            env->features[w] |= kvm_default_features[w];
            env->features[w] &= ~kvm_default_unset_features[w];
        }
    }

    env->features[FEAT_1_ECX] |= CPUID_EXT_HYPERVISOR;

    /* sysenter isn't supported in compatibility mode on AMD,
     * syscall isn't supported in compatibility mode on Intel.
     * Normally we advertise the actual CPU vendor, but you can
     * override this using the 'vendor' property if you want to use
     * KVM's sysenter/syscall emulation in compatibility mode and
     * when doing cross vendor migration
     */
    vendor = def->vendor;
    if (kvm_enabled()) {
        uint32_t  ebx = 0, ecx = 0, edx = 0;
        host_cpuid(0, 0, NULL, &ebx, &ecx, &edx);
        x86_cpu_vendor_words2str(host_vendor, ebx, edx, ecx);
        vendor = host_vendor;
    }

    object_property_set_str(OBJECT(cpu), vendor, "vendor", errp);

}

X86CPU *cpu_x86_create(const char *cpu_model, DeviceState *icc_bridge,
                       Error **errp)
{
    X86CPU *cpu = NULL;
    X86CPUClass *xcc;
    ObjectClass *oc;
    gchar **model_pieces;
    char *name, *features;
    Error *error = NULL;

    model_pieces = g_strsplit(cpu_model, ",", 2);
    if (!model_pieces[0]) {
        error_setg(&error, "Invalid/empty CPU model name");
        goto out;
    }
    name = model_pieces[0];
    features = model_pieces[1];

    oc = x86_cpu_class_by_name(name);
    if (oc == NULL) {
        error_setg(&error, "Unable to find CPU definition: %s", name);
        goto out;
    }
    xcc = X86_CPU_CLASS(oc);

    if (xcc->kvm_required && !kvm_enabled()) {
        error_setg(&error, "CPU model '%s' requires KVM", name);
        goto out;
    }

    cpu = X86_CPU(object_new(object_class_get_name(oc)));

#ifndef CONFIG_USER_ONLY
    if (icc_bridge == NULL) {
        error_setg(&error, "Invalid icc-bridge value");
        goto out;
    }
    qdev_set_parent_bus(DEVICE(cpu), qdev_get_child_bus(icc_bridge, "icc"));
    object_unref(OBJECT(cpu));
#endif

    x86_cpu_parse_featurestr(CPU(cpu), features, &error);
    if (error) {
        goto out;
    }

out:
    if (error != NULL) {
        error_propagate(errp, error);
        if (cpu) {
            object_unref(OBJECT(cpu));
            cpu = NULL;
        }
    }
    g_strfreev(model_pieces);
    return cpu;
}

X86CPU *cpu_x86_init(const char *cpu_model)
{
    Error *error = NULL;
    X86CPU *cpu;

    cpu = cpu_x86_create(cpu_model, NULL, &error);
    if (error) {
        goto out;
    }

    object_property_set_bool(OBJECT(cpu), true, "realized", &error);

out:
    if (error) {
        error_report("%s", error_get_pretty(error));
        error_free(error);
        if (cpu != NULL) {
            object_unref(OBJECT(cpu));
            cpu = NULL;
        }
    }
    return cpu;
}

static void x86_cpu_cpudef_class_init(ObjectClass *oc, void *data)
{
    X86CPUDefinition *cpudef = data;
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->cpu_def = cpudef;
}

static void x86_register_cpudef_type(X86CPUDefinition *def)
{
    char *typename = x86_cpu_type_name(def->name);
    TypeInfo ti = {
        .name = typename,
        .parent = TYPE_X86_CPU,
        .class_init = x86_cpu_cpudef_class_init,
        .class_data = def,
    };

    type_register(&ti);
    g_free(typename);
}

#if !defined(CONFIG_USER_ONLY)

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] &= ~CPUID_APIC;
}

#endif /* !CONFIG_USER_ONLY */

/* Initialize list of CPU models, filling some non-static fields if necessary
 */
void x86_cpudef_setup(void)
{
    int i, j;
    static const char *model_with_versions[] = { "qemu32", "qemu64", "athlon" };

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); ++i) {
        X86CPUDefinition *def = &builtin_x86_defs[i];

        /* Look for specific "cpudef" models that */
        /* have the QEMU version in .model_id */
        for (j = 0; j < ARRAY_SIZE(model_with_versions); j++) {
            if (strcmp(model_with_versions[j], def->name) == 0) {
                pstrcpy(def->model_id, sizeof(def->model_id),
                        "QEMU Virtual CPU version ");
                pstrcat(def->model_id, sizeof(def->model_id),
                        qemu_get_version());
                break;
            }
        }
    }
}

static void get_cpuid_vendor(CPUX86State *env, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx)
{
    *ebx = env->cpuid_vendor1;
    *edx = env->cpuid_vendor2;
    *ecx = env->cpuid_vendor3;
}

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    /* test if maximum index reached */
    if (index & 0x80000000) {
        if (index > env->cpuid_xlevel) {
            if (env->cpuid_xlevel2 > 0) {
                /* Handle the Centaur's CPUID instruction. */
                if (index > env->cpuid_xlevel2) {
                    index = env->cpuid_xlevel2;
                } else if (index < 0xC0000000) {
                    index = env->cpuid_xlevel;
                }
            } else {
                /* Intel documentation states that invalid EAX input will
                 * return the same information as EAX=cpuid_level
                 * (Intel SDM Vol. 2A - Instruction Set Reference - CPUID)
                 */
                index =  env->cpuid_level;
            }
        }
    } else {
        if (index > env->cpuid_level)
            index = env->cpuid_level;
    }

    switch(index) {
    case 0:
        *eax = env->cpuid_level;
        get_cpuid_vendor(env, ebx, ecx, edx);
        break;
    case 1:
        *eax = env->cpuid_version;
        *ebx = (env->cpuid_apic_id << 24) | 8 << 8; /* CLFLUSH size in quad words, Linux wants it. */
        *ecx = env->features[FEAT_1_ECX];
        *edx = env->features[FEAT_1_EDX];
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ebx |= (cs->nr_cores * cs->nr_threads) << 16;
            *edx |= 1 << 28;    /* HTT bit */
        }
        break;
    case 2:
        /* cache info: needed for Pentium Pro compatibility */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = 1; /* Number of CPUID[EAX=2] calls required */
        *ebx = 0;
        *ecx = 0;
        *edx = (L1D_DESCRIPTOR << 16) | \
               (L1I_DESCRIPTOR <<  8) | \
               (L2_DESCRIPTOR);
        break;
    case 4:
        /* cache info: needed for Core compatibility */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, count, eax, ebx, ecx, edx);
            *eax &= ~0xFC000000;
        } else {
            *eax = 0;
            switch (count) {
            case 0: /* L1 dcache info */
                *eax |= CPUID_4_TYPE_DCACHE | \
                        CPUID_4_LEVEL(1) | \
                        CPUID_4_SELF_INIT_LEVEL;
                *ebx = (L1D_LINE_SIZE - 1) | \
                       ((L1D_PARTITIONS - 1) << 12) | \
                       ((L1D_ASSOCIATIVITY - 1) << 22);
                *ecx = L1D_SETS - 1;
                *edx = CPUID_4_NO_INVD_SHARING;
                break;
            case 1: /* L1 icache info */
                *eax |= CPUID_4_TYPE_ICACHE | \
                        CPUID_4_LEVEL(1) | \
                        CPUID_4_SELF_INIT_LEVEL;
                *ebx = (L1I_LINE_SIZE - 1) | \
                       ((L1I_PARTITIONS - 1) << 12) | \
                       ((L1I_ASSOCIATIVITY - 1) << 22);
                *ecx = L1I_SETS - 1;
                *edx = CPUID_4_NO_INVD_SHARING;
                break;
            case 2: /* L2 cache info */
                *eax |= CPUID_4_TYPE_UNIFIED | \
                        CPUID_4_LEVEL(2) | \
                        CPUID_4_SELF_INIT_LEVEL;
                if (cs->nr_threads > 1) {
                    *eax |= (cs->nr_threads - 1) << 14;
                }
                *ebx = (L2_LINE_SIZE - 1) | \
                       ((L2_PARTITIONS - 1) << 12) | \
                       ((L2_ASSOCIATIVITY - 1) << 22);
                *ecx = L2_SETS - 1;
                *edx = CPUID_4_NO_INVD_SHARING;
                break;
            default: /* end of info */
                *eax = 0;
                *ebx = 0;
                *ecx = 0;
                *edx = 0;
                break;
            }
        }

        /* QEMU gives out its own APIC IDs, never pass down bits 31..26.  */
        if ((*eax & 31) && cs->nr_cores > 1) {
            *eax |= (cs->nr_cores - 1) << 26;
        }
        break;
    case 5:
        /* mwait info: needed for Core compatibility */
        *eax = 0; /* Smallest monitor-line size in bytes */
        *ebx = 0; /* Largest monitor-line size in bytes */
        *ecx = CPUID_MWAIT_EMX | CPUID_MWAIT_IBE;
        *edx = 0;
        break;
    case 6:
        /* Thermal and Power Leaf */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 7:
        /* Structured Extended Feature Flags Enumeration Leaf */
        if (count == 0) {
            *eax = 0; /* Maximum ECX value for sub-leaves */
            *ebx = env->features[FEAT_7_0_EBX]; /* Feature flags */
            *ecx = 0; /* Reserved */
            *edx = 0; /* Reserved */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 9:
        /* Direct Cache Access Information Leaf */
        *eax = 0; /* Bits 0-31 in DCA_CAP MSR */
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xA:
        /* Architectural Performance Monitoring Leaf */
        if (kvm_enabled() && cpu->enable_pmu) {
            KVMState *s = cs->kvm_state;

            *eax = kvm_arch_get_supported_cpuid(s, 0xA, count, R_EAX);
            *ebx = kvm_arch_get_supported_cpuid(s, 0xA, count, R_EBX);
            *ecx = kvm_arch_get_supported_cpuid(s, 0xA, count, R_ECX);
            *edx = kvm_arch_get_supported_cpuid(s, 0xA, count, R_EDX);
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0xD: {
        KVMState *s = cs->kvm_state;
        uint64_t kvm_mask;
        int i;

        /* Processor Extended State */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) || !kvm_enabled()) {
            break;
        }
        kvm_mask =
            kvm_arch_get_supported_cpuid(s, 0xd, 0, R_EAX) |
            ((uint64_t)kvm_arch_get_supported_cpuid(s, 0xd, 0, R_EDX) << 32);

        if (count == 0) {
            *ecx = 0x240;
            for (i = 2; i < ARRAY_SIZE(ext_save_areas); i++) {
                const ExtSaveArea *esa = &ext_save_areas[i];
                if ((env->features[esa->feature] & esa->bits) == esa->bits &&
                    (kvm_mask & (1 << i)) != 0) {
                    if (i < 32) {
                        *eax |= 1 << i;
                    } else {
                        *edx |= 1 << (i - 32);
                    }
                    *ecx = MAX(*ecx, esa->offset + esa->size);
                }
            }
            *eax |= kvm_mask & (XSTATE_FP | XSTATE_SSE);
            *ebx = *ecx;
        } else if (count == 1) {
            *eax = kvm_arch_get_supported_cpuid(s, 0xd, 1, R_EAX);
        } else if (count < ARRAY_SIZE(ext_save_areas)) {
            const ExtSaveArea *esa = &ext_save_areas[count];
            if ((env->features[esa->feature] & esa->bits) == esa->bits &&
                (kvm_mask & (1 << count)) != 0) {
                *eax = esa->size;
                *ebx = esa->offset;
            }
        }
        break;
    }
    case 0x80000000:
        *eax = env->cpuid_xlevel;
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 0x80000001:
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = env->features[FEAT_8000_0001_ECX];
        *edx = env->features[FEAT_8000_0001_EDX];

        /* The Linux kernel checks for the CMPLegacy bit and
         * discards multiple thread information if it is set.
         * So dont set it here for Intel to make Linux guests happy.
         */
        if (cs->nr_cores * cs->nr_threads > 1) {
            uint32_t tebx, tecx, tedx;
            get_cpuid_vendor(env, &tebx, &tecx, &tedx);
            if (tebx != CPUID_VENDOR_INTEL_1 ||
                tedx != CPUID_VENDOR_INTEL_2 ||
                tecx != CPUID_VENDOR_INTEL_3) {
                *ecx |= 1 << 1;    /* CmpLegacy bit */
            }
        }
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        *eax = env->cpuid_model[(index - 0x80000002) * 4 + 0];
        *ebx = env->cpuid_model[(index - 0x80000002) * 4 + 1];
        *ecx = env->cpuid_model[(index - 0x80000002) * 4 + 2];
        *edx = env->cpuid_model[(index - 0x80000002) * 4 + 3];
        break;
    case 0x80000005:
        /* cache info (L1 cache) */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = (L1_DTLB_2M_ASSOC << 24) | (L1_DTLB_2M_ENTRIES << 16) | \
               (L1_ITLB_2M_ASSOC <<  8) | (L1_ITLB_2M_ENTRIES);
        *ebx = (L1_DTLB_4K_ASSOC << 24) | (L1_DTLB_4K_ENTRIES << 16) | \
               (L1_ITLB_4K_ASSOC <<  8) | (L1_ITLB_4K_ENTRIES);
        *ecx = (L1D_SIZE_KB_AMD << 24) | (L1D_ASSOCIATIVITY_AMD << 16) | \
               (L1D_LINES_PER_TAG << 8) | (L1D_LINE_SIZE);
        *edx = (L1I_SIZE_KB_AMD << 24) | (L1I_ASSOCIATIVITY_AMD << 16) | \
               (L1I_LINES_PER_TAG << 8) | (L1I_LINE_SIZE);
        break;
    case 0x80000006:
        /* cache info (L2 cache) */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = (AMD_ENC_ASSOC(L2_DTLB_2M_ASSOC) << 28) | \
               (L2_DTLB_2M_ENTRIES << 16) | \
               (AMD_ENC_ASSOC(L2_ITLB_2M_ASSOC) << 12) | \
               (L2_ITLB_2M_ENTRIES);
        *ebx = (AMD_ENC_ASSOC(L2_DTLB_4K_ASSOC) << 28) | \
               (L2_DTLB_4K_ENTRIES << 16) | \
               (AMD_ENC_ASSOC(L2_ITLB_4K_ASSOC) << 12) | \
               (L2_ITLB_4K_ENTRIES);
        *ecx = (L2_SIZE_KB_AMD << 16) | \
               (AMD_ENC_ASSOC(L2_ASSOCIATIVITY) << 12) | \
               (L2_LINES_PER_TAG << 8) | (L2_LINE_SIZE);
        *edx = ((L3_SIZE_KB/512) << 18) | \
               (AMD_ENC_ASSOC(L3_ASSOCIATIVITY) << 12) | \
               (L3_LINES_PER_TAG << 8) | (L3_LINE_SIZE);
        break;
    case 0x80000007:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_8000_0007_EDX];
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
/* XXX: This value must match the one used in the MMU code. */
        if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
            /* 64 bit processor */
/* XXX: The physical address space is limited to 42 bits in exec.c. */
            *eax = 0x00003028; /* 48 bits virtual, 40 bits physical */
        } else {
            if (env->features[FEAT_1_EDX] & CPUID_PSE36) {
                *eax = 0x00000024; /* 36 bits physical */
            } else {
                *eax = 0x00000020; /* 32 bits physical */
            }
        }
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ecx |= (cs->nr_cores * cs->nr_threads) - 1;
        }
        break;
    case 0x8000000A:
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            *eax = 0x00000001; /* SVM Revision */
            *ebx = 0x00000010; /* nr of ASIDs */
            *ecx = 0;
            *edx = env->features[FEAT_SVM]; /* optional features */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0xC0000000:
        *eax = env->cpuid_xlevel2;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xC0000001:
        /* Support for VIA CPU's CPUID instruction */
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_C000_0001_EDX];
        break;
    case 0xC0000002:
    case 0xC0000003:
    case 0xC0000004:
        /* Reserved for the future, and now filled with zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    default:
        /* reserved values: zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    }
}

/* CPUClass::reset() */
static void x86_cpu_reset(CPUState *s)
{
    X86CPU *cpu = X86_CPU(s);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);
    CPUX86State *env = &cpu->env;
    int i;

    xcc->parent_reset(s);

    memset(env, 0, offsetof(CPUX86State, cpuid_level));

    tlb_flush(s, 1);

    env->old_exception = -1;

    /* init to reset state */

#ifdef CONFIG_SOFTMMU
    env->hflags |= HF_SOFTMMU_MASK;
#endif
    env->hflags2 |= HF2_GIF_MASK;

    cpu_x86_update_cr0(env, 0x60000010);
    env->a20_mask = ~0x0;
    env->smbase = 0x30000;

    env->idt.limit = 0xffff;
    env->gdt.limit = 0xffff;
    env->ldt.limit = 0xffff;
    env->ldt.flags = DESC_P_MASK | (2 << DESC_TYPE_SHIFT);
    env->tr.limit = 0xffff;
    env->tr.flags = DESC_P_MASK | (11 << DESC_TYPE_SHIFT);

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, 0xffff0000, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);

    env->eip = 0xfff0;
    env->regs[R_EDX] = env->cpuid_version;

    env->eflags = 0x2;

    /* FPU init */
    for (i = 0; i < 8; i++) {
        env->fptags[i] = 1;
    }
    env->fpuc = 0x37f;

    env->mxcsr = 0x1f80;
    env->xstate_bv = XSTATE_FP | XSTATE_SSE;

    env->pat = 0x0007040600070406ULL;
    env->msr_ia32_misc_enable = MSR_IA32_MISC_ENABLE_DEFAULT;

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(s, BP_CPU);
    cpu_watchpoint_remove_all(s, BP_CPU);

    env->xcr0 = 1;

#if !defined(CONFIG_USER_ONLY)
    /* We hard-wire the BSP to the first CPU. */
    if (s->cpu_index == 0) {
        apic_designate_bsp(cpu->apic_state);
    }

    s->halted = !cpu_is_bsp(cpu);

    if (kvm_enabled()) {
        kvm_arch_reset_vcpu(cpu);
    }
#endif
}

#ifndef CONFIG_USER_ONLY
bool cpu_is_bsp(X86CPU *cpu)
{
    return cpu_get_apic_base(cpu->apic_state) & MSR_IA32_APICBASE_BSP;
}

/* TODO: remove me, when reset over QOM tree is implemented */
static void x86_cpu_machine_reset_cb(void *opaque)
{
    X86CPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}
#endif

static void mce_init(X86CPU *cpu)
{
    CPUX86State *cenv = &cpu->env;
    unsigned int bank;

    if (((cenv->cpuid_version >> 8) & 0xf) >= 6
        && (cenv->features[FEAT_1_EDX] & (CPUID_MCE | CPUID_MCA)) ==
            (CPUID_MCE | CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF;
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

#ifndef CONFIG_USER_ONLY
static void x86_cpu_apic_create(X86CPU *cpu, Error **errp)
{
    CPUX86State *env = &cpu->env;
    DeviceState *dev = DEVICE(cpu);
    APICCommonState *apic;
    const char *apic_type = "apic";

    if (kvm_irqchip_in_kernel()) {
        apic_type = "kvm-apic";
    } else if (xen_enabled()) {
        apic_type = "xen-apic";
    }

    cpu->apic_state = qdev_try_create(qdev_get_parent_bus(dev), apic_type);
    if (cpu->apic_state == NULL) {
        error_setg(errp, "APIC device '%s' could not be created", apic_type);
        return;
    }

    object_property_add_child(OBJECT(cpu), "apic",
                              OBJECT(cpu->apic_state), NULL);
    qdev_prop_set_uint8(cpu->apic_state, "id", env->cpuid_apic_id);
    /* TODO: convert to link<> */
    apic = APIC_COMMON(cpu->apic_state);
    apic->cpu = cpu;
}

static void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
    if (cpu->apic_state == NULL) {
        return;
    }

    if (qdev_init(cpu->apic_state)) {
        error_setg(errp, "APIC device '%s' could not be initialized",
                   object_get_typename(OBJECT(cpu->apic_state)));
        return;
    }
}
#else
static void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
}
#endif

static void x86_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
    CPUX86State *env = &cpu->env;
    Error *local_err = NULL;

    if (env->features[FEAT_7_0_EBX] && env->cpuid_level < 7) {
        env->cpuid_level = 7;
    }

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (env->cpuid_vendor1 == CPUID_VENDOR_AMD_1 &&
        env->cpuid_vendor2 == CPUID_VENDOR_AMD_2 &&
        env->cpuid_vendor3 == CPUID_VENDOR_AMD_3) {
        env->features[FEAT_8000_0001_EDX] &= ~CPUID_EXT2_AMD_ALIASES;
        env->features[FEAT_8000_0001_EDX] |= (env->features[FEAT_1_EDX]
           & CPUID_EXT2_AMD_ALIASES);
    }


    if (x86_cpu_filter_features(cpu) && cpu->enforce_cpuid) {
        error_setg(&local_err,
                   kvm_enabled() ?
                       "Host doesn't support requested features" :
                       "TCG doesn't support requested features");
        goto out;
    }

#ifndef CONFIG_USER_ONLY
    qemu_register_reset(x86_cpu_machine_reset_cb, cpu);

    if (cpu->env.features[FEAT_1_EDX] & CPUID_APIC || smp_cpus > 1) {
        x86_cpu_apic_create(cpu, &local_err);
        if (local_err != NULL) {
            goto out;
        }
    }
#endif

    mce_init(cpu);
    qemu_init_vcpu(cs);

    x86_cpu_apic_realize(cpu, &local_err);
    if (local_err != NULL) {
        goto out;
    }
    cpu_reset(cs);

    xcc->parent_realize(dev, &local_err);
out:
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}

/* Enables contiguous-apic-ID mode, for compatibility */
static bool compat_apic_id_mode;

void enable_compat_apic_id_mode(void)
{
    compat_apic_id_mode = true;
}

/* Calculates initial APIC ID for a specific CPU index
 *
 * Currently we need to be able to calculate the APIC ID from the CPU index
 * alone (without requiring a CPU object), as the QEMU<->Seabios interfaces have
 * no concept of "CPU index", and the NUMA tables on fw_cfg need the APIC ID of
 * all CPUs up to max_cpus.
 */
uint32_t x86_cpu_apic_id_from_index(unsigned int cpu_index)
{
    uint32_t correct_id;
    static bool warned;

    correct_id = x86_apicid_from_cpu_idx(smp_cores, smp_threads, cpu_index);
    if (compat_apic_id_mode) {
        if (cpu_index != correct_id && !warned) {
            error_report("APIC IDs set in compatibility mode, "
                         "CPU topology won't match the configuration");
            warned = true;
        }
        return cpu_index;
    } else {
        return correct_id;
    }
}

static void x86_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    X86CPU *cpu = X86_CPU(obj);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(obj);
    CPUX86State *env = &cpu->env;
    static int inited;

    cs->env_ptr = env;
    cpu_exec_init(env);

    object_property_add(obj, "family", "int",
                        x86_cpuid_version_get_family,
                        x86_cpuid_version_set_family, NULL, NULL, NULL);
    object_property_add(obj, "model", "int",
                        x86_cpuid_version_get_model,
                        x86_cpuid_version_set_model, NULL, NULL, NULL);
    object_property_add(obj, "stepping", "int",
                        x86_cpuid_version_get_stepping,
                        x86_cpuid_version_set_stepping, NULL, NULL, NULL);
    object_property_add(obj, "level", "int",
                        x86_cpuid_get_level,
                        x86_cpuid_set_level, NULL, NULL, NULL);
    object_property_add(obj, "xlevel", "int",
                        x86_cpuid_get_xlevel,
                        x86_cpuid_set_xlevel, NULL, NULL, NULL);
    object_property_add_str(obj, "vendor",
                            x86_cpuid_get_vendor,
                            x86_cpuid_set_vendor, NULL);
    object_property_add_str(obj, "model-id",
                            x86_cpuid_get_model_id,
                            x86_cpuid_set_model_id, NULL);
    object_property_add(obj, "tsc-frequency", "int",
                        x86_cpuid_get_tsc_freq,
                        x86_cpuid_set_tsc_freq, NULL, NULL, NULL);
    object_property_add(obj, "apic-id", "int",
                        x86_cpuid_get_apic_id,
                        x86_cpuid_set_apic_id, NULL, NULL, NULL);
    object_property_add(obj, "feature-words", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)env->features, NULL);
    object_property_add(obj, "filtered-features", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)cpu->filtered_features, NULL);

    cpu->hyperv_spinlock_attempts = HYPERV_SPINLOCK_NEVER_RETRY;
    env->cpuid_apic_id = x86_cpu_apic_id_from_index(cs->cpu_index);

    x86_cpu_load_def(cpu, xcc->cpu_def, &error_abort);

    /* init various static tables used in TCG mode */
    if (tcg_enabled() && !inited) {
        inited = 1;
        optimize_flags_init();
#ifndef CONFIG_USER_ONLY
        cpu_set_debug_excp_handler(breakpoint_handler);
#endif
    }
}

static int64_t x86_cpu_get_arch_id(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    return env->cpuid_apic_id;
}

static bool x86_cpu_get_paging_enabled(const CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->env.cr[0] & CR0_PG_MASK;
}

static void x86_cpu_set_pc(CPUState *cs, vaddr value)
{
    X86CPU *cpu = X86_CPU(cs);

    cpu->env.eip = value;
}

static void x86_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    X86CPU *cpu = X86_CPU(cs);

    cpu->env.eip = tb->pc - tb->cs_base;
}

static bool x86_cpu_has_work(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    return ((cs->interrupt_request & (CPU_INTERRUPT_HARD |
                                      CPU_INTERRUPT_POLL)) &&
            (env->eflags & IF_MASK)) ||
           (cs->interrupt_request & (CPU_INTERRUPT_NMI |
                                     CPU_INTERRUPT_INIT |
                                     CPU_INTERRUPT_SIPI |
                                     CPU_INTERRUPT_MCE));
}

static Property x86_cpu_properties[] = {
    DEFINE_PROP_BOOL("pmu", X86CPU, enable_pmu, false),
    { .name  = "hv-spinlocks", .info  = &qdev_prop_spinlocks },
    DEFINE_PROP_BOOL("hv-relaxed", X86CPU, hyperv_relaxed_timing, false),
    DEFINE_PROP_BOOL("hv-vapic", X86CPU, hyperv_vapic, false),
    DEFINE_PROP_BOOL("hv-time", X86CPU, hyperv_time, false),
    DEFINE_PROP_BOOL("check", X86CPU, check_cpuid, false),
    DEFINE_PROP_BOOL("enforce", X86CPU, enforce_cpuid, false),
    DEFINE_PROP_BOOL("kvm", X86CPU, expose_kvm, true),
    DEFINE_PROP_END_OF_LIST()
};

static void x86_cpu_common_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    xcc->parent_realize = dc->realize;
    dc->realize = x86_cpu_realizefn;
    dc->bus_type = TYPE_ICC_BUS;
    dc->props = x86_cpu_properties;

    xcc->parent_reset = cc->reset;
    cc->reset = x86_cpu_reset;
    cc->reset_dump_flags = CPU_DUMP_FPU | CPU_DUMP_CCOP;

    cc->class_by_name = x86_cpu_class_by_name;
    cc->parse_features = x86_cpu_parse_featurestr;
    cc->has_work = x86_cpu_has_work;
    cc->do_interrupt = x86_cpu_do_interrupt;
    cc->dump_state = x86_cpu_dump_state;
    cc->set_pc = x86_cpu_set_pc;
    cc->synchronize_from_tb = x86_cpu_synchronize_from_tb;
    cc->gdb_read_register = x86_cpu_gdb_read_register;
    cc->gdb_write_register = x86_cpu_gdb_write_register;
    cc->get_arch_id = x86_cpu_get_arch_id;
    cc->get_paging_enabled = x86_cpu_get_paging_enabled;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = x86_cpu_handle_mmu_fault;
#else
    cc->get_memory_mapping = x86_cpu_get_memory_mapping;
    cc->get_phys_page_debug = x86_cpu_get_phys_page_debug;
    cc->write_elf64_note = x86_cpu_write_elf64_note;
    cc->write_elf64_qemunote = x86_cpu_write_elf64_qemunote;
    cc->write_elf32_note = x86_cpu_write_elf32_note;
    cc->write_elf32_qemunote = x86_cpu_write_elf32_qemunote;
    cc->vmsd = &vmstate_x86_cpu;
#endif
    cc->gdb_num_core_regs = CPU_NB_REGS * 2 + 25;
}

static const TypeInfo x86_cpu_type_info = {
    .name = TYPE_X86_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(X86CPU),
    .instance_init = x86_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(X86CPUClass),
    .class_init = x86_cpu_common_class_init,
};

static void x86_cpu_register_types(void)
{
    int i;

    type_register_static(&x86_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        x86_register_cpudef_type(&builtin_x86_defs[i]);
    }
#ifdef CONFIG_KVM
    type_register_static(&host_x86_cpu_type_info);
#endif
}

type_init(x86_cpu_register_types)
