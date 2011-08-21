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
#include "kvm.h"

#include "qemu-option.h"
#include "qemu-config.h"

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
    "pni|sse3" /* Intel,AMD sse3 */, "pclmuldq", "dtes64", "monitor",
    "ds_cpl", "vmx", "smx", "est",
    "tm2", "ssse3", "cid", NULL,
    "fma", "cx16", "xtpr", "pdcm",
    NULL, NULL, "dca", "sse4.1|sse4_1",
    "sse4.2|sse4_2", "x2apic", "movbe", "popcnt",
    NULL, "aes", "xsave", "osxsave",
    "avx", NULL, NULL, "hypervisor",
};
static const char *ext2_feature_name[] = {
    "fpu", "vme", "de", "pse",
    "tsc", "msr", "pae", "mce",
    "cx8" /* AMD CMPXCHG8B */, "apic", NULL, "syscall",
    "mtrr", "pge", "mca", "cmov",
    "pat", "pse36", NULL, NULL /* Linux mp */,
    "nx" /* Intel xd */, NULL, "mmxext", "mmx",
    "fxsr", "fxsr_opt" /* AMD ffxsr */, "pdpe1gb" /* AMD Page1GB */, "rdtscp",
    NULL, "lm" /* Intel 64 */, "3dnowext", "3dnow",
};
static const char *ext3_feature_name[] = {
    "lahf_lm" /* AMD LahfSahf */, "cmp_legacy", "svm", "extapic" /* AMD ExtApicSpace */,
    "cr8legacy" /* AMD AltMovCr8 */, "abm", "sse4a", "misalignsse",
    "3dnowprefetch", "osvw", "ibs", "xop",
    "skinit", "wdt", NULL, NULL,
    "fma4", NULL, "cvt16", "nodeid_msr",
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *kvm_feature_name[] = {
    "kvmclock", "kvm_nopiodelay", "kvm_mmu", "kvmclock", "kvm_asyncpf", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
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

/* collects per-function cpuid data
 */
typedef struct model_features_t {
    uint32_t *guest_feat;
    uint32_t *host_feat;
    uint32_t check_feat;
    const char **flag_names;
    uint32_t cpuid;
    } model_features_t;

int check_cpuid = 0;
int enforce_cpuid = 0;

void host_cpuid(uint32_t function, uint32_t count,
                uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
#if defined(CONFIG_KVM)
    uint32_t vec[4];

#ifdef __x86_64__
    asm volatile("cpuid"
                 : "=a"(vec[0]), "=b"(vec[1]),
                   "=c"(vec[2]), "=d"(vec[3])
                 : "0"(function), "c"(count) : "cc");
#else
    asm volatile("pusha \n\t"
                 "cpuid \n\t"
                 "mov %%eax, 0(%2) \n\t"
                 "mov %%ebx, 4(%2) \n\t"
                 "mov %%ecx, 8(%2) \n\t"
                 "mov %%edx, 12(%2) \n\t"
                 "popa"
                 : : "a"(function), "c"(count), "S"(vec)
                 : "memory", "cc");
#endif

    if (eax)
        *eax = vec[0];
    if (ebx)
        *ebx = vec[1];
    if (ecx)
        *ecx = vec[2];
    if (edx)
        *edx = vec[3];
#endif
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

static void add_flagname_to_bitmaps(const char *flagname, uint32_t *features,
                                    uint32_t *ext_features,
                                    uint32_t *ext2_features,
                                    uint32_t *ext3_features,
                                    uint32_t *kvm_features,
                                    uint32_t *svm_features)
{
    if (!lookup_feature(features, flagname, NULL, feature_name) &&
        !lookup_feature(ext_features, flagname, NULL, ext_feature_name) &&
        !lookup_feature(ext2_features, flagname, NULL, ext2_feature_name) &&
        !lookup_feature(ext3_features, flagname, NULL, ext3_feature_name) &&
        !lookup_feature(kvm_features, flagname, NULL, kvm_feature_name) &&
        !lookup_feature(svm_features, flagname, NULL, svm_feature_name))
            fprintf(stderr, "CPU feature %s not found\n", flagname);
}

typedef struct x86_def_t {
    struct x86_def_t *next;
    const char *name;
    uint32_t level;
    uint32_t vendor1, vendor2, vendor3;
    int family;
    int model;
    int stepping;
    int tsc_khz;
    uint32_t features, ext_features, ext2_features, ext3_features;
    uint32_t kvm_features, svm_features;
    uint32_t xlevel;
    char model_id[48];
    int vendor_override;
    uint32_t flags;
    /* Store the results of Centaur's CPUID instructions */
    uint32_t ext4_features;
    uint32_t xlevel2;
} x86_def_t;

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
#define EXT2_FEATURE_MASK 0x0183F3FF

#define TCG_FEATURES (CPUID_FP87 | CPUID_PSE | CPUID_TSC | CPUID_MSR | \
          CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_CLFLUSH | CPUID_ACPI | CPUID_MMX | \
          CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS)
          /* partly implemented:
          CPUID_MTRR, CPUID_MCA, CPUID_CLFLUSH (needed for Win64)
          CPUID_PSE36 (needed for Solaris) */
          /* missing:
          CPUID_VME, CPUID_DTS, CPUID_SS, CPUID_HT, CPUID_TM, CPUID_PBE */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | \
          CPUID_EXT_CX16 | CPUID_EXT_POPCNT | \
          CPUID_EXT_HYPERVISOR)
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_EST,
          CPUID_EXT_TM2, CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_XSAVE */
#define TCG_EXT2_FEATURES ((TCG_FEATURES & EXT2_FEATURE_MASK) | \
          CPUID_EXT2_NX | CPUID_EXT2_MMXEXT | CPUID_EXT2_RDTSCP | \
          CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT)
          /* missing:
          CPUID_EXT2_PDPE1GB */
