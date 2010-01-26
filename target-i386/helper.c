/*
 *  i386 helpers (without register variable usage)
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>

#include "cpu.h"
#include "exec-all.h"
#include "qemu-common.h"
#include "kvm.h"

//#define DEBUG_MMU
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
    "pni|sse3" /* Intel,AMD sse3 */, NULL, NULL, "monitor",
    "ds_cpl", "vmx", NULL /* Linux smx */, "est",
    "tm2", "ssse3", "cid", NULL,
    NULL, "cx16", "xtpr", NULL,
    NULL, NULL, "dca", "sse4.1|sse4_1",
    "sse4.2|sse4_2", "x2apic", NULL, "popcnt",
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, "hypervisor",
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
    "3dnowprefetch", "osvw", NULL /* Linux ibs */, NULL,
    "skinit", "wdt", NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *kvm_feature_name[] = {
    "kvmclock", "kvm_nopiodelay", "kvm_mmu", NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
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

static void host_cpuid(uint32_t function, uint32_t count, uint32_t *eax,
                       uint32_t *ebx, uint32_t *ecx, uint32_t *edx);

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
 * *pval and return success, otherwise return zero
 */
static int lookup_feature(uint32_t *pval, const char *s, const char *e,
    const char **featureset)
{
    uint32_t mask;
    const char **ppc;

    for (mask = 1, ppc = featureset; mask; mask <<= 1, ++ppc)
        if (*ppc && !altcmp(s, e, *ppc)) {
            *pval |= mask;
            break;
        }
    return (mask ? 1 : 0);
}

static void add_flagname_to_bitmaps(const char *flagname, uint32_t *features,
                                    uint32_t *ext_features,
                                    uint32_t *ext2_features,
                                    uint32_t *ext3_features,
                                    uint32_t *kvm_features)
{
    if (!lookup_feature(features, flagname, NULL, feature_name) &&
        !lookup_feature(ext_features, flagname, NULL, ext_feature_name) &&
        !lookup_feature(ext2_features, flagname, NULL, ext2_feature_name) &&
        !lookup_feature(ext3_features, flagname, NULL, ext3_feature_name) &&
        !lookup_feature(kvm_features, flagname, NULL, kvm_feature_name))
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
    uint32_t features, ext_features, ext2_features, ext3_features, kvm_features;
    uint32_t xlevel;
    char model_id[48];
    int vendor_override;
    uint32_t flags;
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

/* maintains list of cpu model definitions
 */
static x86_def_t *x86_defs = {NULL};

/* built-in cpu model definitions (deprecated)
 */
static x86_def_t builtin_x86_defs[] = {
#ifdef TARGET_X86_64
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
        /* these features are needed for Win64 and aren't fully implemented */
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
        /* this feature is needed for Solaris and isn't fully implemented */
            CPUID_PSE36,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_CX16 | CPUID_EXT_POPCNT,
        .ext2_features = (PPRO_FEATURES & 0x0183F3FF) | 
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
        /* Missing: CPUID_VME, CPUID_HT */
        .features = PPRO_FEATURES | 
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .ext2_features = (PPRO_FEATURES & 0x0183F3FF) | 
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .ext3_features = CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .xlevel = 0x8000001A,
        .model_id = "AMD Phenom(tm) 9550 Quad-Core Processor"
    },
    {
        .name = "core2duo",
        .level = 10,
        .family = 6,
        .model = 15,
        .stepping = 11,
	/* The original CPU also implements these features:
               CPUID_VME, CPUID_DTS, CPUID_ACPI, CPUID_SS, CPUID_HT,
               CPUID_TM, CPUID_PBE */
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
	/* The original CPU also implements these ext features:
               CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_EST,
               CPUID_EXT_TM2, CPUID_EXT_CX16, CPUID_EXT_XTPR, CPUID_EXT_PDCM */
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3,
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
        .ext2_features = (PPRO_FEATURES & 0x0183F3FF) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .ext3_features = 0,
        .xlevel = 0x80000008,
        .model_id = "Common KVM processor"
    },
#endif
    {
        .name = "qemu32",
        .level = 4,
        .family = 6,
        .model = 3,
        .stepping = 3,
        .features = PPRO_FEATURES,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_POPCNT,
        .xlevel = 0,
        .model_id = "QEMU Virtual CPU version " QEMU_VERSION,
    },
    {
        .name = "coreduo",
        .level = 10,
        .family = 6,
        .model = 14,
        .stepping = 8,
        /* The original CPU also implements these features:
               CPUID_DTS, CPUID_ACPI, CPUID_SS, CPUID_HT,
               CPUID_TM, CPUID_PBE */
        .features = PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA,
        /* The original CPU also implements these ext features:
               CPUID_EXT_VMX, CPUID_EXT_EST, CPUID_EXT_TM2, CPUID_EXT_XTPR,
               CPUID_EXT_PDCM */
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR,
        .ext2_features = CPUID_EXT2_NX,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 0,
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
        .ext2_features = (PPRO_FEATURES & 0x0183F3FF) | CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
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
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME,
            /* Missing: CPUID_DTS | CPUID_ACPI | CPUID_SS |
             * CPUID_HT | CPUID_TM | CPUID_PBE */
            /* Some CPUs got no CPUID_SEP */
        .ext_features = CPUID_EXT_MONITOR |
            CPUID_EXT_SSE3 /* PNI */ | CPUID_EXT_SSSE3,
            /* Missing: CPUID_EXT_DSCPL | CPUID_EXT_EST |
             * CPUID_EXT_TM2 | CPUID_EXT_XTPR */
        .ext2_features = (PPRO_FEATURES & 0x0183F3FF) | CPUID_EXT2_NX,
        /* Missing: .ext3_features = CPUID_EXT3_LAHF_LM */
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
    for (rv = 0, i = 0; i < sizeof (ft) / sizeof (ft[0]); ++i)
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
    uint32_t plus_features = 0, plus_ext_features = 0, plus_ext2_features = 0, plus_ext3_features = 0, plus_kvm_features = 0;
    uint32_t minus_features = 0, minus_ext_features = 0, minus_ext2_features = 0, minus_ext3_features = 0, minus_kvm_features = 0;
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
        &plus_kvm_features);

    featurestr = strtok(NULL, ",");

    while (featurestr) {
        char *val;
        if (featurestr[0] == '+') {
            add_flagname_to_bitmaps(featurestr + 1, &plus_features, &plus_ext_features, &plus_ext2_features, &plus_ext3_features, &plus_kvm_features);
        } else if (featurestr[0] == '-') {
            add_flagname_to_bitmaps(featurestr + 1, &minus_features, &minus_ext_features, &minus_ext2_features, &minus_ext3_features, &minus_kvm_features);
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
    x86_cpu_def->features &= ~minus_features;
    x86_cpu_def->ext_features &= ~minus_ext_features;
    x86_cpu_def->ext2_features &= ~minus_ext2_features;
    x86_cpu_def->ext3_features &= ~minus_ext3_features;
    x86_cpu_def->kvm_features &= ~minus_kvm_features;
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
void x86_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                  const char *optarg)
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
}

static int cpu_x86_register (CPUX86State *env, const char *cpu_model)
{
    x86_def_t def1, *def = &def1;

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
    env->pat = 0x0007040600070406ULL;
    env->cpuid_ext_features = def->ext_features;
    env->cpuid_ext2_features = def->ext2_features;
    env->cpuid_xlevel = def->xlevel;
    env->cpuid_kvm_features = def->kvm_features;
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
    x86_def_t *def = qemu_mallocz(sizeof (x86_def_t));

    qemu_opt_foreach(opts, cpudef_setfield, def, 1);
    def->next = x86_defs;
    x86_defs = def;
    return (0);
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
    qemu_opts_foreach(&qemu_cpudef_opts, cpudef_register, NULL, 0);
#endif
}

/* NOTE: must be called outside the CPU execute loop */
void cpu_reset(CPUX86State *env)
{
    int i;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, X86_DUMP_FPU | X86_DUMP_CCOP);
    }

    memset(env, 0, offsetof(CPUX86State, breakpoints));

    tlb_flush(env, 1);

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
    for(i = 0;i < 8; i++)
        env->fptags[i] = 1;
    env->fpuc = 0x37f;

    env->mxcsr = 0x1f80;

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(env, BP_CPU);
    cpu_watchpoint_remove_all(env, BP_CPU);

    env->mcg_status = 0;
}