#define TCG_EXT3_FEATURES (CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM | \
          CPUID_EXT3_CR8LEG | CPUID_EXT3_ABM | CPUID_EXT3_SSE4A)
#define TCG_SVM_FEATURES 0

/* maintains list of cpu model definitions
 */
static x86_def_t *x86_defs = {NULL};

/* built-in cpu model definitions (deprecated)
 */
static x86_def_t builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 4,
        .vendor1 = CPUID_VENDOR_AMD_1,
        .vendor2 = CPUID_VENDOR_AMD_2,
        .vendor3 = CPUID_VENDOR_AMD_3,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_CX16 | CPUID_EXT_POPCNT,
        .ext2_features = (PPRO_FEATURES & EXT2_FEATURE_MASK) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .xlevel = 0x8000000A,
        .model_id = "QEMU Virtual CPU version " QEMU_VERSION,
    },
    {
        .name = "phenom",
        .level = 5,
        .vendor1 = CPUID_VENDOR_AMD_1,
        .vendor2 = CPUID_VENDOR_AMD_2,
        .vendor3 = CPUID_VENDOR_AMD_3,
        .family = 16,
        .model = 2,
        .stepping = 3,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_HT,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .ext2_features = (PPRO_FEATURES & EXT2_FEATURE_MASK) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .ext3_features = CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .svm_features = CPUID_SVM_NPT | CPUID_SVM_LBRV,
        .xlevel = 0x8000001A,
        .model_id = "AMD Phenom(tm) 9550 Quad-Core Processor"
    },
    {
        .name = "core2duo",
        .level = 10,
        .family = 6,
        .model = 15,
        .stepping = 11,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_DTS | CPUID_ACPI | CPUID_SS |
            CPUID_HT | CPUID_TM | CPUID_PBE,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DTES64 | CPUID_EXT_DSCPL | CPUID_EXT_VMX | CPUID_EXT_EST |
            CPUID_EXT_TM2 | CPUID_EXT_CX16 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Core(TM)2 Duo CPU     T7700  @ 2.40GHz",
    },
    {
        .name = "kvm64",
        .level = 5,
        .vendor1 = CPUID_VENDOR_INTEL_1,
        .vendor2 = CPUID_VENDOR_INTEL_2,
        .vendor3 = CPUID_VENDOR_INTEL_3,
        .family = 15,
        .model = 6,
        .stepping = 1,
        /* Missing: CPUID_VME, CPUID_HT */
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .ext2_features = (PPRO_FEATURES & EXT2_FEATURE_MASK) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .ext3_features = 0,
        .xlevel = 0x80000008,
        .model_id = "Common KVM processor"
    },
    {
        .name = "qemu32",
        .level = 4,
        .family = 6,
        .model = 3,
        .stepping = 3,
        .features = PPRO_FEATURES,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_POPCNT,
        .xlevel = 0x80000004,
        .model_id = "QEMU Virtual CPU version " QEMU_VERSION,
    },
    {
        .name = "kvm32",
        .level = 5,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .ext_features = CPUID_EXT_SSE3,
        .ext2_features = PPRO_FEATURES & EXT2_FEATURE_MASK,
        .ext3_features = 0,
        .xlevel = 0x80000008,
        .model_id = "Common 32-bit KVM processor"
    },
    {
        .name = "coreduo",
        .level = 10,
        .family = 6,
        .model = 14,
        .stepping = 8,
        .features = PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_DTS | CPUID_ACPI |
            CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_VMX |
            CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .ext2_features = CPUID_EXT2_NX,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 1,
        .family = 4,
        .model = 0,
        .stepping = 0,
        .features = I486_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium",
        .level = 1,
        .family = 5,
        .model = 4,
        .stepping = 3,
        .features = PENTIUM_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium2",
        .level = 2,
        .family = 6,
        .model = 5,
        .stepping = 2,
        .features = PENTIUM2_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium3",
        .level = 2,
        .family = 6,
        .model = 7,
        .stepping = 3,
        .features = PENTIUM3_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "athlon",
        .level = 2,
        .vendor1 = CPUID_VENDOR_AMD_1,
        .vendor2 = CPUID_VENDOR_AMD_2,
        .vendor3 = CPUID_VENDOR_AMD_3,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = PPRO_FEATURES | CPUID_PSE36 | CPUID_VME | CPUID_MTRR | CPUID_MCA,
        .ext2_features = (PPRO_FEATURES & EXT2_FEATURE_MASK) | CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
        .xlevel = 0x80000008,
        /* XXX: put another string ? */
        .model_id = "QEMU Virtual CPU version " QEMU_VERSION,
    },
    {
        .name = "n270",
        /* original is on level 10 */
        .level = 5,
        .family = 6,
        .model = 28,
        .stepping = 2,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME | CPUID_DTS |
            CPUID_ACPI | CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
            /* Some CPUs got no CPUID_SEP */
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DSCPL | CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR,
        .ext2_features = (PPRO_FEATURES & EXT2_FEATURE_MASK) | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
};

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

static int cpu_x86_fill_host(x86_def_t *x86_cpu_def)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    x86_cpu_def->name = "host";
    host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_def->level = eax;
    x86_cpu_def->vendor1 = ebx;
    x86_cpu_def->vendor2 = edx;
    x86_cpu_def->vendor3 = ecx;

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_def->family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
    x86_cpu_def->model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
    x86_cpu_def->stepping = eax & 0x0F;
    x86_cpu_def->ext_features = ecx;
    x86_cpu_def->features = edx;

    host_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_def->xlevel = eax;

    host_cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_def->ext2_features = edx;
    x86_cpu_def->ext3_features = ecx;
    cpu_x86_fill_model_id(x86_cpu_def->model_id);
    x86_cpu_def->vendor_override = 0;

    /* Call Centaur's CPUID instruction. */
    if (x86_cpu_def->vendor1 == CPUID_VENDOR_VIA_1 &&
        x86_cpu_def->vendor2 == CPUID_VENDOR_VIA_2 &&
        x86_cpu_def->vendor3 == CPUID_VENDOR_VIA_3) {
        host_cpuid(0xC0000000, 0, &eax, &ebx, &ecx, &edx);
        if (eax >= 0xC0000001) {
            /* Support VIA max extended level */
            x86_cpu_def->xlevel2 = eax;
            host_cpuid(0xC0000001, 0, &eax, &ebx, &ecx, &edx);
            x86_cpu_def->ext4_features = edx;
        }
    }

    /*
     * Every SVM feature requires emulation support in KVM - so we can't just
     * read the host features here. KVM might even support SVM features not
     * available on the host hardware. Just set all bits and mask out the
     * unsupported ones later.
     */
    x86_cpu_def->svm_features = -1;

    return 0;
}

static int unavailable_host_feature(struct model_features_t *f, uint32_t mask)
{
    int i;

    for (i = 0; i < 32; ++i)
        if (1 << i & mask) {
            fprintf(stderr, "warning: host cpuid %04x_%04x lacks requested"
                " flag '%s' [0x%08x]\n",
                f->cpuid >> 16, f->cpuid & 0xffff,
                f->flag_names[i] ? f->flag_names[i] : "[reserved]", mask);
            break;
        }
    return 0;
}