void cpu_x86_close(CPUX86State *env)
{
    qemu_free(env);
}

/***********************************************************/
/* x86 debug */

static const char *cc_op_str[] = {
    "DYNAMIC",
    "EFLAGS",

    "MULB",
    "MULW",
    "MULL",
    "MULQ",

    "ADDB",
    "ADDW",
    "ADDL",
    "ADDQ",

    "ADCB",
    "ADCW",
    "ADCL",
    "ADCQ",

    "SUBB",
    "SUBW",
    "SUBL",
    "SUBQ",

    "SBBB",
    "SBBW",
    "SBBL",
    "SBBQ",

    "LOGICB",
    "LOGICW",
    "LOGICL",
    "LOGICQ",

    "INCB",
    "INCW",
    "INCL",
    "INCQ",

    "DECB",
    "DECW",
    "DECL",
    "DECQ",

    "SHLB",
    "SHLW",
    "SHLL",
    "SHLQ",

    "SARB",
    "SARW",
    "SARL",
    "SARQ",
};

static void
cpu_x86_dump_seg_cache(CPUState *env, FILE *f,
                       int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                       const char *name, struct SegmentCache *sc)
{
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        cpu_fprintf(f, "%-3s=%04x %016" PRIx64 " %08x %08x", name,
                    sc->selector, sc->base, sc->limit, sc->flags);
    } else