/* best effort attempt to inform user requested cpu flags aren't making
 * their way to the guest.  Note: ft[].check_feat ideally should be
 * specified via a guest_def field to suppress report of extraneous flags.
 */
static int check_features_against_host(x86_def_t *guest_def)
{
    x86_def_t host_def;
    uint32_t mask;
    int rv, i;
    struct model_features_t ft[] = {
        {&guest_def->features, &host_def.features,
            ~0, feature_name, 0x00000000},
        {&guest_def->ext_features, &host_def.ext_features,
            ~CPUID_EXT_HYPERVISOR, ext_feature_name, 0x00000001},
        {&guest_def->ext2_features, &host_def.ext2_features,
            ~PPRO_FEATURES, ext2_feature_name, 0x80000000},
        {&guest_def->ext3_features, &host_def.ext3_features,
            ~CPUID_EXT3_SVM, ext3_feature_name, 0x80000001}};

    cpu_x86_fill_host(&host_def);
    for (rv = 0, i = 0; i < ARRAY_SIZE(ft); ++i)
        for (mask = 1; mask; mask <<= 1)
            if (ft[i].check_feat & mask && *ft[i].guest_feat & mask &&
                !(*ft[i].host_feat & mask)) {
                    unavailable_host_feature(&ft[i], mask);
                    rv = 1;
                }
    return rv;
}

static int cpu_x86_find_by_name(x86_def_t *x86_cpu_def, const char *cpu_model)
{
    unsigned int i;
    x86_def_t *def;

    char *s = strdup(cpu_model);
    char *featurestr, *name = strtok(s, ",");
    /* Features to be added*/
    uint32_t plus_features = 0, plus_ext_features = 0;
    uint32_t plus_ext2_features = 0, plus_ext3_features = 0;
    uint32_t plus_kvm_features = 0, plus_svm_features = 0;
    /* Features to be removed */
    uint32_t minus_features = 0, minus_ext_features = 0;
    uint32_t minus_ext2_features = 0, minus_ext3_features = 0;
    uint32_t minus_kvm_features = 0, minus_svm_features = 0;
    uint32_t numvalue;

    for (def = x86_defs; def; def = def->next)
        if (!strcmp(name, def->name))
            break;
    if (kvm_enabled() && strcmp(name, "host") == 0) {
        cpu_x86_fill_host(x86_cpu_def);
    } else if (!def) {
        goto error;
    } else {
        memcpy(x86_cpu_def, def, sizeof(*def));
    }

    plus_kvm_features = ~0; /* not supported bits will be filtered out later */

    add_flagname_to_bitmaps("hypervisor", &plus_features,
        &plus_ext_features, &plus_ext2_features, &plus_ext3_features,
        &plus_kvm_features, &plus_svm_features);

    featurestr = strtok(NULL, ",");

    while (featurestr) {
        char *val;
        if (featurestr[0] == '+') {
            add_flagname_to_bitmaps(featurestr + 1, &plus_features,
                            &plus_ext_features, &plus_ext2_features,
                            &plus_ext3_features, &plus_kvm_features,
                            &plus_svm_features);
        } else if (featurestr[0] == '-') {
            add_flagname_to_bitmaps(featurestr + 1, &minus_features,
                            &minus_ext_features, &minus_ext2_features,
                            &minus_ext3_features, &minus_kvm_features,
                            &minus_svm_features);
        } else if ((val = strchr(featurestr, '='))) {
            *val = 0; val++;
            if (!strcmp(featurestr, "family")) {
                char *err;
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                x86_cpu_def->family = numvalue;
            } else if (!strcmp(featurestr, "model")) {
                char *err;
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err || numvalue > 0xff) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                x86_cpu_def->model = numvalue;
            } else if (!strcmp(featurestr, "stepping")) {
                char *err;
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err || numvalue > 0xf) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                x86_cpu_def->stepping = numvalue ;
            } else if (!strcmp(featurestr, "level")) {
                char *err;
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                x86_cpu_def->level = numvalue;
            } else if (!strcmp(featurestr, "xlevel")) {
                char *err;
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                if (numvalue < 0x80000000) {
                    numvalue += 0x80000000;
                }
                x86_cpu_def->xlevel = numvalue;
            } else if (!strcmp(featurestr, "vendor")) {
                if (strlen(val) != 12) {
                    fprintf(stderr, "vendor string must be 12 chars long\n");
                    goto error;
                }
                x86_cpu_def->vendor1 = 0;
                x86_cpu_def->vendor2 = 0;
                x86_cpu_def->vendor3 = 0;
                for(i = 0; i < 4; i++) {
                    x86_cpu_def->vendor1 |= ((uint8_t)val[i    ]) << (8 * i);
                    x86_cpu_def->vendor2 |= ((uint8_t)val[i + 4]) << (8 * i);
                    x86_cpu_def->vendor3 |= ((uint8_t)val[i + 8]) << (8 * i);
                }
                x86_cpu_def->vendor_override = 1;
            } else if (!strcmp(featurestr, "model_id")) {
                pstrcpy(x86_cpu_def->model_id, sizeof(x86_cpu_def->model_id),
                        val);
            } else if (!strcmp(featurestr, "tsc_freq")) {
                int64_t tsc_freq;
                char *err;

                tsc_freq = strtosz_suffix_unit(val, &err,
                                               STRTOSZ_DEFSUFFIX_B, 1000);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                x86_cpu_def->tsc_khz = tsc_freq / 1000;
            } else {
                fprintf(stderr, "unrecognized feature %s\n", featurestr);
                goto error;
            }
        } else if (!strcmp(featurestr, "check")) {
            check_cpuid = 1;
        } else if (!strcmp(featurestr, "enforce")) {
            check_cpuid = enforce_cpuid = 1;
        } else {
            fprintf(stderr, "feature string `%s' not in format (+feature|-feature|feature=xyz)\n", featurestr);
            goto error;
        }
        featurestr = strtok(NULL, ",");
    }
    x86_cpu_def->features |= plus_features;
    x86_cpu_def->ext_features |= plus_ext_features;
    x86_cpu_def->ext2_features |= plus_ext2_features;
    x86_cpu_def->ext3_features |= plus_ext3_features;
    x86_cpu_def->kvm_features |= plus_kvm_features;
    x86_cpu_def->svm_features |= plus_svm_features;
    x86_cpu_def->features &= ~minus_features;
    x86_cpu_def->ext_features &= ~minus_ext_features;
    x86_cpu_def->ext2_features &= ~minus_ext2_features;
    x86_cpu_def->ext3_features &= ~minus_ext3_features;
    x86_cpu_def->kvm_features &= ~minus_kvm_features;
    x86_cpu_def->svm_features &= ~minus_svm_features;
    if (check_cpuid) {
        if (check_features_against_host(x86_cpu_def) && enforce_cpuid)
            goto error;
    }
    free(s);
    return 0;