#endif
    {
        cpu_fprintf(f, "%-3s=%04x %08x %08x %08x", name, sc->selector,
                    (uint32_t)sc->base, sc->limit, sc->flags);
    }

    if (!(env->hflags & HF_PE_MASK) || !(sc->flags & DESC_P_MASK))
        goto done;

    cpu_fprintf(f, " DPL=%d ", (sc->flags & DESC_DPL_MASK) >> DESC_DPL_SHIFT);
    if (sc->flags & DESC_S_MASK) {
        if (sc->flags & DESC_CS_MASK) {
            cpu_fprintf(f, (sc->flags & DESC_L_MASK) ? "CS64" :
                           ((sc->flags & DESC_B_MASK) ? "CS32" : "CS16"));
            cpu_fprintf(f, " [%c%c", (sc->flags & DESC_C_MASK) ? 'C' : '-',
                        (sc->flags & DESC_R_MASK) ? 'R' : '-');
        } else {
            cpu_fprintf(f, (sc->flags & DESC_B_MASK) ? "DS  " : "DS16");
            cpu_fprintf(f, " [%c%c", (sc->flags & DESC_E_MASK) ? 'E' : '-',
                        (sc->flags & DESC_W_MASK) ? 'W' : '-');
        }
        cpu_fprintf(f, "%c]", (sc->flags & DESC_A_MASK) ? 'A' : '-');
    } else {
        static const char *sys_type_name[2][16] = {
            { /* 32 bit mode */
                "Reserved", "TSS16-avl", "LDT", "TSS16-busy",
                "CallGate16", "TaskGate", "IntGate16", "TrapGate16",
                "Reserved", "TSS32-avl", "Reserved", "TSS32-busy",
                "CallGate32", "Reserved", "IntGate32", "TrapGate32"
            },
            { /* 64 bit mode */
                "<hiword>", "Reserved", "LDT", "Reserved", "Reserved",
                "Reserved", "Reserved", "Reserved", "Reserved",
                "TSS64-avl", "Reserved", "TSS64-busy", "CallGate64",
                "Reserved", "IntGate64", "TrapGate64"
            }
        };
        cpu_fprintf(f, sys_type_name[(env->hflags & HF_LMA_MASK) ? 1 : 0]
                                    [(sc->flags & DESC_TYPE_MASK)
                                     >> DESC_TYPE_SHIFT]);
    }
done:
    cpu_fprintf(f, "\n");
}

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int eflags, i, nb;
    char cc_op_name[32];
    static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS", "GS" };

    cpu_synchronize_state(env);

    eflags = env->eflags;
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        cpu_fprintf(f,
                    "RAX=%016" PRIx64 " RBX=%016" PRIx64 " RCX=%016" PRIx64 " RDX=%016" PRIx64 "\n"
                    "RSI=%016" PRIx64 " RDI=%016" PRIx64 " RBP=%016" PRIx64 " RSP=%016" PRIx64 "\n"
                    "R8 =%016" PRIx64 " R9 =%016" PRIx64 " R10=%016" PRIx64 " R11=%016" PRIx64 "\n"
                    "R12=%016" PRIx64 " R13=%016" PRIx64 " R14=%016" PRIx64 " R15=%016" PRIx64 "\n"
                    "RIP=%016" PRIx64 " RFL=%08x [%c%c%c%c%c%c%c] CPL=%d II=%d A20=%d SMM=%d HLT=%d\n",
                    env->regs[R_EAX],
                    env->regs[R_EBX],
                    env->regs[R_ECX],
                    env->regs[R_EDX],
                    env->regs[R_ESI],
                    env->regs[R_EDI],
                    env->regs[R_EBP],
                    env->regs[R_ESP],
                    env->regs[8],
                    env->regs[9],
                    env->regs[10],
                    env->regs[11],
                    env->regs[12],
                    env->regs[13],
                    env->regs[14],
                    env->regs[15],
                    env->eip, eflags,
                    eflags & DF_MASK ? 'D' : '-',
                    eflags & CC_O ? 'O' : '-',
                    eflags & CC_S ? 'S' : '-',
                    eflags & CC_Z ? 'Z' : '-',
                    eflags & CC_A ? 'A' : '-',
                    eflags & CC_P ? 'P' : '-',
                    eflags & CC_C ? 'C' : '-',
                    env->hflags & HF_CPL_MASK,
                    (env->hflags >> HF_INHIBIT_IRQ_SHIFT) & 1,
                    (env->a20_mask >> 20) & 1,
                    (env->hflags >> HF_SMM_SHIFT) & 1,
                    env->halted);
    } else
#endif
    {
        cpu_fprintf(f, "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
                    "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
                    "EIP=%08x EFL=%08x [%c%c%c%c%c%c%c] CPL=%d II=%d A20=%d SMM=%d HLT=%d\n",
                    (uint32_t)env->regs[R_EAX],
                    (uint32_t)env->regs[R_EBX],
                    (uint32_t)env->regs[R_ECX],
                    (uint32_t)env->regs[R_EDX],
                    (uint32_t)env->regs[R_ESI],
                    (uint32_t)env->regs[R_EDI],
                    (uint32_t)env->regs[R_EBP],
                    (uint32_t)env->regs[R_ESP],
                    (uint32_t)env->eip, eflags,
                    eflags & DF_MASK ? 'D' : '-',
                    eflags & CC_O ? 'O' : '-',
                    eflags & CC_S ? 'S' : '-',
                    eflags & CC_Z ? 'Z' : '-',
                    eflags & CC_A ? 'A' : '-',
                    eflags & CC_P ? 'P' : '-',
                    eflags & CC_C ? 'C' : '-',
                    env->hflags & HF_CPL_MASK,
                    (env->hflags >> HF_INHIBIT_IRQ_SHIFT) & 1,
                    (env->a20_mask >> 20) & 1,
                    (env->hflags >> HF_SMM_SHIFT) & 1,
                    env->halted);
    }

    for(i = 0; i < 6; i++) {
        cpu_x86_dump_seg_cache(env, f, cpu_fprintf, seg_name[i],
                               &env->segs[i]);
    }
    cpu_x86_dump_seg_cache(env, f, cpu_fprintf, "LDT", &env->ldt);
    cpu_x86_dump_seg_cache(env, f, cpu_fprintf, "TR", &env->tr);

#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        cpu_fprintf(f, "GDT=     %016" PRIx64 " %08x\n",
                    env->gdt.base, env->gdt.limit);
        cpu_fprintf(f, "IDT=     %016" PRIx64 " %08x\n",
                    env->idt.base, env->idt.limit);
        cpu_fprintf(f, "CR0=%08x CR2=%016" PRIx64 " CR3=%016" PRIx64 " CR4=%08x\n",
                    (uint32_t)env->cr[0],
                    env->cr[2],
                    env->cr[3],
                    (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++)
            cpu_fprintf(f, "DR%d=%016" PRIx64 " ", i, env->dr[i]);
        cpu_fprintf(f, "\nDR6=%016" PRIx64 " DR7=%016" PRIx64 "\n",
                    env->dr[6], env->dr[7]);
    } else
#endif
    {
        cpu_fprintf(f, "GDT=     %08x %08x\n",
                    (uint32_t)env->gdt.base, env->gdt.limit);
        cpu_fprintf(f, "IDT=     %08x %08x\n",
                    (uint32_t)env->idt.base, env->idt.limit);
        cpu_fprintf(f, "CR0=%08x CR2=%08x CR3=%08x CR4=%08x\n",
                    (uint32_t)env->cr[0],
                    (uint32_t)env->cr[2],
                    (uint32_t)env->cr[3],
                    (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++)
            cpu_fprintf(f, "DR%d=%08x ", i, env->dr[i]);
        cpu_fprintf(f, "\nDR6=%08x DR7=%08x\n", env->dr[6], env->dr[7]);
    }
    if (flags & X86_DUMP_CCOP) {
        if ((unsigned)env->cc_op < CC_OP_NB)
            snprintf(cc_op_name, sizeof(cc_op_name), "%s", cc_op_str[env->cc_op]);
        else
            snprintf(cc_op_name, sizeof(cc_op_name), "[%d]", env->cc_op);
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            cpu_fprintf(f, "CCS=%016" PRIx64 " CCD=%016" PRIx64 " CCO=%-8s\n",
                        env->cc_src, env->cc_dst,
                        cc_op_name);
        } else
#endif
        {
            cpu_fprintf(f, "CCS=%08x CCD=%08x CCO=%-8s\n",
                        (uint32_t)env->cc_src, (uint32_t)env->cc_dst,
                        cc_op_name);
        }
    }
    if (flags & X86_DUMP_FPU) {
        int fptag;
        fptag = 0;
        for(i = 0; i < 8; i++) {
            fptag |= ((!env->fptags[i]) << i);
        }
        cpu_fprintf(f, "FCW=%04x FSW=%04x [ST=%d] FTW=%02x MXCSR=%08x\n",
                    env->fpuc,
                    (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11,
                    env->fpstt,
                    fptag,
                    env->mxcsr);
        for(i=0;i<8;i++) {
#if defined(USE_X86LDOUBLE)
            union {
                long double d;
                struct {
                    uint64_t lower;
                    uint16_t upper;
                } l;
            } tmp;
            tmp.d = env->fpregs[i].d;
            cpu_fprintf(f, "FPR%d=%016" PRIx64 " %04x",
                        i, tmp.l.lower, tmp.l.upper);
#else
            cpu_fprintf(f, "FPR%d=%016" PRIx64,
                        i, env->fpregs[i].mmx.q);
#endif
            if ((i & 1) == 1)
                cpu_fprintf(f, "\n");
            else
                cpu_fprintf(f, " ");
        }
        if (env->hflags & HF_CS64_MASK)
            nb = 16;
        else
            nb = 8;
        for(i=0;i<nb;i++) {
            cpu_fprintf(f, "XMM%02d=%08x%08x%08x%08x",
                        i,
                        env->xmm_regs[i].XMM_L(3),
                        env->xmm_regs[i].XMM_L(2),
                        env->xmm_regs[i].XMM_L(1),
                        env->xmm_regs[i].XMM_L(0));
            if ((i & 1) == 1)
                cpu_fprintf(f, "\n");
            else
                cpu_fprintf(f, " ");
        }
    }
}

/***********************************************************/
/* x86 mmu */
/* XXX: add PGE support */

void cpu_x86_set_a20(CPUX86State *env, int a20_state)
{
    a20_state = (a20_state != 0);
    if (a20_state != ((env->a20_mask >> 20) & 1)) {
#if defined(DEBUG_MMU)
        printf("A20 update: a20=%d\n", a20_state);
#endif
        /* if the cpu is currently executing code, we must unlink it and
           all the potentially executing TB */
        cpu_interrupt(env, CPU_INTERRUPT_EXITTB);

        /* when a20 is changed, all the MMU mappings are invalid, so
           we must flush everything */
        tlb_flush(env, 1);
        env->a20_mask = ~(1 << 20) | (a20_state << 20);
    }
}

void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0)
{
    int pe_state;

#if defined(DEBUG_MMU)
    printf("CR0 update: CR0=0x%08x\n", new_cr0);
#endif
    if ((new_cr0 & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK)) !=
        (env->cr[0] & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK))) {
        tlb_flush(env, 1);
    }