error:
    free(s);
    return -1;
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

/* generate CPU information:
 * -?        list model names
 * -?model   list model names/IDs
 * -?dump    output all model (x86_def_t) data
 * -?cpuid   list all recognized cpuid flag names
 */
void x86_cpu_list(FILE *f, fprintf_function cpu_fprintf, const char *optarg)
{
    unsigned char model = !strcmp("?model", optarg);
    unsigned char dump = !strcmp("?dump", optarg);
    unsigned char cpuid = !strcmp("?cpuid", optarg);
    x86_def_t *def;
    char buf[256];

    if (cpuid) {
        (*cpu_fprintf)(f, "Recognized CPUID flags:\n");
        listflags(buf, sizeof (buf), (uint32_t)~0, feature_name, 1);
        (*cpu_fprintf)(f, "  f_edx: %s\n", buf);
        listflags(buf, sizeof (buf), (uint32_t)~0, ext_feature_name, 1);
        (*cpu_fprintf)(f, "  f_ecx: %s\n", buf);
        listflags(buf, sizeof (buf), (uint32_t)~0, ext2_feature_name, 1);
        (*cpu_fprintf)(f, "  extf_edx: %s\n", buf);
        listflags(buf, sizeof (buf), (uint32_t)~0, ext3_feature_name, 1);
        (*cpu_fprintf)(f, "  extf_ecx: %s\n", buf);
        return;
    }
    for (def = x86_defs; def; def = def->next) {
        snprintf(buf, sizeof (buf), def->flags ? "[%s]": "%s", def->name);
        if (model || dump) {
            (*cpu_fprintf)(f, "x86 %16s  %-48s\n", buf, def->model_id);
        } else {
            (*cpu_fprintf)(f, "x86 %16s\n", buf);
        }
        if (dump) {
            memcpy(buf, &def->vendor1, sizeof (def->vendor1));
            memcpy(buf + 4, &def->vendor2, sizeof (def->vendor2));
            memcpy(buf + 8, &def->vendor3, sizeof (def->vendor3));
            buf[12] = '\0';
            (*cpu_fprintf)(f,
                "  family %d model %d stepping %d level %d xlevel 0x%x"
                " vendor \"%s\"\n",
                def->family, def->model, def->stepping, def->level,
                def->xlevel, buf);
            listflags(buf, sizeof (buf), def->features, feature_name, 0);
            (*cpu_fprintf)(f, "  feature_edx %08x (%s)\n", def->features,
                buf);
            listflags(buf, sizeof (buf), def->ext_features, ext_feature_name,
                0);
            (*cpu_fprintf)(f, "  feature_ecx %08x (%s)\n", def->ext_features,
                buf);
            listflags(buf, sizeof (buf), def->ext2_features, ext2_feature_name,
                0);
            (*cpu_fprintf)(f, "  extfeature_edx %08x (%s)\n",
                def->ext2_features, buf);
            listflags(buf, sizeof (buf), def->ext3_features, ext3_feature_name,
                0);
            (*cpu_fprintf)(f, "  extfeature_ecx %08x (%s)\n",
                def->ext3_features, buf);
            (*cpu_fprintf)(f, "\n");
        }
    }
    if (kvm_enabled()) {
        (*cpu_fprintf)(f, "x86 %16s\n", "[host]");
    }
}