#ifdef TARGET_X86_64
    if (!(env->cr[0] & CR0_PG_MASK) && (new_cr0 & CR0_PG_MASK) &&
        (env->efer & MSR_EFER_LME)) {
        /* enter in long mode */
        /* XXX: generate an exception */
        if (!(env->cr[4] & CR4_PAE_MASK))
            return;
        env->efer |= MSR_EFER_LMA;
        env->hflags |= HF_LMA_MASK;
    } else if ((env->cr[0] & CR0_PG_MASK) && !(new_cr0 & CR0_PG_MASK) &&
               (env->efer & MSR_EFER_LMA)) {
        /* exit long mode */
        env->efer &= ~MSR_EFER_LMA;
        env->hflags &= ~(HF_LMA_MASK | HF_CS64_MASK);
        env->eip &= 0xffffffff;
    }
#endif
    env->cr[0] = new_cr0 | CR0_ET_MASK;

    /* update PE flag in hidden flags */
    pe_state = (env->cr[0] & CR0_PE_MASK);
    env->hflags = (env->hflags & ~HF_PE_MASK) | (pe_state << HF_PE_SHIFT);
    /* ensure that ADDSEG is always set in real mode */
    env->hflags |= ((pe_state ^ 1) << HF_ADDSEG_SHIFT);
    /* update FPU flags */
    env->hflags = (env->hflags & ~(HF_MP_MASK | HF_EM_MASK | HF_TS_MASK)) |
        ((new_cr0 << (HF_MP_SHIFT - 1)) & (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK));
}

/* XXX: in legacy PAE mode, generate a GPF if reserved bits are set in
   the PDPT */
void cpu_x86_update_cr3(CPUX86State *env, target_ulong new_cr3)
{
    env->cr[3] = new_cr3;
    if (env->cr[0] & CR0_PG_MASK) {
#if defined(DEBUG_MMU)
        printf("CR3 update: CR3=" TARGET_FMT_lx "\n", new_cr3);
#endif
        tlb_flush(env, 0);
    }
}

void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4)
{
#if defined(DEBUG_MMU)
    printf("CR4 update: CR4=%08x\n", (uint32_t)env->cr[4]);
#endif
    if ((new_cr4 & (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK)) !=
        (env->cr[4] & (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK))) {
        tlb_flush(env, 1);
    }
    /* SSE handling */
    if (!(env->cpuid_features & CPUID_SSE))
        new_cr4 &= ~CR4_OSFXSR_MASK;
    if (new_cr4 & CR4_OSFXSR_MASK)
        env->hflags |= HF_OSFXSR_MASK;
    else
        env->hflags &= ~HF_OSFXSR_MASK;

    env->cr[4] = new_cr4;
}

#if defined(CONFIG_USER_ONLY)

int cpu_x86_handle_mmu_fault(CPUX86State *env, target_ulong addr,
                             int is_write, int mmu_idx, int is_softmmu)
{
    /* user mode only emulation */
    is_write &= 1;
    env->cr[2] = addr;
    env->error_code = (is_write << PG_ERROR_W_BIT);
    env->error_code |= PG_ERROR_U_MASK;
    env->exception_index = EXCP0E_PAGE;
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

#else

/* XXX: This value should match the one returned by CPUID
 * and in exec.c */
# if defined(TARGET_X86_64)
# define PHYS_ADDR_MASK 0xfffffff000LL
# else
# define PHYS_ADDR_MASK 0xffffff000LL
# endif

/* return value:
   -1 = cannot handle fault
   0  = nothing more to do
   1  = generate PF fault
   2  = soft MMU activation required for this block
*/
int cpu_x86_handle_mmu_fault(CPUX86State *env, target_ulong addr,
                             int is_write1, int mmu_idx, int is_softmmu)
{
    uint64_t ptep, pte;
    target_ulong pde_addr, pte_addr;
    int error_code, is_dirty, prot, page_size, ret, is_write, is_user;
    target_phys_addr_t paddr;
    uint32_t page_offset;
    target_ulong vaddr, virt_addr;

    is_user = mmu_idx == MMU_USER_IDX;
#if defined(DEBUG_MMU)
    printf("MMU fault: addr=" TARGET_FMT_lx " w=%d u=%d eip=" TARGET_FMT_lx "\n",
           addr, is_write1, is_user, env->eip);
#endif
    is_write = is_write1 & 1;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
        virt_addr = addr & TARGET_PAGE_MASK;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = 4096;
        goto do_mapping;
    }

    if (env->cr[4] & CR4_PAE_MASK) {
        uint64_t pde, pdpe;
        target_ulong pdpe_addr;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                env->error_code = 0;
                env->exception_index = EXCP0D_GPF;
                return 1;
            }

            pml4e_addr = ((env->cr[3] & ~0xfff) + (((addr >> 39) & 0x1ff) << 3)) &
                env->a20_mask;
            pml4e = ldq_phys(pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            if (!(env->efer & MSR_EFER_NXE) && (pml4e & PG_NX_MASK)) {
                error_code = PG_ERROR_RSVD_MASK;
                goto do_fault;
            }
            if (!(pml4e & PG_ACCESSED_MASK)) {
                pml4e |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pml4e_addr, pml4e);
            }
            ptep = pml4e ^ PG_NX_MASK;
            pdpe_addr = ((pml4e & PHYS_ADDR_MASK) + (((addr >> 30) & 0x1ff) << 3)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            if (!(env->efer & MSR_EFER_NXE) && (pdpe & PG_NX_MASK)) {
                error_code = PG_ERROR_RSVD_MASK;
                goto do_fault;
            }
            ptep &= pdpe ^ PG_NX_MASK;
            if (!(pdpe & PG_ACCESSED_MASK)) {
                pdpe |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pdpe_addr, pdpe);
            }
        } else