int cpu_x86_register (CPUX86State *env, const char *cpu_model)
{
    x86_def_t def1, *def = &def1;

    memset(def, 0, sizeof(*def));

    if (cpu_x86_find_by_name(def, cpu_model) < 0)
        return -1;
    if (def->vendor1) {
        env->cpuid_vendor1 = def->vendor1;
        env->cpuid_vendor2 = def->vendor2;
        env->cpuid_vendor3 = def->vendor3;
    } else {
        env->cpuid_vendor1 = CPUID_VENDOR_INTEL_1;
        env->cpuid_vendor2 = CPUID_VENDOR_INTEL_2;
        env->cpuid_vendor3 = CPUID_VENDOR_INTEL_3;
    }
    env->cpuid_vendor_override = def->vendor_override;
    env->cpuid_level = def->level;
    if (def->family > 0x0f)
        env->cpuid_version = 0xf00 | ((def->family - 0x0f) << 20);
    else
        env->cpuid_version = def->family << 8;
    env->cpuid_version |= ((def->model & 0xf) << 4) | ((def->model >> 4) << 16);
    env->cpuid_version |= def->stepping;
    env->cpuid_features = def->features;
    env->cpuid_ext_features = def->ext_features;
    env->cpuid_ext2_features = def->ext2_features;
    env->cpuid_ext3_features = def->ext3_features;
    env->cpuid_xlevel = def->xlevel;
    env->cpuid_kvm_features = def->kvm_features;
    env->cpuid_svm_features = def->svm_features;
    env->cpuid_ext4_features = def->ext4_features;
    env->cpuid_xlevel2 = def->xlevel2;
    env->tsc_khz = def->tsc_khz;
    if (!kvm_enabled()) {
        env->cpuid_features &= TCG_FEATURES;
        env->cpuid_ext_features &= TCG_EXT_FEATURES;
        env->cpuid_ext2_features &= (TCG_EXT2_FEATURES
#ifdef TARGET_X86_64
            | CPUID_EXT2_SYSCALL | CPUID_EXT2_LM
#endif
            );
        env->cpuid_ext3_features &= TCG_EXT3_FEATURES;
        env->cpuid_svm_features &= TCG_SVM_FEATURES;
    }
    {
        const char *model_id = def->model_id;
        int c, len, i;
        if (!model_id)
            model_id = "";
        len = strlen(model_id);
        for(i = 0; i < 48; i++) {
            if (i >= len)
                c = '\0';
            else
                c = (uint8_t)model_id[i];
            env->cpuid_model[i >> 2] |= c << (8 * (i & 3));
        }
    }
    return 0;
}

#if !defined(CONFIG_USER_ONLY)
/* copy vendor id string to 32 bit register, nul pad as needed
 */
static void cpyid(const char *s, uint32_t *id)
{
    char *d = (char *)id;
    char i;

    for (i = sizeof (*id); i--; )
        *d++ = *s ? *s++ : '\0';
}

/* interpret radix and convert from string to arbitrary scalar,
 * otherwise flag failure
 */
#define setscalar(pval, str, perr)                      \
{                                                       \
    char *pend;                                         \
    unsigned long ul;                                   \
                                                        \
    ul = strtoul(str, &pend, 0);                        \
    *str && !*pend ? (*pval = ul) : (*perr = 1);        \
}

/* map cpuid options to feature bits, otherwise return failure
 * (option tags in *str are delimited by whitespace)
 */
static void setfeatures(uint32_t *pval, const char *str,
    const char **featureset, int *perr)
{
    const char *p, *q;

    for (q = p = str; *p || *q; q = p) {
        while (iswhite(*p))
            q = ++p;
        while (*p && !iswhite(*p))
            ++p;
        if (!*q && !*p)
            return;
        if (!lookup_feature(pval, q, p, featureset)) {
            fprintf(stderr, "error: feature \"%.*s\" not available in set\n",
                (int)(p - q), q);
            *perr = 1;
            return;
        }
    }
}

/* map config file options to x86_def_t form
 */
static int cpudef_setfield(const char *name, const char *str, void *opaque)
{
    x86_def_t *def = opaque;
    int err = 0;

    if (!strcmp(name, "name")) {
        def->name = strdup(str);
    } else if (!strcmp(name, "model_id")) {
        strncpy(def->model_id, str, sizeof (def->model_id));
    } else if (!strcmp(name, "level")) {
        setscalar(&def->level, str, &err)
    } else if (!strcmp(name, "vendor")) {
        cpyid(&str[0], &def->vendor1);
        cpyid(&str[4], &def->vendor2);
        cpyid(&str[8], &def->vendor3);
    } else if (!strcmp(name, "family")) {
        setscalar(&def->family, str, &err)
    } else if (!strcmp(name, "model")) {
        setscalar(&def->model, str, &err)
    } else if (!strcmp(name, "stepping")) {
        setscalar(&def->stepping, str, &err)
    } else if (!strcmp(name, "feature_edx")) {
        setfeatures(&def->features, str, feature_name, &err);
    } else if (!strcmp(name, "feature_ecx")) {
        setfeatures(&def->ext_features, str, ext_feature_name, &err);
    } else if (!strcmp(name, "extfeature_edx")) {
        setfeatures(&def->ext2_features, str, ext2_feature_name, &err);
    } else if (!strcmp(name, "extfeature_ecx")) {
        setfeatures(&def->ext3_features, str, ext3_feature_name, &err);
    } else if (!strcmp(name, "xlevel")) {
        setscalar(&def->xlevel, str, &err)
    } else {
        fprintf(stderr, "error: unknown option [%s = %s]\n", name, str);
        return (1);
    }
    if (err) {
        fprintf(stderr, "error: bad option value [%s = %s]\n", name, str);
        return (1);
    }
    return (0);
}

/* register config file entry as x86_def_t
 */
static int cpudef_register(QemuOpts *opts, void *opaque)
{
    x86_def_t *def = g_malloc0(sizeof (x86_def_t));

    qemu_opt_foreach(opts, cpudef_setfield, def, 1);
    def->next = x86_defs;
    x86_defs = def;
    return (0);
}

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->cpuid_features &= ~CPUID_APIC;
}

#endif /* !CONFIG_USER_ONLY */

/* register "cpudef" models defined in configuration file.  Here we first
 * preload any built-in definitions
 */
void x86_cpudef_setup(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); ++i) {
        builtin_x86_defs[i].next = x86_defs;
        builtin_x86_defs[i].flags = 1;
        x86_defs = &builtin_x86_defs[i];
    }
#if !defined(CONFIG_USER_ONLY)
    qemu_opts_foreach(qemu_find_opts("cpudef"), cpudef_register, NULL, 0);
#endif
}