#endif
        {
            /* XXX: load them when cr3 is loaded ? */
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        pde_addr = ((pdpe & PHYS_ADDR_MASK) + (((addr >> 21) & 0x1ff) << 3)) &
            env->a20_mask;
        pde = ldq_phys(pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            error_code = 0;
            goto do_fault;
        }
        if (!(env->efer & MSR_EFER_NXE) && (pde & PG_NX_MASK)) {
            error_code = PG_ERROR_RSVD_MASK;
            goto do_fault;
        }
        ptep &= pde ^ PG_NX_MASK;
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            ptep ^= PG_NX_MASK;
            if ((ptep & PG_NX_MASK) && is_write1 == 2)
                goto do_fault_protect;
            if (is_user) {
                if (!(ptep & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pde & PG_DIRTY_MASK);
            if (!(pde & PG_ACCESSED_MASK) || is_dirty) {
                pde |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pde |= PG_DIRTY_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }
            /* align to page_size */
            pte = pde & ((PHYS_ADDR_MASK & ~(page_size - 1)) | 0xfff);
            virt_addr = addr & ~(page_size - 1);
        } else {
            /* 4 KB page */
            if (!(pde & PG_ACCESSED_MASK)) {
                pde |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }
            pte_addr = ((pde & PHYS_ADDR_MASK) + (((addr >> 12) & 0x1ff) << 3)) &
                env->a20_mask;
            pte = ldq_phys(pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            if (!(env->efer & MSR_EFER_NXE) && (pte & PG_NX_MASK)) {
                error_code = PG_ERROR_RSVD_MASK;
                goto do_fault;
            }
            /* combine pde and pte nx, user and rw protections */
            ptep &= pte ^ PG_NX_MASK;
            ptep ^= PG_NX_MASK;
            if ((ptep & PG_NX_MASK) && is_write1 == 2)
                goto do_fault_protect;
            if (is_user) {
                if (!(ptep & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pte & PG_DIRTY_MASK);
            if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
                pte |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pte |= PG_DIRTY_MASK;
                stl_phys_notdirty(pte_addr, pte);
            }
            page_size = 4096;
            virt_addr = addr & ~0xfff;
            pte = pte & (PHYS_ADDR_MASK | 0xfff);
        }
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) &
            env->a20_mask;
        pde = ldl_phys(pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            error_code = 0;
            goto do_fault;
        }
        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            page_size = 4096 * 1024;
            if (is_user) {
                if (!(pde & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(pde & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(pde & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pde & PG_DIRTY_MASK);
            if (!(pde & PG_ACCESSED_MASK) || is_dirty) {
                pde |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pde |= PG_DIRTY_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }

            pte = pde & ~( (page_size - 1) & ~0xfff); /* align to page_size */
            ptep = pte;
            virt_addr = addr & ~(page_size - 1);
        } else {
            if (!(pde & PG_ACCESSED_MASK)) {
                pde |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }

            /* page directory entry */
            pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) &
                env->a20_mask;
            pte = ldl_phys(pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            /* combine pde and pte user and rw protections */
            ptep = pte & pde;
            if (is_user) {
                if (!(ptep & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pte & PG_DIRTY_MASK);
            if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
                pte |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pte |= PG_DIRTY_MASK;
                stl_phys_notdirty(pte_addr, pte);
            }
            page_size = 4096;
            virt_addr = addr & ~0xfff;
        }
    }
    /* the page can be put in the TLB */
    prot = PAGE_READ;
    if (!(ptep & PG_NX_MASK))
        prot |= PAGE_EXEC;
    if (pte & PG_DIRTY_MASK) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        if (is_user) {
            if (ptep & PG_RW_MASK)
                prot |= PAGE_WRITE;
        } else {
            if (!(env->cr[0] & CR0_WP_MASK) ||
                (ptep & PG_RW_MASK))
                prot |= PAGE_WRITE;
        }
    }
 do_mapping:
    pte = pte & env->a20_mask;

    /* Even if 4MB pages, we map only one 4KB page in the cache to
       avoid filling it too fast */
    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    paddr = (pte & TARGET_PAGE_MASK) + page_offset;
    vaddr = virt_addr + page_offset;

    ret = tlb_set_page_exec(env, vaddr, paddr, prot, mmu_idx, is_softmmu);
    return ret;
 do_fault_protect:
    error_code = PG_ERROR_P_MASK;
 do_fault:
    error_code |= (is_write << PG_ERROR_W_BIT);
    if (is_user)
        error_code |= PG_ERROR_U_MASK;
    if (is_write1 == 2 &&
        (env->efer & MSR_EFER_NXE) &&
        (env->cr[4] & CR4_PAE_MASK))
        error_code |= PG_ERROR_I_D_MASK;
    if (env->intercept_exceptions & (1 << EXCP0E_PAGE)) {
        /* cr2 is not modified in case of exceptions */
        stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), 
                 addr);
    } else {
        env->cr[2] = addr;
    }
    env->error_code = error_code;
    env->exception_index = EXCP0E_PAGE;
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    target_ulong pde_addr, pte_addr;
    uint64_t pte;
    target_phys_addr_t paddr;
    uint32_t page_offset;
    int page_size;

    if (env->cr[4] & CR4_PAE_MASK) {
        target_ulong pdpe_addr;
        uint64_t pde, pdpe;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = (int64_t)addr >> 47;
            if (sext != 0 && sext != -1)
                return -1;

            pml4e_addr = ((env->cr[3] & ~0xfff) + (((addr >> 39) & 0x1ff) << 3)) &
                env->a20_mask;
            pml4e = ldq_phys(pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK))
                return -1;

            pdpe_addr = ((pml4e & ~0xfff) + (((addr >> 30) & 0x1ff) << 3)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        } else
#endif
        {
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        }

        pde_addr = ((pdpe & ~0xfff) + (((addr >> 21) & 0x1ff) << 3)) &
            env->a20_mask;
        pde = ldq_phys(pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            return -1;
        }
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte = pde & ~( (page_size - 1) & ~0xfff); /* align to page_size */
        } else {
            /* 4 KB page */
            pte_addr = ((pde & ~0xfff) + (((addr >> 12) & 0x1ff) << 3)) &
                env->a20_mask;
            page_size = 4096;
            pte = ldq_phys(pte_addr);
        }
        if (!(pte & PG_PRESENT_MASK))
            return -1;
    } else {
        uint32_t pde;

        if (!(env->cr[0] & CR0_PG_MASK)) {
            pte = addr;
            page_size = 4096;
        } else {
            /* page directory entry */
            pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) & env->a20_mask;
            pde = ldl_phys(pde_addr);
            if (!(pde & PG_PRESENT_MASK))
                return -1;
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                pte = pde & ~0x003ff000; /* align to 4MB */
                page_size = 4096 * 1024;
            } else {
                /* page directory entry */
                pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & env->a20_mask;
                pte = ldl_phys(pte_addr);
                if (!(pte & PG_PRESENT_MASK))
                    return -1;
                page_size = 4096;
            }
        }
        pte = pte & env->a20_mask;
    }

    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    paddr = (pte & TARGET_PAGE_MASK) + page_offset;
    return paddr;
}