static void get_cpuid_vendor(CPUX86State *env, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx)
{
    *ebx = env->cpuid_vendor1;
    *edx = env->cpuid_vendor2;
    *ecx = env->cpuid_vendor3;

    /* sysenter isn't supported on compatibility mode on AMD, syscall
     * isn't supported in compatibility mode on Intel.
     * Normally we advertise the actual cpu vendor, but you can override
     * this if you want to use KVM's sysenter/syscall emulation
     * in compatibility mode and when doing cross vendor migration
     */
    if (kvm_enabled() && ! env->cpuid_vendor_override) {
        host_cpuid(0, 0, NULL, ebx, ecx, edx);
    }
}

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
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
                index =  env->cpuid_xlevel;
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
        *ecx = env->cpuid_ext_features;
        *edx = env->cpuid_features;
        if (env->nr_cores * env->nr_threads > 1) {
            *ebx |= (env->nr_cores * env->nr_threads) << 16;
            *edx |= 1 << 28;    /* HTT bit */
        }
        break;
    case 2:
        /* cache info: needed for Pentium Pro compatibility */
        *eax = 1;
        *ebx = 0;
        *ecx = 0;
        *edx = 0x2c307d;
        break;
    case 4:
        /* cache info: needed for Core compatibility */
        if (env->nr_cores > 1) {
            *eax = (env->nr_cores - 1) << 26;
        } else {
            *eax = 0;
        }
        switch (count) {
            case 0: /* L1 dcache info */
                *eax |= 0x0000121;
                *ebx = 0x1c0003f;
                *ecx = 0x000003f;
                *edx = 0x0000001;
                break;
            case 1: /* L1 icache info */
                *eax |= 0x0000122;
                *ebx = 0x1c0003f;
                *ecx = 0x000003f;
                *edx = 0x0000001;
                break;
            case 2: /* L2 cache info */
                *eax |= 0x0000143;
                if (env->nr_threads > 1) {
                    *eax |= (env->nr_threads - 1) << 14;
                }
                *ebx = 0x3c0003f;
                *ecx = 0x0000fff;
                *edx = 0x0000001;
                break;
            default: /* end of info */
                *eax = 0;
                *ebx = 0;
                *ecx = 0;
                *edx = 0;
                break;
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
        if (kvm_enabled()) {
            KVMState *s = env->kvm_state;

            *eax = kvm_arch_get_supported_cpuid(s, 0x7, count, R_EAX);
            *ebx = kvm_arch_get_supported_cpuid(s, 0x7, count, R_EBX);
            *ecx = kvm_arch_get_supported_cpuid(s, 0x7, count, R_ECX);
            *edx = kvm_arch_get_supported_cpuid(s, 0x7, count, R_EDX);
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
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xD:
        /* Processor Extended State */
        if (!(env->cpuid_ext_features & CPUID_EXT_XSAVE)) {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
            break;
        }
        if (kvm_enabled()) {
            KVMState *s = env->kvm_state;

            *eax = kvm_arch_get_supported_cpuid(s, 0xd, count, R_EAX);
            *ebx = kvm_arch_get_supported_cpuid(s, 0xd, count, R_EBX);
            *ecx = kvm_arch_get_supported_cpuid(s, 0xd, count, R_ECX);
            *edx = kvm_arch_get_supported_cpuid(s, 0xd, count, R_EDX);
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0x80000000:
        *eax = env->cpuid_xlevel;
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 0x80000001:
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = env->cpuid_ext3_features;
        *edx = env->cpuid_ext2_features;

        /* The Linux kernel checks for the CMPLegacy bit and
         * discards multiple thread information if it is set.
         * So dont set it here for Intel to make Linux guests happy.
         */
        if (env->nr_cores * env->nr_threads > 1) {
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
        *eax = 0x01ff01ff;
        *ebx = 0x01ff01ff;
        *ecx = 0x40020140;
        *edx = 0x40020140;
        break;
    case 0x80000006:
        /* cache info (L2 cache) */
        *eax = 0;
        *ebx = 0x42004200;
        *ecx = 0x02008140;
        *edx = 0;
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
/* XXX: This value must match the one used in the MMU code. */
        if (env->cpuid_ext2_features & CPUID_EXT2_LM) {
            /* 64 bit processor */
/* XXX: The physical address space is limited to 42 bits in exec.c. */
            *eax = 0x00003028;	/* 48 bits virtual, 40 bits physical */
        } else {
            if (env->cpuid_features & CPUID_PSE36)
                *eax = 0x00000024; /* 36 bits physical */
            else
                *eax = 0x00000020; /* 32 bits physical */
        }
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (env->nr_cores * env->nr_threads > 1) {
            *ecx |= (env->nr_cores * env->nr_threads) - 1;
        }
        break;
    case 0x8000000A:
	if (env->cpuid_ext3_features & CPUID_EXT3_SVM) {
		*eax = 0x00000001; /* SVM Revision */
		*ebx = 0x00000010; /* nr of ASIDs */
		*ecx = 0;
		*edx = env->cpuid_svm_features; /* optional features */
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
        *edx = env->cpuid_ext4_features;
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