void hw_breakpoint_insert(CPUState *env, int index)
{
    int type, err = 0;

    switch (hw_breakpoint_type(env->dr[7], index)) {
    case 0:
        if (hw_breakpoint_enabled(env->dr[7], index))
            err = cpu_breakpoint_insert(env, env->dr[index], BP_CPU,
                                        &env->cpu_breakpoint[index]);
        break;
    case 1:
        type = BP_CPU | BP_MEM_WRITE;
        goto insert_wp;
    case 2:
         /* No support for I/O watchpoints yet */
        break;
    case 3:
        type = BP_CPU | BP_MEM_ACCESS;
    insert_wp:
        err = cpu_watchpoint_insert(env, env->dr[index],
                                    hw_breakpoint_len(env->dr[7], index),
                                    type, &env->cpu_watchpoint[index]);
        break;
    }
    if (err)
        env->cpu_breakpoint[index] = NULL;
}

void hw_breakpoint_remove(CPUState *env, int index)
{
    if (!env->cpu_breakpoint[index])
        return;
    switch (hw_breakpoint_type(env->dr[7], index)) {
    case 0:
        if (hw_breakpoint_enabled(env->dr[7], index))
            cpu_breakpoint_remove_by_ref(env, env->cpu_breakpoint[index]);
        break;
    case 1:
    case 3:
        cpu_watchpoint_remove_by_ref(env, env->cpu_watchpoint[index]);
        break;
    case 2:
        /* No support for I/O watchpoints yet */
        break;
    }
}

int check_hw_breakpoints(CPUState *env, int force_dr6_update)
{
    target_ulong dr6;
    int reg, type;
    int hit_enabled = 0;

    dr6 = env->dr[6] & ~0xf;
    for (reg = 0; reg < 4; reg++) {
        type = hw_breakpoint_type(env->dr[7], reg);
        if ((type == 0 && env->dr[reg] == env->eip) ||
            ((type & 1) && env->cpu_watchpoint[reg] &&
             (env->cpu_watchpoint[reg]->flags & BP_WATCHPOINT_HIT))) {
            dr6 |= 1 << reg;
            if (hw_breakpoint_enabled(env->dr[7], reg))
                hit_enabled = 1;
        }
    }
    if (hit_enabled || force_dr6_update)
        env->dr[6] = dr6;
    return hit_enabled;
}

static CPUDebugExcpHandler *prev_debug_excp_handler;

void raise_exception_env(int exception_index, CPUState *env);

static void breakpoint_handler(CPUState *env)
{
    CPUBreakpoint *bp;

    if (env->watchpoint_hit) {
        if (env->watchpoint_hit->flags & BP_CPU) {
            env->watchpoint_hit = NULL;
            if (check_hw_breakpoints(env, 0))
                raise_exception_env(EXCP01_DB, env);
            else
                cpu_resume_from_signal(env, NULL);
        }
    } else {
        QTAILQ_FOREACH(bp, &env->breakpoints, entry)
            if (bp->pc == env->eip) {
                if (bp->flags & BP_CPU) {
                    check_hw_breakpoints(env, 1);
                    raise_exception_env(EXCP01_DB, env);
                }
                break;
            }
    }
    if (prev_debug_excp_handler)
        prev_debug_excp_handler(env);
}

/* This should come from sysemu.h - if we could include it here... */
void qemu_system_reset_request(void);

void cpu_inject_x86_mce(CPUState *cenv, int bank, uint64_t status,
                        uint64_t mcg_status, uint64_t addr, uint64_t misc)
{
    uint64_t mcg_cap = cenv->mcg_cap;
    unsigned bank_num = mcg_cap & 0xff;
    uint64_t *banks = cenv->mce_banks;

    if (bank >= bank_num || !(status & MCI_STATUS_VAL))
        return;

    /*
     * if MSR_MCG_CTL is not all 1s, the uncorrected error
     * reporting is disabled
     */
    if ((status & MCI_STATUS_UC) && (mcg_cap & MCG_CTL_P) &&
        cenv->mcg_ctl != ~(uint64_t)0)
        return;
    banks += 4 * bank;
    /*
     * if MSR_MCi_CTL is not all 1s, the uncorrected error
     * reporting is disabled for the bank
     */
    if ((status & MCI_STATUS_UC) && banks[0] != ~(uint64_t)0)
        return;
    if (status & MCI_STATUS_UC) {
        if ((cenv->mcg_status & MCG_STATUS_MCIP) ||
            !(cenv->cr[4] & CR4_MCE_MASK)) {
            fprintf(stderr, "injects mce exception while previous "
                    "one is in progress!\n");
            qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");
            qemu_system_reset_request();
            return;
        }
        if (banks[1] & MCI_STATUS_VAL)
            status |= MCI_STATUS_OVER;
        banks[2] = addr;
        banks[3] = misc;
        cenv->mcg_status = mcg_status;
        banks[1] = status;
        cpu_interrupt(cenv, CPU_INTERRUPT_MCE);
    } else if (!(banks[1] & MCI_STATUS_VAL)
               || !(banks[1] & MCI_STATUS_UC)) {
        if (banks[1] & MCI_STATUS_VAL)
            status |= MCI_STATUS_OVER;
        banks[2] = addr;
        banks[3] = misc;
        banks[1] = status;
    } else
        banks[1] |= MCI_STATUS_OVER;
}
#endif /* !CONFIG_USER_ONLY */

static void mce_init(CPUX86State *cenv)
{
    unsigned int bank, bank_num;

    if (((cenv->cpuid_version >> 8)&0xf) >= 6
        && (cenv->cpuid_features&(CPUID_MCE|CPUID_MCA)) == (CPUID_MCE|CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF;
        cenv->mcg_ctl = ~(uint64_t)0;
        bank_num = MCE_BANKS_DEF;
        for (bank = 0; bank < bank_num; bank++)
            cenv->mce_banks[bank*4] = ~(uint64_t)0;
    }
}

static void host_cpuid(uint32_t function, uint32_t count,
                       uint32_t *eax, uint32_t *ebx,
                       uint32_t *ecx, uint32_t *edx)
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
    if (kvm_enabled() && env->cpuid_vendor_override) {
        host_cpuid(0, 0, NULL, ebx, ecx, edx);
    }
}

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    /* test if maximum index reached */
    if (index & 0x80000000) {
        if (index > env->cpuid_xlevel)
            index = env->cpuid_level;
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

        if (kvm_enabled()) {
            /* Nested SVM not yet supported in upstream QEMU */
            *ecx &= ~CPUID_EXT3_SVM;
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
        *eax = 0x00000001; /* SVM Revision */
        *ebx = 0x00000010; /* nr of ASIDs */
        *ecx = 0;
        *edx = 0; /* optional features */
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


int cpu_x86_get_descr_debug(CPUX86State *env, unsigned int selector,
                            target_ulong *base, unsigned int *limit,
                            unsigned int *flags)
{
    SegmentCache *dt;
    target_ulong ptr;
    uint32_t e1, e2;
    int index;

    if (selector & 0x4)
        dt = &env->ldt;
    else
        dt = &env->gdt;
    index = selector & ~7;
    ptr = dt->base + index;
    if ((index + 7) > dt->limit
        || cpu_memory_rw_debug(env, ptr, (uint8_t *)&e1, sizeof(e1), 0) != 0
        || cpu_memory_rw_debug(env, ptr+4, (uint8_t *)&e2, sizeof(e2), 0) != 0)
        return 0;

    *base = ((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
    *limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK)
        *limit = (*limit << 12) | 0xfff;
    *flags = e2;

    return 1;
}

CPUX86State *cpu_x86_init(const char *cpu_model)
{
    CPUX86State *env;
    static int inited;

    env = qemu_mallocz(sizeof(CPUX86State));
    cpu_exec_init(env);
    env->cpu_model_str = cpu_model;

    /* init various static tables */
    if (!inited) {
        inited = 1;
        optimize_flags_init();
#ifndef CONFIG_USER_ONLY
        prev_debug_excp_handler =
            cpu_set_debug_excp_handler(breakpoint_handler);
#endif
    }
    if (cpu_x86_register(env, cpu_model) < 0) {
        cpu_x86_close(env);
        return NULL;
    }
    mce_init(env);

    qemu_init_vcpu(env);

    return env;
}

#if !defined(CONFIG_USER_ONLY)
void do_cpu_init(CPUState *env)
{
    int sipi = env->interrupt_request & CPU_INTERRUPT_SIPI;
    cpu_reset(env);
    env->interrupt_request = sipi;
    apic_init_reset(env);
}

void do_cpu_sipi(CPUState *env)
{
    apic_sipi(env);
}
#else
void do_cpu_init(CPUState *env)
{
}
void do_cpu_sipi(CPUState *env)
{
}
#endif
