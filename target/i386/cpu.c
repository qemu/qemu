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
#include "qemu/osdep.h"
#include "qemu/cutils.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "sysemu/kvm.h"
#include "sysemu/cpus.h"
#include "kvm_i386.h"

#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qfloat.h"

#include "qapi-types.h"
#include "qapi-visit.h"
#include "qapi/visitor.h"
#include "qom/qom-qobject.h"
#include "sysemu/arch_init.h"

#if defined(CONFIG_KVM)
#include <linux/kvm_para.h>
#endif

#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/i386/topology.h"
#ifndef CONFIG_USER_ONLY
#include "exec/address-spaces.h"
#include "hw/hw.h"
#include "hw/xen/xen.h"
#include "hw/i386/apic_internal.h"
#endif


/* Cache topology CPUID constants: */

/* CPUID Leaf 2 Descriptors */

#define CPUID_2_L1D_32KB_8WAY_64B 0x2c
#define CPUID_2_L1I_32KB_8WAY_64B 0x30
#define CPUID_2_L2_2MB_8WAY_64B   0x7d
#define CPUID_2_L3_16MB_16WAY_64B 0x4d


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

/* Level 3 unified cache: */
#define L3_SIZE_KB             0 /* disabled */
#define L3_ASSOCIATIVITY       0 /* disabled */
#define L3_LINES_PER_TAG       0 /* disabled */
#define L3_LINE_SIZE           0 /* disabled */
#define L3_N_LINE_SIZE         64
#define L3_N_ASSOCIATIVITY     16
#define L3_N_SETS           16384
#define L3_N_PARTITIONS         1
#define L3_N_DESCRIPTOR CPUID_2_L3_16MB_16WAY_64B
#define L3_N_LINES_PER_TAG      1
#define L3_N_SIZE_KB_AMD    16384

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
          CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS | CPUID_DE)
          /* partly implemented:
          CPUID_MTRR, CPUID_MCA, CPUID_CLFLUSH (needed for Win64) */
          /* missing:
          CPUID_VME, CPUID_DTS, CPUID_SS, CPUID_HT, CPUID_TM, CPUID_PBE */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | \
          CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | \
          CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_POPCNT | \
          CPUID_EXT_XSAVE | /* CPUID_EXT_OSXSAVE is dynamic */   \
          CPUID_EXT_MOVBE | CPUID_EXT_AES | CPUID_EXT_HYPERVISOR)
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_SMX,
          CPUID_EXT_EST, CPUID_EXT_TM2, CPUID_EXT_CID, CPUID_EXT_FMA,
          CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_PCID, CPUID_EXT_DCA,
          CPUID_EXT_X2APIC, CPUID_EXT_TSC_DEADLINE_TIMER, CPUID_EXT_AVX,
          CPUID_EXT_F16C, CPUID_EXT_RDRAND */

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
          CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ADX | \
          CPUID_7_0_EBX_PCOMMIT | CPUID_7_0_EBX_CLFLUSHOPT |            \
          CPUID_7_0_EBX_CLWB | CPUID_7_0_EBX_MPX | CPUID_7_0_EBX_FSGSBASE | \
          CPUID_7_0_EBX_ERMS)
          /* missing:
          CPUID_7_0_EBX_HLE, CPUID_7_0_EBX_AVX2,
          CPUID_7_0_EBX_INVPCID, CPUID_7_0_EBX_RTM,
          CPUID_7_0_EBX_RDSEED */
#define TCG_7_0_ECX_FEATURES (CPUID_7_0_ECX_PKU | CPUID_7_0_ECX_OSPKE | \
          CPUID_7_0_ECX_LA57)
#define TCG_7_0_EDX_FEATURES 0
#define TCG_APM_FEATURES 0
#define TCG_6_EAX_FEATURES CPUID_6_EAX_ARAT
#define TCG_XSAVE_FEATURES (CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XGETBV1)
          /* missing:
          CPUID_XSAVE_XSAVEC, CPUID_XSAVE_XSAVES */

typedef struct FeatureWordInfo {
    /* feature flags names are taken from "Intel Processor Identification and
     * the CPUID Instruction" and AMD's "CPUID Specification".
     * In cases of disagreement between feature naming conventions,
     * aliases may be added.
     */
    const char *feat_names[32];
    uint32_t cpuid_eax;   /* Input EAX for CPUID */
    bool cpuid_needs_ecx; /* CPUID instruction uses ECX as input */
    uint32_t cpuid_ecx;   /* Input ECX value for CPUID */
    int cpuid_reg;        /* output register (R_* constant) */
    uint32_t tcg_features; /* Feature flags supported by TCG */
    uint32_t unmigratable_flags; /* Feature flags known to be unmigratable */
    uint32_t migratable_flags; /* Feature flags known to be migratable */
} FeatureWordInfo;

static FeatureWordInfo feature_word_info[FEATURE_WORDS] = {
    [FEAT_1_EDX] = {
        .feat_names = {
            "fpu", "vme", "de", "pse",
            "tsc", "msr", "pae", "mce",
            "cx8", "apic", NULL, "sep",
            "mtrr", "pge", "mca", "cmov",
            "pat", "pse36", "pn" /* Intel psn */, "clflush" /* Intel clfsh */,
            NULL, "ds" /* Intel dts */, "acpi", "mmx",
            "fxsr", "sse", "sse2", "ss",
            "ht" /* Intel htt */, "tm", "ia64", "pbe",
        },
        .cpuid_eax = 1, .cpuid_reg = R_EDX,
        .tcg_features = TCG_FEATURES,
    },
    [FEAT_1_ECX] = {
        .feat_names = {
            "pni" /* Intel,AMD sse3 */, "pclmulqdq", "dtes64", "monitor",
            "ds-cpl", "vmx", "smx", "est",
            "tm2", "ssse3", "cid", NULL,
            "fma", "cx16", "xtpr", "pdcm",
            NULL, "pcid", "dca", "sse4.1",
            "sse4.2", "x2apic", "movbe", "popcnt",
            "tsc-deadline", "aes", "xsave", "osxsave",
            "avx", "f16c", "rdrand", "hypervisor",
        },
        .cpuid_eax = 1, .cpuid_reg = R_ECX,
        .tcg_features = TCG_EXT_FEATURES,
    },
    /* Feature names that are already defined on feature_name[] but
     * are set on CPUID[8000_0001].EDX on AMD CPUs don't have their
     * names on feat_names below. They are copied automatically
     * to features[FEAT_8000_0001_EDX] if and only if CPU vendor is AMD.
     */
    [FEAT_8000_0001_EDX] = {
        .feat_names = {
            NULL /* fpu */, NULL /* vme */, NULL /* de */, NULL /* pse */,
            NULL /* tsc */, NULL /* msr */, NULL /* pae */, NULL /* mce */,
            NULL /* cx8 */, NULL /* apic */, NULL, "syscall",
            NULL /* mtrr */, NULL /* pge */, NULL /* mca */, NULL /* cmov */,
            NULL /* pat */, NULL /* pse36 */, NULL, NULL /* Linux mp */,
            "nx", NULL, "mmxext", NULL /* mmx */,
            NULL /* fxsr */, "fxsr-opt", "pdpe1gb", "rdtscp",
            NULL, "lm", "3dnowext", "3dnow",
        },
        .cpuid_eax = 0x80000001, .cpuid_reg = R_EDX,
        .tcg_features = TCG_EXT2_FEATURES,
    },
    [FEAT_8000_0001_ECX] = {
        .feat_names = {
            "lahf-lm", "cmp-legacy", "svm", "extapic",
            "cr8legacy", "abm", "sse4a", "misalignsse",
            "3dnowprefetch", "osvw", "ibs", "xop",
            "skinit", "wdt", NULL, "lwp",
            "fma4", "tce", NULL, "nodeid-msr",
            NULL, "tbm", "topoext", "perfctr-core",
            "perfctr-nb", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0x80000001, .cpuid_reg = R_ECX,
        .tcg_features = TCG_EXT3_FEATURES,
    },
    [FEAT_C000_0001_EDX] = {
        .feat_names = {
            NULL, NULL, "xstore", "xstore-en",
            NULL, NULL, "xcrypt", "xcrypt-en",
            "ace2", "ace2-en", "phe", "phe-en",
            "pmm", "pmm-en", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0xC0000001, .cpuid_reg = R_EDX,
        .tcg_features = TCG_EXT4_FEATURES,
    },
    [FEAT_KVM] = {
        .feat_names = {
            "kvmclock", "kvm-nopiodelay", "kvm-mmu", "kvmclock",
            "kvm-asyncpf", "kvm-steal-time", "kvm-pv-eoi", "kvm-pv-unhalt",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "kvmclock-stable-bit", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = KVM_CPUID_FEATURES, .cpuid_reg = R_EAX,
        .tcg_features = TCG_KVM_FEATURES,
    },
    [FEAT_HYPERV_EAX] = {
        .feat_names = {
            NULL /* hv_msr_vp_runtime_access */, NULL /* hv_msr_time_refcount_access */,
            NULL /* hv_msr_synic_access */, NULL /* hv_msr_stimer_access */,
            NULL /* hv_msr_apic_access */, NULL /* hv_msr_hypercall_access */,
            NULL /* hv_vpindex_access */, NULL /* hv_msr_reset_access */,
            NULL /* hv_msr_stats_access */, NULL /* hv_reftsc_access */,
            NULL /* hv_msr_idle_access */, NULL /* hv_msr_frequency_access */,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0x40000003, .cpuid_reg = R_EAX,
    },
    [FEAT_HYPERV_EBX] = {
        .feat_names = {
            NULL /* hv_create_partitions */, NULL /* hv_access_partition_id */,
            NULL /* hv_access_memory_pool */, NULL /* hv_adjust_message_buffers */,
            NULL /* hv_post_messages */, NULL /* hv_signal_events */,
            NULL /* hv_create_port */, NULL /* hv_connect_port */,
            NULL /* hv_access_stats */, NULL, NULL, NULL /* hv_debugging */,
            NULL /* hv_cpu_power_management */, NULL /* hv_configure_profiler */,
            NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0x40000003, .cpuid_reg = R_EBX,
    },
    [FEAT_HYPERV_EDX] = {
        .feat_names = {
            NULL /* hv_mwait */, NULL /* hv_guest_debugging */,
            NULL /* hv_perf_monitor */, NULL /* hv_cpu_dynamic_part */,
            NULL /* hv_hypercall_params_xmm */, NULL /* hv_guest_idle_state */,
            NULL, NULL,
            NULL, NULL, NULL /* hv_guest_crash_msr */, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0x40000003, .cpuid_reg = R_EDX,
    },
    [FEAT_SVM] = {
        .feat_names = {
            "npt", "lbrv", "svm-lock", "nrip-save",
            "tsc-scale", "vmcb-clean",  "flushbyasid", "decodeassists",
            NULL, NULL, "pause-filter", NULL,
            "pfthreshold", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0x8000000A, .cpuid_reg = R_EDX,
        .tcg_features = TCG_SVM_FEATURES,
    },
    [FEAT_7_0_EBX] = {
        .feat_names = {
            "fsgsbase", "tsc-adjust", NULL, "bmi1",
            "hle", "avx2", NULL, "smep",
            "bmi2", "erms", "invpcid", "rtm",
            NULL, NULL, "mpx", NULL,
            "avx512f", "avx512dq", "rdseed", "adx",
            "smap", "avx512ifma", "pcommit", "clflushopt",
            "clwb", NULL, "avx512pf", "avx512er",
            "avx512cd", "sha-ni", "avx512bw", "avx512vl",
        },
        .cpuid_eax = 7,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_EBX,
        .tcg_features = TCG_7_0_EBX_FEATURES,
    },
    [FEAT_7_0_ECX] = {
        .feat_names = {
            NULL, "avx512vbmi", "umip", "pku",
            "ospke", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, "avx512-vpopcntdq", NULL,
            "la57", NULL, NULL, NULL,
            NULL, NULL, "rdpid", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 7,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_ECX,
        .tcg_features = TCG_7_0_ECX_FEATURES,
    },
    [FEAT_7_0_EDX] = {
        .feat_names = {
            NULL, NULL, "avx512-4vnniw", "avx512-4fmaps",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 7,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_EDX,
        .tcg_features = TCG_7_0_EDX_FEATURES,
    },
    [FEAT_8000_0007_EDX] = {
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "invtsc", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0x80000007,
        .cpuid_reg = R_EDX,
        .tcg_features = TCG_APM_FEATURES,
        .unmigratable_flags = CPUID_APM_INVTSC,
    },
    [FEAT_XSAVE] = {
        .feat_names = {
            "xsaveopt", "xsavec", "xgetbv1", "xsaves",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 0xd,
        .cpuid_needs_ecx = true, .cpuid_ecx = 1,
        .cpuid_reg = R_EAX,
        .tcg_features = TCG_XSAVE_FEATURES,
    },
    [FEAT_6_EAX] = {
        .feat_names = {
            NULL, NULL, "arat", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid_eax = 6, .cpuid_reg = R_EAX,
        .tcg_features = TCG_6_EAX_FEATURES,
    },
    [FEAT_XSAVE_COMP_LO] = {
        .cpuid_eax = 0xD,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_EAX,
        .tcg_features = ~0U,
        .migratable_flags = XSTATE_FP_MASK | XSTATE_SSE_MASK |
            XSTATE_YMM_MASK | XSTATE_BNDREGS_MASK | XSTATE_BNDCSR_MASK |
            XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK | XSTATE_Hi16_ZMM_MASK |
            XSTATE_PKRU_MASK,
    },
    [FEAT_XSAVE_COMP_HI] = {
        .cpuid_eax = 0xD,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_EDX,
        .tcg_features = ~0U,
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

static const ExtSaveArea x86_ext_save_areas[] = {
    [XSTATE_FP_BIT] = {
        /* x87 FP state component is always enabled if XSAVE is supported */
        .feature = FEAT_1_ECX, .bits = CPUID_EXT_XSAVE,
        /* x87 state is in the legacy region of the XSAVE area */
        .offset = 0,
        .size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader),
    },
    [XSTATE_SSE_BIT] = {
        /* SSE state component is always enabled if XSAVE is supported */
        .feature = FEAT_1_ECX, .bits = CPUID_EXT_XSAVE,
        /* SSE state is in the legacy region of the XSAVE area */
        .offset = 0,
        .size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader),
    },
    [XSTATE_YMM_BIT] =
          { .feature = FEAT_1_ECX, .bits = CPUID_EXT_AVX,
            .offset = offsetof(X86XSaveArea, avx_state),
            .size = sizeof(XSaveAVX) },
    [XSTATE_BNDREGS_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .offset = offsetof(X86XSaveArea, bndreg_state),
            .size = sizeof(XSaveBNDREG)  },
    [XSTATE_BNDCSR_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .offset = offsetof(X86XSaveArea, bndcsr_state),
            .size = sizeof(XSaveBNDCSR)  },
    [XSTATE_OPMASK_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .offset = offsetof(X86XSaveArea, opmask_state),
            .size = sizeof(XSaveOpmask) },
    [XSTATE_ZMM_Hi256_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .offset = offsetof(X86XSaveArea, zmm_hi256_state),
            .size = sizeof(XSaveZMM_Hi256) },
    [XSTATE_Hi16_ZMM_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .offset = offsetof(X86XSaveArea, hi16_zmm_state),
            .size = sizeof(XSaveHi16_ZMM) },
    [XSTATE_PKRU_BIT] =
          { .feature = FEAT_7_0_ECX, .bits = CPUID_7_0_ECX_PKU,
            .offset = offsetof(X86XSaveArea, pkru_state),
            .size = sizeof(XSavePKRU) },
};

static uint32_t xsave_area_size(uint64_t mask)
{
    int i;
    uint64_t ret = 0;

    for (i = 0; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if ((mask >> i) & 1) {
            ret = MAX(ret, esa->offset + esa->size);
        }
    }
    return ret;
}

static inline uint64_t x86_cpu_xsave_components(X86CPU *cpu)
{
    return ((uint64_t)cpu->env.features[FEAT_XSAVE_COMP_HI]) << 32 |
           cpu->env.features[FEAT_XSAVE_COMP_LO];
}

const char *get_register_name_32(unsigned int reg)
{
    if (reg >= CPU_NB_REGS32) {
        return NULL;
    }
    return x86_reg_info_32[reg].name;
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

        /* If the feature name is known, it is implicitly considered migratable,
         * unless it is explicitly set in unmigratable_flags */
        if ((wi->migratable_flags & f) ||
            (wi->feat_names[i] && !(wi->unmigratable_flags & f))) {
            r |= f;
        }
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

void host_vendor_fms(char *vendor, int *family, int *model, int *stepping)
{
    uint32_t eax, ebx, ecx, edx;

    host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_vendor_words2str(vendor, ebx, edx, ecx);

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    if (family) {
        *family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
    }
    if (model) {
        *model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
    }
    if (stepping) {
        *stepping = eax & 0x0F;
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

static char *x86_cpu_class_get_model_name(X86CPUClass *cc)
{
    const char *class_name = object_class_get_name(OBJECT_CLASS(cc));
    assert(g_str_has_suffix(class_name, X86_CPU_TYPE_SUFFIX));
    return g_strndup(class_name,
                     strlen(class_name) - strlen(X86_CPU_TYPE_SUFFIX));
}

struct X86CPUDefinition {
    const char *name;
    uint32_t level;
    uint32_t xlevel;
    /* vendor is zero-terminated, 12 character ASCII string */
    char vendor[CPUID_VENDOR_SZ + 1];
    int family;
    int model;
    int stepping;
    FeatureWordArray features;
    char model_id[48];
};

static X86CPUDefinition builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 6,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM,
        .xlevel = 0x8000000A,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "phenom",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        /* Missing: CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
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
        /* Missing: CPUID_SVM_LBRV */
        .features[FEAT_SVM] =
            CPUID_SVM_NPT,
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
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_ACPI | CPUID_SS,
        /* Missing: CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_EST,
         * CPUID_EXT_TM2, CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_VMX */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_CX16,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Core(TM)2 Duo CPU     T7700  @ 2.40GHz",
    },
    {
        .name = "kvm64",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        /* Missing: CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
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
            CPUID_EXT_SSE3,
        .xlevel = 0x80000004,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "kvm32",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
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
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_ACPI |
            CPUID_SS,
        /* Missing: CPUID_EXT_EST, CPUID_EXT_TM2 , CPUID_EXT_XTPR,
         * CPUID_EXT_PDCM, CPUID_EXT_VMX */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR,
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
        .level = 3,
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
            CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
        .xlevel = 0x80000008,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "n270",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 28,
        .stepping = 2,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME |
            CPUID_ACPI | CPUID_SS,
            /* Some CPUs got no CPUID_SEP */
        /* Missing: CPUID_EXT_DSCPL, CPUID_EXT_EST, CPUID_EXT_TM2,
         * CPUID_EXT_XTPR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_MOVBE,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
    {
        .name = "Conroe",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
        .xlevel = 0x80000008,
        .model_id = "Intel Celeron_4x0 (Conroe/Merom Class Core 2)",
    },
    {
        .name = "Penryn",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 23,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
        .xlevel = 0x80000008,
        .model_id = "Intel Core 2 Duo P9xxx (Penryn Class Core 2)",
    },
    {
        .name = "Nehalem",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 26,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
        .xlevel = 0x80000008,
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
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
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
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon E312xx (Sandy Bridge)",
    },
    {
        .name = "IvyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 58,
        .stepping = 9,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
            CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3 | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_ERMS,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon E3-12xx v2 (Ivy Bridge)",
    },
    {
        .name = "Haswell-noTSX",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 60,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Haswell, no TSX)",
    },    {
        .name = "Haswell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 60,
        .stepping = 4,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Haswell)",
    },
    {
        .name = "Broadwell-noTSX",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 61,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Broadwell, no TSX)",
    },
    {
        .name = "Broadwell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 61,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Broadwell)",
    },
    {
        .name = "Skylake-Client",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 94,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
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
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_MPX,
        /* Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.6).
         * KVM doesn't yet expose any XSAVES state save component,
         * and the only one defined in Skylake (processor tracing)
         * probably will block migration anyway.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Skylake)",
    },
    {
        .name = "Opteron_G1",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
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
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_CX16 | CPUID_EXT_SSE3,
        /* Missing: CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 22xx (Gen 2 Class Opteron)",
    },
    {
        .name = "Opteron_G3",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_CX16 | CPUID_EXT_MONITOR |
            CPUID_EXT_SSE3,
        /* Missing: CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
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
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        /* Missing: CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        /* no xsaveopt! */
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
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_F16C | CPUID_EXT_AVX | CPUID_EXT_XSAVE |
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_FMA |
            CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        /* Missing: CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_TBM | CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        /* no xsaveopt! */
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 63xx class CPU",
    },
};

typedef struct PropValue {
    const char *prop, *value;
} PropValue;

/* KVM-specific features that are automatically added/removed
 * from all CPU models when KVM is enabled.
 */
static PropValue kvm_default_props[] = {
    { "kvmclock", "on" },
    { "kvm-nopiodelay", "on" },
    { "kvm-asyncpf", "on" },
    { "kvm-steal-time", "on" },
    { "kvm-pv-eoi", "on" },
    { "kvmclock-stable-bit", "on" },
    { "x2apic", "on" },
    { "acpi", "off" },
    { "monitor", "off" },
    { "svm", "off" },
    { NULL, NULL },
};

/* TCG-specific defaults that override all CPU models when using TCG
 */
static PropValue tcg_default_props[] = {
    { "vme", "off" },
    { NULL, NULL },
};


void x86_cpu_change_kvm_default(const char *prop, const char *value)
{
    PropValue *pv;
    for (pv = kvm_default_props; pv->prop; pv++) {
        if (!strcmp(pv->prop, prop)) {
            pv->value = value;
            break;
        }
    }

    /* It is valid to call this function only for properties that
     * are already present in the kvm_default_props table.
     */
    assert(pv->prop);
}

static uint32_t x86_cpu_get_supported_feature_word(FeatureWord w,
                                                   bool migratable_only);

static bool lmce_supported(void)
{
    uint64_t mce_cap = 0;

#ifdef CONFIG_KVM
    if (kvm_ioctl(kvm_state, KVM_X86_GET_MCE_CAP_SUPPORTED, &mce_cap) < 0) {
        return false;
    }
#endif

    return !!(mce_cap & MCG_LMCE_P);
}

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

static Property max_x86_cpu_properties[] = {
    DEFINE_PROP_BOOL("migratable", X86CPU, migratable, true),
    DEFINE_PROP_BOOL("host-cache-info", X86CPU, cache_info_passthrough, false),
    DEFINE_PROP_END_OF_LIST()
};

static void max_x86_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->ordering = 9;

    xcc->model_description =
        "Enables all features supported by the accelerator in the current host";

    dc->props = max_x86_cpu_properties;
}

static void x86_cpu_load_def(X86CPU *cpu, X86CPUDefinition *def, Error **errp);

static void max_x86_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    KVMState *s = kvm_state;

    /* We can't fill the features array here because we don't know yet if
     * "migratable" is true or false.
     */
    cpu->max_features = true;

    if (kvm_enabled()) {
        X86CPUDefinition host_cpudef = { };
        uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

        host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);
        x86_cpu_vendor_words2str(host_cpudef.vendor, ebx, edx, ecx);

        host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
        host_cpudef.family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
        host_cpudef.model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
        host_cpudef.stepping = eax & 0x0F;

        cpu_x86_fill_model_id(host_cpudef.model_id);

        x86_cpu_load_def(cpu, &host_cpudef, &error_abort);

        env->cpuid_min_level =
            kvm_arch_get_supported_cpuid(s, 0x0, 0, R_EAX);
        env->cpuid_min_xlevel =
            kvm_arch_get_supported_cpuid(s, 0x80000000, 0, R_EAX);
        env->cpuid_min_xlevel2 =
            kvm_arch_get_supported_cpuid(s, 0xC0000000, 0, R_EAX);

        if (lmce_supported()) {
            object_property_set_bool(OBJECT(cpu), true, "lmce", &error_abort);
        }
    } else {
        object_property_set_str(OBJECT(cpu), CPUID_VENDOR_AMD,
                                "vendor", &error_abort);
        object_property_set_int(OBJECT(cpu), 6, "family", &error_abort);
        object_property_set_int(OBJECT(cpu), 6, "model", &error_abort);
        object_property_set_int(OBJECT(cpu), 3, "stepping", &error_abort);
        object_property_set_str(OBJECT(cpu),
                                "QEMU TCG CPU version " QEMU_HW_VERSION,
                                "model-id", &error_abort);
    }

    object_property_set_bool(OBJECT(cpu), true, "pmu", &error_abort);
}

static const TypeInfo max_x86_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("max"),
    .parent = TYPE_X86_CPU,
    .instance_init = max_x86_cpu_initfn,
    .class_init = max_x86_cpu_class_init,
};

#ifdef CONFIG_KVM

static void host_x86_cpu_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->kvm_required = true;
    xcc->ordering = 8;

    xcc->model_description =
        "KVM processor with all supported host features "
        "(only available in KVM mode)";
}

static const TypeInfo host_x86_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("host"),
    .parent = X86_CPU_TYPE_NAME("max"),
    .class_init = host_x86_cpu_class_init,
};

#endif

static void report_unavailable_features(FeatureWord w, uint32_t mask)
{
    FeatureWordInfo *f = &feature_word_info[w];
    int i;

    for (i = 0; i < 32; ++i) {
        if ((1UL << i) & mask) {
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

static void x86_cpuid_version_get_family(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 8) & 0xf;
    if (value == 0xf) {
        value += (env->cpuid_version >> 20) & 0xff;
    }
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_version_set_family(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff + 0xf;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
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

static void x86_cpuid_version_get_model(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 4) & 0xf;
    value |= ((env->cpuid_version >> 16) & 0xf) << 4;
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_version_set_model(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf00f0;
    env->cpuid_version |= ((value & 0xf) << 4) | ((value >> 4) << 16);
}

static void x86_cpuid_version_get_stepping(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = env->cpuid_version & 0xf;
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_version_set_stepping(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xf;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf;
    env->cpuid_version |= value & 0xf;
}

static char *x86_cpuid_get_vendor(Object *obj, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;

    value = g_malloc(CPUID_VENDOR_SZ + 1);
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
        error_setg(errp, QERR_PROPERTY_VALUE_BAD, "", "vendor", value);
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

static void x86_cpuid_get_tsc_freq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value;

    value = cpu->env.tsc_khz * 1000;
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_set_tsc_freq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    const int64_t min = 0;
    const int64_t max = INT64_MAX;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    cpu->env.tsc_khz = cpu->env.user_tsc_khz = value / 1000;
}

/* Generic getter for "feature-words" and "filtered-features" properties */
static void x86_cpu_get_feature_words(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    uint32_t *array = (uint32_t *)opaque;
    FeatureWord w;
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

    visit_type_X86CPUFeatureWordInfoList(v, "feature-words", &list, errp);
}

static void x86_get_hv_spinlocks(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value = cpu->hyperv_spinlock_attempts;

    visit_type_int(v, name, &value, errp);
}

static void x86_set_hv_spinlocks(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    const int64_t min = 0xFFF;
    const int64_t max = UINT_MAX;
    X86CPU *cpu = X86_CPU(obj);
    Error *err = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &err);
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

/* Return the feature property name for a feature flag bit */
static const char *x86_cpu_feature_name(FeatureWord w, int bitnr)
{
    /* XSAVE components are automatically enabled by other features,
     * so return the original feature name instead
     */
    if (w == FEAT_XSAVE_COMP_LO || w == FEAT_XSAVE_COMP_HI) {
        int comp = (w == FEAT_XSAVE_COMP_HI) ? bitnr + 32 : bitnr;

        if (comp < ARRAY_SIZE(x86_ext_save_areas) &&
            x86_ext_save_areas[comp].bits) {
            w = x86_ext_save_areas[comp].feature;
            bitnr = ctz32(x86_ext_save_areas[comp].bits);
        }
    }

    assert(bitnr < 32);
    assert(w < FEATURE_WORDS);
    return feature_word_info[w].feat_names[bitnr];
}

/* Compatibily hack to maintain legacy +-feat semantic,
 * where +-feat overwrites any feature set by
 * feat=on|feat even if the later is parsed after +-feat
 * (i.e. "-x2apic,x2apic=on" will result in x2apic disabled)
 */
static GList *plus_features, *minus_features;

static gint compare_string(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(a, b);
}

/* Parse "+feature,-feature,feature=foo" CPU feature string
 */
static void x86_cpu_parse_featurestr(const char *typename, char *features,
                                     Error **errp)
{
    char *featurestr; /* Single 'key=value" string being parsed */
    static bool cpu_globals_initialized;
    bool ambiguous = false;

    if (cpu_globals_initialized) {
        return;
    }
    cpu_globals_initialized = true;

    if (!features) {
        return;
    }

    for (featurestr = strtok(features, ",");
         featurestr;
         featurestr = strtok(NULL, ",")) {
        const char *name;
        const char *val = NULL;
        char *eq = NULL;
        char num[32];
        GlobalProperty *prop;

        /* Compatibility syntax: */
        if (featurestr[0] == '+') {
            plus_features = g_list_append(plus_features,
                                          g_strdup(featurestr + 1));
            continue;
        } else if (featurestr[0] == '-') {
            minus_features = g_list_append(minus_features,
                                           g_strdup(featurestr + 1));
            continue;
        }

        eq = strchr(featurestr, '=');
        if (eq) {
            *eq++ = 0;
            val = eq;
        } else {
            val = "on";
        }

        feat2prop(featurestr);
        name = featurestr;

        if (g_list_find_custom(plus_features, name, compare_string)) {
            error_report("warning: Ambiguous CPU model string. "
                         "Don't mix both \"+%s\" and \"%s=%s\"",
                         name, name, val);
            ambiguous = true;
        }
        if (g_list_find_custom(minus_features, name, compare_string)) {
            error_report("warning: Ambiguous CPU model string. "
                         "Don't mix both \"-%s\" and \"%s=%s\"",
                         name, name, val);
            ambiguous = true;
        }

        /* Special case: */
        if (!strcmp(name, "tsc-freq")) {
            int ret;
            uint64_t tsc_freq;

            ret = qemu_strtosz_metric(val, NULL, &tsc_freq);
            if (ret < 0 || tsc_freq > INT64_MAX) {
                error_setg(errp, "bad numerical value %s", val);
                return;
            }
            snprintf(num, sizeof(num), "%" PRId64, tsc_freq);
            val = num;
            name = "tsc-frequency";
        }

        prop = g_new0(typeof(*prop), 1);
        prop->driver = typename;
        prop->property = g_strdup(name);
        prop->value = g_strdup(val);
        prop->errp = &error_fatal;
        qdev_prop_register_global(prop);
    }

    if (ambiguous) {
        error_report("warning: Compatibility of ambiguous CPU model "
                     "strings won't be kept on future QEMU versions");
    }
}

static void x86_cpu_expand_features(X86CPU *cpu, Error **errp);
static int x86_cpu_filter_features(X86CPU *cpu);

/* Check for missing features that may prevent the CPU class from
 * running using the current machine and accelerator.
 */
static void x86_cpu_class_check_missing_features(X86CPUClass *xcc,
                                                 strList **missing_feats)
{
    X86CPU *xc;
    FeatureWord w;
    Error *err = NULL;
    strList **next = missing_feats;

    if (xcc->kvm_required && !kvm_enabled()) {
        strList *new = g_new0(strList, 1);
        new->value = g_strdup("kvm");;
        *missing_feats = new;
        return;
    }

    xc = X86_CPU(object_new(object_class_get_name(OBJECT_CLASS(xcc))));

    x86_cpu_expand_features(xc, &err);
    if (err) {
        /* Errors at x86_cpu_expand_features should never happen,
         * but in case it does, just report the model as not
         * runnable at all using the "type" property.
         */
        strList *new = g_new0(strList, 1);
        new->value = g_strdup("type");
        *next = new;
        next = &new->next;
    }

    x86_cpu_filter_features(xc);

    for (w = 0; w < FEATURE_WORDS; w++) {
        uint32_t filtered = xc->filtered_features[w];
        int i;
        for (i = 0; i < 32; i++) {
            if (filtered & (1UL << i)) {
                strList *new = g_new0(strList, 1);
                new->value = g_strdup(x86_cpu_feature_name(w, i));
                *next = new;
                next = &new->next;
            }
        }
    }

    object_unref(OBJECT(xc));
}

/* Print all cpuid feature names in featureset
 */
static void listflags(FILE *f, fprintf_function print, const char **featureset)
{
    int bit;
    bool first = true;

    for (bit = 0; bit < 32; bit++) {
        if (featureset[bit]) {
            print(f, "%s%s", first ? "" : " ", featureset[bit]);
            first = false;
        }
    }
}

/* Sort alphabetically by type name, respecting X86CPUClass::ordering. */
static gint x86_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    X86CPUClass *cc_a = X86_CPU_CLASS(class_a);
    X86CPUClass *cc_b = X86_CPU_CLASS(class_b);
    const char *name_a, *name_b;

    if (cc_a->ordering != cc_b->ordering) {
        return cc_a->ordering - cc_b->ordering;
    } else {
        name_a = object_class_get_name(class_a);
        name_b = object_class_get_name(class_b);
        return strcmp(name_a, name_b);
    }
}

static GSList *get_sorted_cpu_model_list(void)
{
    GSList *list = object_class_get_list(TYPE_X86_CPU, false);
    list = g_slist_sort(list, x86_cpu_list_compare);
    return list;
}

static void x86_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    X86CPUClass *cc = X86_CPU_CLASS(oc);
    CPUListState *s = user_data;
    char *name = x86_cpu_class_get_model_name(cc);
    const char *desc = cc->model_description;
    if (!desc && cc->cpu_def) {
        desc = cc->cpu_def->model_id;
    }

    (*s->cpu_fprintf)(s->file, "x86 %16s  %-48s\n",
                      name, desc);
    g_free(name);
}

/* list available CPU models and flags */
void x86_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    int i;
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    (*cpu_fprintf)(f, "Available CPUs:\n");
    list = get_sorted_cpu_model_list();
    g_slist_foreach(list, x86_cpu_list_entry, &s);
    g_slist_free(list);

    (*cpu_fprintf)(f, "\nRecognized CPUID flags:\n");
    for (i = 0; i < ARRAY_SIZE(feature_word_info); i++) {
        FeatureWordInfo *fw = &feature_word_info[i];

        (*cpu_fprintf)(f, "  ");
        listflags(f, cpu_fprintf, fw->feat_names);
        (*cpu_fprintf)(f, "\n");
    }
}

static void x86_cpu_definition_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    X86CPUClass *cc = X86_CPU_CLASS(oc);
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfoList *entry;
    CpuDefinitionInfo *info;

    info = g_malloc0(sizeof(*info));
    info->name = x86_cpu_class_get_model_name(cc);
    x86_cpu_class_check_missing_features(cc, &info->unavailable_features);
    info->has_unavailable_features = true;
    info->q_typename = g_strdup(object_class_get_name(oc));
    info->migration_safe = cc->migration_safe;
    info->has_migration_safe = true;
    info->q_static = cc->static_model;

    entry = g_malloc0(sizeof(*entry));
    entry->value = info;
    entry->next = *cpu_list;
    *cpu_list = entry;
}

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list = get_sorted_cpu_model_list();
    g_slist_foreach(list, x86_cpu_definition_entry, &cpu_list);
    g_slist_free(list);
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

static void x86_cpu_report_filtered_features(X86CPU *cpu)
{
    FeatureWord w;

    for (w = 0; w < FEATURE_WORDS; w++) {
        report_unavailable_features(w, cpu->filtered_features[w]);
    }
}

static void x86_cpu_apply_props(X86CPU *cpu, PropValue *props)
{
    PropValue *pv;
    for (pv = props; pv->prop; pv++) {
        if (!pv->value) {
            continue;
        }
        object_property_parse(OBJECT(cpu), pv->value, pv->prop,
                              &error_abort);
    }
}

/* Load data from X86CPUDefinition into a X86CPU object
 */
static void x86_cpu_load_def(X86CPU *cpu, X86CPUDefinition *def, Error **errp)
{
    CPUX86State *env = &cpu->env;
    const char *vendor;
    char host_vendor[CPUID_VENDOR_SZ + 1];
    FeatureWord w;

    /*NOTE: any property set by this function should be returned by
     * x86_cpu_static_props(), so static expansion of
     * query-cpu-model-expansion is always complete.
     */

    /* CPU models only set _minimum_ values for level/xlevel: */
    object_property_set_int(OBJECT(cpu), def->level, "min-level", errp);
    object_property_set_int(OBJECT(cpu), def->xlevel, "min-xlevel", errp);

    object_property_set_int(OBJECT(cpu), def->family, "family", errp);
    object_property_set_int(OBJECT(cpu), def->model, "model", errp);
    object_property_set_int(OBJECT(cpu), def->stepping, "stepping", errp);
    object_property_set_str(OBJECT(cpu), def->model_id, "model-id", errp);
    for (w = 0; w < FEATURE_WORDS; w++) {
        env->features[w] = def->features[w];
    }

    /* Special cases not set in the X86CPUDefinition structs: */
    if (kvm_enabled()) {
        if (!kvm_irqchip_in_kernel()) {
            x86_cpu_change_kvm_default("x2apic", "off");
        }

        x86_cpu_apply_props(cpu, kvm_default_props);
    } else if (tcg_enabled()) {
        x86_cpu_apply_props(cpu, tcg_default_props);
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

/* Return a QDict containing keys for all properties that can be included
 * in static expansion of CPU models. All properties set by x86_cpu_load_def()
 * must be included in the dictionary.
 */
static QDict *x86_cpu_static_props(void)
{
    FeatureWord w;
    int i;
    static const char *props[] = {
        "min-level",
        "min-xlevel",
        "family",
        "model",
        "stepping",
        "model-id",
        "vendor",
        "lmce",
        NULL,
    };
    static QDict *d;

    if (d) {
        return d;
    }

    d = qdict_new();
    for (i = 0; props[i]; i++) {
        qdict_put_obj(d, props[i], qnull());
    }

    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *fi = &feature_word_info[w];
        int bit;
        for (bit = 0; bit < 32; bit++) {
            if (!fi->feat_names[bit]) {
                continue;
            }
            qdict_put_obj(d, fi->feat_names[bit], qnull());
        }
    }

    return d;
}

/* Add an entry to @props dict, with the value for property. */
static void x86_cpu_expand_prop(X86CPU *cpu, QDict *props, const char *prop)
{
    QObject *value = object_property_get_qobject(OBJECT(cpu), prop,
                                                 &error_abort);

    qdict_put_obj(props, prop, value);
}

/* Convert CPU model data from X86CPU object to a property dictionary
 * that can recreate exactly the same CPU model.
 */
static void x86_cpu_to_dict(X86CPU *cpu, QDict *props)
{
    QDict *sprops = x86_cpu_static_props();
    const QDictEntry *e;

    for (e = qdict_first(sprops); e; e = qdict_next(sprops, e)) {
        const char *prop = qdict_entry_key(e);
        x86_cpu_expand_prop(cpu, props, prop);
    }
}

/* Convert CPU model data from X86CPU object to a property dictionary
 * that can recreate exactly the same CPU model, including every
 * writeable QOM property.
 */
static void x86_cpu_to_dict_full(X86CPU *cpu, QDict *props)
{
    ObjectPropertyIterator iter;
    ObjectProperty *prop;

    object_property_iter_init(&iter, OBJECT(cpu));
    while ((prop = object_property_iter_next(&iter))) {
        /* skip read-only or write-only properties */
        if (!prop->get || !prop->set) {
            continue;
        }

        /* "hotplugged" is the only property that is configurable
         * on the command-line but will be set differently on CPUs
         * created using "-cpu ... -smp ..." and by CPUs created
         * on the fly by x86_cpu_from_model() for querying. Skip it.
         */
        if (!strcmp(prop->name, "hotplugged")) {
            continue;
        }
        x86_cpu_expand_prop(cpu, props, prop->name);
    }
}

static void object_apply_props(Object *obj, QDict *props, Error **errp)
{
    const QDictEntry *prop;
    Error *err = NULL;

    for (prop = qdict_first(props); prop; prop = qdict_next(props, prop)) {
        object_property_set_qobject(obj, qdict_entry_value(prop),
                                         qdict_entry_key(prop), &err);
        if (err) {
            break;
        }
    }

    error_propagate(errp, err);
}

/* Create X86CPU object according to model+props specification */
static X86CPU *x86_cpu_from_model(const char *model, QDict *props, Error **errp)
{
    X86CPU *xc = NULL;
    X86CPUClass *xcc;
    Error *err = NULL;

    xcc = X86_CPU_CLASS(cpu_class_by_name(TYPE_X86_CPU, model));
    if (xcc == NULL) {
        error_setg(&err, "CPU model '%s' not found", model);
        goto out;
    }

    xc = X86_CPU(object_new(object_class_get_name(OBJECT_CLASS(xcc))));
    if (props) {
        object_apply_props(OBJECT(xc), props, &err);
        if (err) {
            goto out;
        }
    }

    x86_cpu_expand_features(xc, &err);
    if (err) {
        goto out;
    }

out:
    if (err) {
        error_propagate(errp, err);
        object_unref(OBJECT(xc));
        xc = NULL;
    }
    return xc;
}

CpuModelExpansionInfo *
arch_query_cpu_model_expansion(CpuModelExpansionType type,
                                                      CpuModelInfo *model,
                                                      Error **errp)
{
    X86CPU *xc = NULL;
    Error *err = NULL;
    CpuModelExpansionInfo *ret = g_new0(CpuModelExpansionInfo, 1);
    QDict *props = NULL;
    const char *base_name;

    xc = x86_cpu_from_model(model->name,
                            model->has_props ?
                                qobject_to_qdict(model->props) :
                                NULL, &err);
    if (err) {
        goto out;
    }

    props = qdict_new();

    switch (type) {
    case CPU_MODEL_EXPANSION_TYPE_STATIC:
        /* Static expansion will be based on "base" only */
        base_name = "base";
        x86_cpu_to_dict(xc, props);
    break;
    case CPU_MODEL_EXPANSION_TYPE_FULL:
        /* As we don't return every single property, full expansion needs
         * to keep the original model name+props, and add extra
         * properties on top of that.
         */
        base_name = model->name;
        x86_cpu_to_dict_full(xc, props);
    break;
    default:
        error_setg(&err, "Unsupportted expansion type");
        goto out;
    }

    if (!props) {
        props = qdict_new();
    }
    x86_cpu_to_dict(xc, props);

    ret->model = g_new0(CpuModelInfo, 1);
    ret->model->name = g_strdup(base_name);
    ret->model->props = QOBJECT(props);
    ret->model->has_props = true;

out:
    object_unref(OBJECT(xc));
    if (err) {
        error_propagate(errp, err);
        qapi_free_CpuModelExpansionInfo(ret);
        ret = NULL;
    }
    return ret;
}

X86CPU *cpu_x86_init(const char *cpu_model)
{
    return X86_CPU(cpu_generic_init(TYPE_X86_CPU, cpu_model));
}

static void x86_cpu_cpudef_class_init(ObjectClass *oc, void *data)
{
    X86CPUDefinition *cpudef = data;
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->cpu_def = cpudef;
    xcc->migration_safe = true;
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

    /* AMD aliases are handled at runtime based on CPUID vendor, so
     * they shouldn't be set on the CPU model table.
     */
    assert(!(def->features[FEAT_8000_0001_EDX] & CPUID_EXT2_AMD_ALIASES));

    type_register(&ti);
    g_free(typename);
}

#if !defined(CONFIG_USER_ONLY)

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] &= ~CPUID_APIC;
}

#endif /* !CONFIG_USER_ONLY */

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    uint32_t pkg_offset;

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
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 1:
        *eax = env->cpuid_version;
        *ebx = (cpu->apic_id << 24) |
               8 << 8; /* CLFLUSH size in quad words, Linux wants it. */
        *ecx = env->features[FEAT_1_ECX];
        if ((*ecx & CPUID_EXT_XSAVE) && (env->cr[4] & CR4_OSXSAVE_MASK)) {
            *ecx |= CPUID_EXT_OSXSAVE;
        }
        *edx = env->features[FEAT_1_EDX];
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ebx |= (cs->nr_cores * cs->nr_threads) << 16;
            *edx |= CPUID_HT;
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
        if (!cpu->enable_l3_cache) {
            *ecx = 0;
        } else {
            *ecx = L3_N_DESCRIPTOR;
        }
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
            case 3: /* L3 cache info */
                if (!cpu->enable_l3_cache) {
                    *eax = 0;
                    *ebx = 0;
                    *ecx = 0;
                    *edx = 0;
                    break;
                }
                *eax |= CPUID_4_TYPE_UNIFIED | \
                        CPUID_4_LEVEL(3) | \
                        CPUID_4_SELF_INIT_LEVEL;
                pkg_offset = apicid_pkg_offset(cs->nr_cores, cs->nr_threads);
                *eax |= ((1 << pkg_offset) - 1) << 14;
                *ebx = (L3_N_LINE_SIZE - 1) | \
                       ((L3_N_PARTITIONS - 1) << 12) | \
                       ((L3_N_ASSOCIATIVITY - 1) << 22);
                *ecx = L3_N_SETS - 1;
                *edx = CPUID_4_INCLUSIVE | CPUID_4_COMPLEX_IDX;
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
        *eax = env->features[FEAT_6_EAX];
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 7:
        /* Structured Extended Feature Flags Enumeration Leaf */
        if (count == 0) {
            *eax = 0; /* Maximum ECX value for sub-leaves */
            *ebx = env->features[FEAT_7_0_EBX]; /* Feature flags */
            *ecx = env->features[FEAT_7_0_ECX]; /* Feature flags */
            if ((*ecx & CPUID_7_0_ECX_PKU) && env->cr[4] & CR4_PKE_MASK) {
                *ecx |= CPUID_7_0_ECX_OSPKE;
            }
            *edx = env->features[FEAT_7_0_EDX]; /* Feature flags */
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
    case 0xB:
        /* Extended Topology Enumeration Leaf */
        if (!cpu->enable_cpuid_0xb) {
                *eax = *ebx = *ecx = *edx = 0;
                break;
        }

        *ecx = count & 0xff;
        *edx = cpu->apic_id;

        switch (count) {
        case 0:
            *eax = apicid_core_offset(cs->nr_cores, cs->nr_threads);
            *ebx = cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_SMT;
            break;
        case 1:
            *eax = apicid_pkg_offset(cs->nr_cores, cs->nr_threads);
            *ebx = cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_CORE;
            break;
        default:
            *eax = 0;
            *ebx = 0;
            *ecx |= CPUID_TOPOLOGY_LEVEL_INVALID;
        }

        assert(!(*eax & ~0x1f));
        *ebx &= 0xffff; /* The count doesn't need to be reliable. */
        break;
    case 0xD: {
        /* Processor Extended State */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
            break;
        }

        if (count == 0) {
            *ecx = xsave_area_size(x86_cpu_xsave_components(cpu));
            *eax = env->features[FEAT_XSAVE_COMP_LO];
            *edx = env->features[FEAT_XSAVE_COMP_HI];
            *ebx = *ecx;
        } else if (count == 1) {
            *eax = env->features[FEAT_XSAVE];
        } else if (count < ARRAY_SIZE(x86_ext_save_areas)) {
            if ((x86_cpu_xsave_components(cpu) >> count) & 1) {
                const ExtSaveArea *esa = &x86_ext_save_areas[count];
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
         * So don't set it here for Intel to make Linux guests happy.
         */
        if (cs->nr_cores * cs->nr_threads > 1) {
            if (env->cpuid_vendor1 != CPUID_VENDOR_INTEL_1 ||
                env->cpuid_vendor2 != CPUID_VENDOR_INTEL_2 ||
                env->cpuid_vendor3 != CPUID_VENDOR_INTEL_3) {
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
        if (!cpu->enable_l3_cache) {
            *edx = ((L3_SIZE_KB / 512) << 18) | \
                   (AMD_ENC_ASSOC(L3_ASSOCIATIVITY) << 12) | \
                   (L3_LINES_PER_TAG << 8) | (L3_LINE_SIZE);
        } else {
            *edx = ((L3_N_SIZE_KB_AMD / 512) << 18) | \
                   (AMD_ENC_ASSOC(L3_N_ASSOCIATIVITY) << 12) | \
                   (L3_N_LINES_PER_TAG << 8) | (L3_N_LINE_SIZE);
        }
        break;
    case 0x80000007:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_8000_0007_EDX];
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
        if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
            /* 64 bit processor */
            *eax = cpu->phys_bits; /* configurable physical bits */
            if  (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) {
                *eax |= 0x00003900; /* 57 bits virtual */
            } else {
                *eax |= 0x00003000; /* 48 bits virtual */
            }
        } else {
            *eax = cpu->phys_bits;
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
    target_ulong cr4;
    uint64_t xcr0;
    int i;

    xcc->parent_reset(s);

    memset(env, 0, offsetof(CPUX86State, end_reset_fields));

    env->old_exception = -1;

    /* init to reset state */

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
    cpu_set_fpuc(env, 0x37f);

    env->mxcsr = 0x1f80;
    /* All units are in INIT state.  */
    env->xstate_bv = 0;

    env->pat = 0x0007040600070406ULL;
    env->msr_ia32_misc_enable = MSR_IA32_MISC_ENABLE_DEFAULT;

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(s, BP_CPU);
    cpu_watchpoint_remove_all(s, BP_CPU);

    cr4 = 0;
    xcr0 = XSTATE_FP_MASK;

#ifdef CONFIG_USER_ONLY
    /* Enable all the features for user-mode.  */
    if (env->features[FEAT_1_EDX] & CPUID_SSE) {
        xcr0 |= XSTATE_SSE_MASK;
    }
    for (i = 2; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if (env->features[esa->feature] & esa->bits) {
            xcr0 |= 1ull << i;
        }
    }

    if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
        cr4 |= CR4_OSFXSR_MASK | CR4_OSXSAVE_MASK;
    }
    if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_FSGSBASE) {
        cr4 |= CR4_FSGSBASE_MASK;
    }
#endif

    env->xcr0 = xcr0;
    cpu_x86_update_cr4(env, cr4);

    /*
     * SDM 11.11.5 requires:
     *  - IA32_MTRR_DEF_TYPE MSR.E = 0
     *  - IA32_MTRR_PHYSMASKn.V = 0
     * All other bits are undefined.  For simplification, zero it all.
     */
    env->mtrr_deftype = 0;
    memset(env->mtrr_var, 0, sizeof(env->mtrr_var));
    memset(env->mtrr_fixed, 0, sizeof(env->mtrr_fixed));

#if !defined(CONFIG_USER_ONLY)
    /* We hard-wire the BSP to the first CPU. */
    apic_designate_bsp(cpu->apic_state, s->cpu_index == 0);

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
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF |
                        (cpu->enable_lmce ? MCG_LMCE_P : 0);
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

#ifndef CONFIG_USER_ONLY
APICCommonClass *apic_get_class(void)
{
    const char *apic_type = "apic";

    if (kvm_apic_in_kernel()) {
        apic_type = "kvm-apic";
    } else if (xen_enabled()) {
        apic_type = "xen-apic";
    }

    return APIC_COMMON_CLASS(object_class_by_name(apic_type));
}

static void x86_cpu_apic_create(X86CPU *cpu, Error **errp)
{
    APICCommonState *apic;
    ObjectClass *apic_class = OBJECT_CLASS(apic_get_class());

    cpu->apic_state = DEVICE(object_new(object_class_get_name(apic_class)));

    object_property_add_child(OBJECT(cpu), "lapic",
                              OBJECT(cpu->apic_state), &error_abort);
    object_unref(OBJECT(cpu->apic_state));

    qdev_prop_set_uint32(cpu->apic_state, "id", cpu->apic_id);
    /* TODO: convert to link<> */
    apic = APIC_COMMON(cpu->apic_state);
    apic->cpu = cpu;
    apic->apicbase = APIC_DEFAULT_ADDRESS | MSR_IA32_APICBASE_ENABLE;
}

static void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
    APICCommonState *apic;
    static bool apic_mmio_map_once;

    if (cpu->apic_state == NULL) {
        return;
    }
    object_property_set_bool(OBJECT(cpu->apic_state), true, "realized",
                             errp);

    /* Map APIC MMIO area */
    apic = APIC_COMMON(cpu->apic_state);
    if (!apic_mmio_map_once) {
        memory_region_add_subregion_overlap(get_system_memory(),
                                            apic->apicbase &
                                            MSR_IA32_APICBASE_BASE,
                                            &apic->io_memory,
                                            0x1000);
        apic_mmio_map_once = true;
     }
}

static void x86_cpu_machine_done(Notifier *n, void *unused)
{
    X86CPU *cpu = container_of(n, X86CPU, machine_done);
    MemoryRegion *smram =
        (MemoryRegion *) object_resolve_path("/machine/smram", NULL);

    if (smram) {
        cpu->smram = g_new(MemoryRegion, 1);
        memory_region_init_alias(cpu->smram, OBJECT(cpu), "smram",
                                 smram, 0, 1ull << 32);
        memory_region_set_enabled(cpu->smram, false);
        memory_region_add_subregion_overlap(cpu->cpu_as_root, 0, cpu->smram, 1);
    }
}
#else
static void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
}
#endif

/* Note: Only safe for use on x86(-64) hosts */
static uint32_t x86_host_phys_bits(void)
{
    uint32_t eax;
    uint32_t host_phys_bits;

    host_cpuid(0x80000000, 0, &eax, NULL, NULL, NULL);
    if (eax >= 0x80000008) {
        host_cpuid(0x80000008, 0, &eax, NULL, NULL, NULL);
        /* Note: According to AMD doc 25481 rev 2.34 they have a field
         * at 23:16 that can specify a maximum physical address bits for
         * the guest that can override this value; but I've not seen
         * anything with that set.
         */
        host_phys_bits = eax & 0xff;
    } else {
        /* It's an odd 64 bit machine that doesn't have the leaf for
         * physical address bits; fall back to 36 that's most older
         * Intel.
         */
        host_phys_bits = 36;
    }

    return host_phys_bits;
}

static void x86_cpu_adjust_level(X86CPU *cpu, uint32_t *min, uint32_t value)
{
    if (*min < value) {
        *min = value;
    }
}

/* Increase cpuid_min_{level,xlevel,xlevel2} automatically, if appropriate */
static void x86_cpu_adjust_feat_level(X86CPU *cpu, FeatureWord w)
{
    CPUX86State *env = &cpu->env;
    FeatureWordInfo *fi = &feature_word_info[w];
    uint32_t eax = fi->cpuid_eax;
    uint32_t region = eax & 0xF0000000;

    if (!env->features[w]) {
        return;
    }

    switch (region) {
    case 0x00000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_level, eax);
    break;
    case 0x80000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, eax);
    break;
    case 0xC0000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel2, eax);
    break;
    }
}

/* Calculate XSAVE components based on the configured CPU feature flags */
static void x86_cpu_enable_xsave_components(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int i;
    uint64_t mask;

    if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
        return;
    }

    mask = 0;
    for (i = 0; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if (env->features[esa->feature] & esa->bits) {
            mask |= (1ULL << i);
        }
    }

    env->features[FEAT_XSAVE_COMP_LO] = mask;
    env->features[FEAT_XSAVE_COMP_HI] = mask >> 32;
}

/***** Steps involved on loading and filtering CPUID data
 *
 * When initializing and realizing a CPU object, the steps
 * involved in setting up CPUID data are:
 *
 * 1) Loading CPU model definition (X86CPUDefinition). This is
 *    implemented by x86_cpu_load_def() and should be completely
 *    transparent, as it is done automatically by instance_init.
 *    No code should need to look at X86CPUDefinition structs
 *    outside instance_init.
 *
 * 2) CPU expansion. This is done by realize before CPUID
 *    filtering, and will make sure host/accelerator data is
 *    loaded for CPU models that depend on host capabilities
 *    (e.g. "host"). Done by x86_cpu_expand_features().
 *
 * 3) CPUID filtering. This initializes extra data related to
 *    CPUID, and checks if the host supports all capabilities
 *    required by the CPU. Runnability of a CPU model is
 *    determined at this step. Done by x86_cpu_filter_features().
 *
 * Some operations don't require all steps to be performed.
 * More precisely:
 *
 * - CPU instance creation (instance_init) will run only CPU
 *   model loading. CPU expansion can't run at instance_init-time
 *   because host/accelerator data may be not available yet.
 * - CPU realization will perform both CPU model expansion and CPUID
 *   filtering, and return an error in case one of them fails.
 * - query-cpu-definitions needs to run all 3 steps. It needs
 *   to run CPUID filtering, as the 'unavailable-features'
 *   field is set based on the filtering results.
 * - The query-cpu-model-expansion QMP command only needs to run
 *   CPU model loading and CPU expansion. It should not filter
 *   any CPUID data based on host capabilities.
 */

/* Expand CPU configuration data, based on configured features
 * and host/accelerator capabilities when appropriate.
 */
static void x86_cpu_expand_features(X86CPU *cpu, Error **errp)
{
    CPUX86State *env = &cpu->env;
    FeatureWord w;
    GList *l;
    Error *local_err = NULL;

    /*TODO: Now cpu->max_features doesn't overwrite features
     * set using QOM properties, and we can convert
     * plus_features & minus_features to global properties
     * inside x86_cpu_parse_featurestr() too.
     */
    if (cpu->max_features) {
        for (w = 0; w < FEATURE_WORDS; w++) {
            /* Override only features that weren't set explicitly
             * by the user.
             */
            env->features[w] |=
                x86_cpu_get_supported_feature_word(w, cpu->migratable) &
                ~env->user_features[w];
        }
    }

    for (l = plus_features; l; l = l->next) {
        const char *prop = l->data;
        object_property_set_bool(OBJECT(cpu), true, prop, &local_err);
        if (local_err) {
            goto out;
        }
    }

    for (l = minus_features; l; l = l->next) {
        const char *prop = l->data;
        object_property_set_bool(OBJECT(cpu), false, prop, &local_err);
        if (local_err) {
            goto out;
        }
    }

    if (!kvm_enabled() || !cpu->expose_kvm) {
        env->features[FEAT_KVM] = 0;
    }

    x86_cpu_enable_xsave_components(cpu);

    /* CPUID[EAX=7,ECX=0].EBX always increased level automatically: */
    x86_cpu_adjust_feat_level(cpu, FEAT_7_0_EBX);
    if (cpu->full_cpuid_auto_level) {
        x86_cpu_adjust_feat_level(cpu, FEAT_1_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_1_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_6_EAX);
        x86_cpu_adjust_feat_level(cpu, FEAT_7_0_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0001_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0001_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0007_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_C000_0001_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_SVM);
        x86_cpu_adjust_feat_level(cpu, FEAT_XSAVE);
        /* SVM requires CPUID[0x8000000A] */
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, 0x8000000A);
        }
    }

    /* Set cpuid_*level* based on cpuid_min_*level, if not explicitly set */
    if (env->cpuid_level == UINT32_MAX) {
        env->cpuid_level = env->cpuid_min_level;
    }
    if (env->cpuid_xlevel == UINT32_MAX) {
        env->cpuid_xlevel = env->cpuid_min_xlevel;
    }
    if (env->cpuid_xlevel2 == UINT32_MAX) {
        env->cpuid_xlevel2 = env->cpuid_min_xlevel2;
    }

out:
    if (local_err != NULL) {
        error_propagate(errp, local_err);
    }
}

/*
 * Finishes initialization of CPUID data, filters CPU feature
 * words based on host availability of each feature.
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
            x86_cpu_get_supported_feature_word(w, false);
        uint32_t requested_features = env->features[w];
        env->features[w] &= host_feat;
        cpu->filtered_features[w] = requested_features & ~env->features[w];
        if (cpu->filtered_features[w]) {
            rv = 1;
        }
    }

    return rv;
}

#define IS_INTEL_CPU(env) ((env)->cpuid_vendor1 == CPUID_VENDOR_INTEL_1 && \
                           (env)->cpuid_vendor2 == CPUID_VENDOR_INTEL_2 && \
                           (env)->cpuid_vendor3 == CPUID_VENDOR_INTEL_3)
#define IS_AMD_CPU(env) ((env)->cpuid_vendor1 == CPUID_VENDOR_AMD_1 && \
                         (env)->cpuid_vendor2 == CPUID_VENDOR_AMD_2 && \
                         (env)->cpuid_vendor3 == CPUID_VENDOR_AMD_3)
static void x86_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
    CPUX86State *env = &cpu->env;
    Error *local_err = NULL;
    static bool ht_warned;

    if (xcc->kvm_required && !kvm_enabled()) {
        char *name = x86_cpu_class_get_model_name(xcc);
        error_setg(&local_err, "CPU model '%s' requires KVM", name);
        g_free(name);
        goto out;
    }

    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        error_setg(errp, "apic-id property was not initialized properly");
        return;
    }

    x86_cpu_expand_features(cpu, &local_err);
    if (local_err) {
        goto out;
    }

    if (x86_cpu_filter_features(cpu) &&
        (cpu->check_cpuid || cpu->enforce_cpuid)) {
        x86_cpu_report_filtered_features(cpu);
        if (cpu->enforce_cpuid) {
            error_setg(&local_err,
                       kvm_enabled() ?
                           "Host doesn't support requested features" :
                           "TCG doesn't support requested features");
            goto out;
        }
    }

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (IS_AMD_CPU(env)) {
        env->features[FEAT_8000_0001_EDX] &= ~CPUID_EXT2_AMD_ALIASES;
        env->features[FEAT_8000_0001_EDX] |= (env->features[FEAT_1_EDX]
           & CPUID_EXT2_AMD_ALIASES);
    }

    /* For 64bit systems think about the number of physical bits to present.
     * ideally this should be the same as the host; anything other than matching
     * the host can cause incorrect guest behaviour.
     * QEMU used to pick the magic value of 40 bits that corresponds to
     * consumer AMD devices but nothing else.
     */
    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        if (kvm_enabled()) {
            uint32_t host_phys_bits = x86_host_phys_bits();
            static bool warned;

            if (cpu->host_phys_bits) {
                /* The user asked for us to use the host physical bits */
                cpu->phys_bits = host_phys_bits;
            }

            /* Print a warning if the user set it to a value that's not the
             * host value.
             */
            if (cpu->phys_bits != host_phys_bits && cpu->phys_bits != 0 &&
                !warned) {
                error_report("Warning: Host physical bits (%u)"
                                 " does not match phys-bits property (%u)",
                                 host_phys_bits, cpu->phys_bits);
                warned = true;
            }

            if (cpu->phys_bits &&
                (cpu->phys_bits > TARGET_PHYS_ADDR_SPACE_BITS ||
                cpu->phys_bits < 32)) {
                error_setg(errp, "phys-bits should be between 32 and %u "
                                 " (but is %u)",
                                 TARGET_PHYS_ADDR_SPACE_BITS, cpu->phys_bits);
                return;
            }
        } else {
            if (cpu->phys_bits && cpu->phys_bits != TCG_PHYS_ADDR_BITS) {
                error_setg(errp, "TCG only supports phys-bits=%u",
                                  TCG_PHYS_ADDR_BITS);
                return;
            }
        }
        /* 0 means it was not explicitly set by the user (or by machine
         * compat_props or by the host code above). In this case, the default
         * is the value used by TCG (40).
         */
        if (cpu->phys_bits == 0) {
            cpu->phys_bits = TCG_PHYS_ADDR_BITS;
        }
    } else {
        /* For 32 bit systems don't use the user set value, but keep
         * phys_bits consistent with what we tell the guest.
         */
        if (cpu->phys_bits != 0) {
            error_setg(errp, "phys-bits is not user-configurable in 32 bit");
            return;
        }

        if (env->features[FEAT_1_EDX] & CPUID_PSE36) {
            cpu->phys_bits = 36;
        } else {
            cpu->phys_bits = 32;
        }
    }
    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (tcg_enabled()) {
        tcg_x86_init();
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

#ifndef CONFIG_USER_ONLY
    if (tcg_enabled()) {
        AddressSpace *newas = g_new(AddressSpace, 1);

        cpu->cpu_as_mem = g_new(MemoryRegion, 1);
        cpu->cpu_as_root = g_new(MemoryRegion, 1);

        /* Outer container... */
        memory_region_init(cpu->cpu_as_root, OBJECT(cpu), "memory", ~0ull);
        memory_region_set_enabled(cpu->cpu_as_root, true);

        /* ... with two regions inside: normal system memory with low
         * priority, and...
         */
        memory_region_init_alias(cpu->cpu_as_mem, OBJECT(cpu), "memory",
                                 get_system_memory(), 0, ~0ull);
        memory_region_add_subregion_overlap(cpu->cpu_as_root, 0, cpu->cpu_as_mem, 0);
        memory_region_set_enabled(cpu->cpu_as_mem, true);
        address_space_init(newas, cpu->cpu_as_root, "CPU");
        cs->num_ases = 1;
        cpu_address_space_init(cs, newas, 0);

        /* ... SMRAM with higher priority, linked from /machine/smram.  */
        cpu->machine_done.notify = x86_cpu_machine_done;
        qemu_add_machine_init_done_notifier(&cpu->machine_done);
    }
#endif

    qemu_init_vcpu(cs);

    /* Only Intel CPUs support hyperthreading. Even though QEMU fixes this
     * issue by adjusting CPUID_0000_0001_EBX and CPUID_8000_0008_ECX
     * based on inputs (sockets,cores,threads), it is still better to gives
     * users a warning.
     *
     * NOTE: the following code has to follow qemu_init_vcpu(). Otherwise
     * cs->nr_threads hasn't be populated yet and the checking is incorrect.
     */
    if (!IS_INTEL_CPU(env) && cs->nr_threads > 1 && !ht_warned) {
        error_report("AMD CPU doesn't support hyperthreading. Please configure"
                     " -smp options properly.");
        ht_warned = true;
    }

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

static void x86_cpu_unrealizefn(DeviceState *dev, Error **errp)
{
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

#ifndef CONFIG_USER_ONLY
    cpu_remove_sync(CPU(dev));
    qemu_unregister_reset(x86_cpu_machine_reset_cb, dev);
#endif

    if (cpu->apic_state) {
        object_unparent(OBJECT(cpu->apic_state));
        cpu->apic_state = NULL;
    }

    xcc->parent_unrealize(dev, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}

typedef struct BitProperty {
    FeatureWord w;
    uint32_t mask;
} BitProperty;

static void x86_cpu_get_bit_prop(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    BitProperty *fp = opaque;
    uint32_t f = cpu->env.features[fp->w];
    bool value = (f & fp->mask) == fp->mask;
    visit_type_bool(v, name, &value, errp);
}

static void x86_cpu_set_bit_prop(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    X86CPU *cpu = X86_CPU(obj);
    BitProperty *fp = opaque;
    Error *local_err = NULL;
    bool value;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_bool(v, name, &value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (value) {
        cpu->env.features[fp->w] |= fp->mask;
    } else {
        cpu->env.features[fp->w] &= ~fp->mask;
    }
    cpu->env.user_features[fp->w] |= fp->mask;
}

static void x86_cpu_release_bit_prop(Object *obj, const char *name,
                                     void *opaque)
{
    BitProperty *prop = opaque;
    g_free(prop);
}

/* Register a boolean property to get/set a single bit in a uint32_t field.
 *
 * The same property name can be registered multiple times to make it affect
 * multiple bits in the same FeatureWord. In that case, the getter will return
 * true only if all bits are set.
 */
static void x86_cpu_register_bit_prop(X86CPU *cpu,
                                      const char *prop_name,
                                      FeatureWord w,
                                      int bitnr)
{
    BitProperty *fp;
    ObjectProperty *op;
    uint32_t mask = (1UL << bitnr);

    op = object_property_find(OBJECT(cpu), prop_name, NULL);
    if (op) {
        fp = op->opaque;
        assert(fp->w == w);
        fp->mask |= mask;
    } else {
        fp = g_new0(BitProperty, 1);
        fp->w = w;
        fp->mask = mask;
        object_property_add(OBJECT(cpu), prop_name, "bool",
                            x86_cpu_get_bit_prop,
                            x86_cpu_set_bit_prop,
                            x86_cpu_release_bit_prop, fp, &error_abort);
    }
}

static void x86_cpu_register_feature_bit_props(X86CPU *cpu,
                                               FeatureWord w,
                                               int bitnr)
{
    FeatureWordInfo *fi = &feature_word_info[w];
    const char *name = fi->feat_names[bitnr];

    if (!name) {
        return;
    }

    /* Property names should use "-" instead of "_".
     * Old names containing underscores are registered as aliases
     * using object_property_add_alias()
     */
    assert(!strchr(name, '_'));
    /* aliases don't use "|" delimiters anymore, they are registered
     * manually using object_property_add_alias() */
    assert(!strchr(name, '|'));
    x86_cpu_register_bit_prop(cpu, name, w, bitnr);
}

static GuestPanicInformation *x86_cpu_get_crash_info(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    GuestPanicInformation *panic_info = NULL;

    if (env->features[FEAT_HYPERV_EDX] & HV_X64_GUEST_CRASH_MSR_AVAILABLE) {
        panic_info = g_malloc0(sizeof(GuestPanicInformation));

        panic_info->type = GUEST_PANIC_INFORMATION_TYPE_HYPER_V;

        assert(HV_X64_MSR_CRASH_PARAMS >= 5);
        panic_info->u.hyper_v.arg1 = env->msr_hv_crash_params[0];
        panic_info->u.hyper_v.arg2 = env->msr_hv_crash_params[1];
        panic_info->u.hyper_v.arg3 = env->msr_hv_crash_params[2];
        panic_info->u.hyper_v.arg4 = env->msr_hv_crash_params[3];
        panic_info->u.hyper_v.arg5 = env->msr_hv_crash_params[4];
    }

    return panic_info;
}
static void x86_cpu_get_crash_info_qom(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    CPUState *cs = CPU(obj);
    GuestPanicInformation *panic_info;

    if (!cs->crash_occurred) {
        error_setg(errp, "No crash occured");
        return;
    }

    panic_info = x86_cpu_get_crash_info(cs);
    if (panic_info == NULL) {
        error_setg(errp, "No crash information");
        return;
    }

    visit_type_GuestPanicInformation(v, "crash-information", &panic_info,
                                     errp);
    qapi_free_GuestPanicInformation(panic_info);
}

static void x86_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    X86CPU *cpu = X86_CPU(obj);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(obj);
    CPUX86State *env = &cpu->env;
    FeatureWord w;

    cs->env_ptr = env;

    object_property_add(obj, "family", "int",
                        x86_cpuid_version_get_family,
                        x86_cpuid_version_set_family, NULL, NULL, NULL);
    object_property_add(obj, "model", "int",
                        x86_cpuid_version_get_model,
                        x86_cpuid_version_set_model, NULL, NULL, NULL);
    object_property_add(obj, "stepping", "int",
                        x86_cpuid_version_get_stepping,
                        x86_cpuid_version_set_stepping, NULL, NULL, NULL);
    object_property_add_str(obj, "vendor",
                            x86_cpuid_get_vendor,
                            x86_cpuid_set_vendor, NULL);
    object_property_add_str(obj, "model-id",
                            x86_cpuid_get_model_id,
                            x86_cpuid_set_model_id, NULL);
    object_property_add(obj, "tsc-frequency", "int",
                        x86_cpuid_get_tsc_freq,
                        x86_cpuid_set_tsc_freq, NULL, NULL, NULL);
    object_property_add(obj, "feature-words", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)env->features, NULL);
    object_property_add(obj, "filtered-features", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)cpu->filtered_features, NULL);

    object_property_add(obj, "crash-information", "GuestPanicInformation",
                        x86_cpu_get_crash_info_qom, NULL, NULL, NULL, NULL);

    cpu->hyperv_spinlock_attempts = HYPERV_SPINLOCK_NEVER_RETRY;

    for (w = 0; w < FEATURE_WORDS; w++) {
        int bitnr;

        for (bitnr = 0; bitnr < 32; bitnr++) {
            x86_cpu_register_feature_bit_props(cpu, w, bitnr);
        }
    }

    object_property_add_alias(obj, "sse3", obj, "pni", &error_abort);
    object_property_add_alias(obj, "pclmuldq", obj, "pclmulqdq", &error_abort);
    object_property_add_alias(obj, "sse4-1", obj, "sse4.1", &error_abort);
    object_property_add_alias(obj, "sse4-2", obj, "sse4.2", &error_abort);
    object_property_add_alias(obj, "xd", obj, "nx", &error_abort);
    object_property_add_alias(obj, "ffxsr", obj, "fxsr-opt", &error_abort);
    object_property_add_alias(obj, "i64", obj, "lm", &error_abort);

    object_property_add_alias(obj, "ds_cpl", obj, "ds-cpl", &error_abort);
    object_property_add_alias(obj, "tsc_adjust", obj, "tsc-adjust", &error_abort);
    object_property_add_alias(obj, "fxsr_opt", obj, "fxsr-opt", &error_abort);
    object_property_add_alias(obj, "lahf_lm", obj, "lahf-lm", &error_abort);
    object_property_add_alias(obj, "cmp_legacy", obj, "cmp-legacy", &error_abort);
    object_property_add_alias(obj, "nodeid_msr", obj, "nodeid-msr", &error_abort);
    object_property_add_alias(obj, "perfctr_core", obj, "perfctr-core", &error_abort);
    object_property_add_alias(obj, "perfctr_nb", obj, "perfctr-nb", &error_abort);
    object_property_add_alias(obj, "kvm_nopiodelay", obj, "kvm-nopiodelay", &error_abort);
    object_property_add_alias(obj, "kvm_mmu", obj, "kvm-mmu", &error_abort);
    object_property_add_alias(obj, "kvm_asyncpf", obj, "kvm-asyncpf", &error_abort);
    object_property_add_alias(obj, "kvm_steal_time", obj, "kvm-steal-time", &error_abort);
    object_property_add_alias(obj, "kvm_pv_eoi", obj, "kvm-pv-eoi", &error_abort);
    object_property_add_alias(obj, "kvm_pv_unhalt", obj, "kvm-pv-unhalt", &error_abort);
    object_property_add_alias(obj, "svm_lock", obj, "svm-lock", &error_abort);
    object_property_add_alias(obj, "nrip_save", obj, "nrip-save", &error_abort);
    object_property_add_alias(obj, "tsc_scale", obj, "tsc-scale", &error_abort);
    object_property_add_alias(obj, "vmcb_clean", obj, "vmcb-clean", &error_abort);
    object_property_add_alias(obj, "pause_filter", obj, "pause-filter", &error_abort);
    object_property_add_alias(obj, "sse4_1", obj, "sse4.1", &error_abort);
    object_property_add_alias(obj, "sse4_2", obj, "sse4.2", &error_abort);

    if (xcc->cpu_def) {
        x86_cpu_load_def(cpu, xcc->cpu_def, &error_abort);
    }
}

static int64_t x86_cpu_get_arch_id(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->apic_id;
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
                                     CPU_INTERRUPT_MCE)) ||
           ((cs->interrupt_request & CPU_INTERRUPT_SMI) &&
            !(env->hflags & HF_SMM_MASK));
}

static Property x86_cpu_properties[] = {
#ifdef CONFIG_USER_ONLY
    /* apic_id = 0 by default for *-user, see commit 9886e834 */
    DEFINE_PROP_UINT32("apic-id", X86CPU, apic_id, 0),
    DEFINE_PROP_INT32("thread-id", X86CPU, thread_id, 0),
    DEFINE_PROP_INT32("core-id", X86CPU, core_id, 0),
    DEFINE_PROP_INT32("socket-id", X86CPU, socket_id, 0),
#else
    DEFINE_PROP_UINT32("apic-id", X86CPU, apic_id, UNASSIGNED_APIC_ID),
    DEFINE_PROP_INT32("thread-id", X86CPU, thread_id, -1),
    DEFINE_PROP_INT32("core-id", X86CPU, core_id, -1),
    DEFINE_PROP_INT32("socket-id", X86CPU, socket_id, -1),
#endif
    DEFINE_PROP_BOOL("pmu", X86CPU, enable_pmu, false),
    { .name  = "hv-spinlocks", .info  = &qdev_prop_spinlocks },
    DEFINE_PROP_BOOL("hv-relaxed", X86CPU, hyperv_relaxed_timing, false),
    DEFINE_PROP_BOOL("hv-vapic", X86CPU, hyperv_vapic, false),
    DEFINE_PROP_BOOL("hv-time", X86CPU, hyperv_time, false),
    DEFINE_PROP_BOOL("hv-crash", X86CPU, hyperv_crash, false),
    DEFINE_PROP_BOOL("hv-reset", X86CPU, hyperv_reset, false),
    DEFINE_PROP_BOOL("hv-vpindex", X86CPU, hyperv_vpindex, false),
    DEFINE_PROP_BOOL("hv-runtime", X86CPU, hyperv_runtime, false),
    DEFINE_PROP_BOOL("hv-synic", X86CPU, hyperv_synic, false),
    DEFINE_PROP_BOOL("hv-stimer", X86CPU, hyperv_stimer, false),
    DEFINE_PROP_BOOL("check", X86CPU, check_cpuid, true),
    DEFINE_PROP_BOOL("enforce", X86CPU, enforce_cpuid, false),
    DEFINE_PROP_BOOL("kvm", X86CPU, expose_kvm, true),
    DEFINE_PROP_UINT32("phys-bits", X86CPU, phys_bits, 0),
    DEFINE_PROP_BOOL("host-phys-bits", X86CPU, host_phys_bits, false),
    DEFINE_PROP_BOOL("fill-mtrr-mask", X86CPU, fill_mtrr_mask, true),
    DEFINE_PROP_UINT32("level", X86CPU, env.cpuid_level, UINT32_MAX),
    DEFINE_PROP_UINT32("xlevel", X86CPU, env.cpuid_xlevel, UINT32_MAX),
    DEFINE_PROP_UINT32("xlevel2", X86CPU, env.cpuid_xlevel2, UINT32_MAX),
    DEFINE_PROP_UINT32("min-level", X86CPU, env.cpuid_min_level, 0),
    DEFINE_PROP_UINT32("min-xlevel", X86CPU, env.cpuid_min_xlevel, 0),
    DEFINE_PROP_UINT32("min-xlevel2", X86CPU, env.cpuid_min_xlevel2, 0),
    DEFINE_PROP_BOOL("full-cpuid-auto-level", X86CPU, full_cpuid_auto_level, true),
    DEFINE_PROP_STRING("hv-vendor-id", X86CPU, hyperv_vendor_id),
    DEFINE_PROP_BOOL("cpuid-0xb", X86CPU, enable_cpuid_0xb, true),
    DEFINE_PROP_BOOL("lmce", X86CPU, enable_lmce, false),
    DEFINE_PROP_BOOL("l3-cache", X86CPU, enable_l3_cache, true),
    DEFINE_PROP_BOOL("kvm-no-smi-migration", X86CPU, kvm_no_smi_migration,
                     false),
    DEFINE_PROP_BOOL("vmware-cpuid-freq", X86CPU, vmware_cpuid_freq, true),
    DEFINE_PROP_END_OF_LIST()
};

static void x86_cpu_common_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    xcc->parent_realize = dc->realize;
    xcc->parent_unrealize = dc->unrealize;
    dc->realize = x86_cpu_realizefn;
    dc->unrealize = x86_cpu_unrealizefn;
    dc->props = x86_cpu_properties;

    xcc->parent_reset = cc->reset;
    cc->reset = x86_cpu_reset;
    cc->reset_dump_flags = CPU_DUMP_FPU | CPU_DUMP_CCOP;

    cc->class_by_name = x86_cpu_class_by_name;
    cc->parse_features = x86_cpu_parse_featurestr;
    cc->has_work = x86_cpu_has_work;
    cc->do_interrupt = x86_cpu_do_interrupt;
    cc->cpu_exec_interrupt = x86_cpu_exec_interrupt;
    cc->dump_state = x86_cpu_dump_state;
    cc->get_crash_info = x86_cpu_get_crash_info;
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
    /* CPU_NB_REGS * 2 = general regs + xmm regs
     * 25 = eip, eflags, 6 seg regs, st[0-7], fctrl,...,fop, mxcsr.
     */
    cc->gdb_num_core_regs = CPU_NB_REGS * 2 + 25;
#ifndef CONFIG_USER_ONLY
    cc->debug_excp_handler = breakpoint_handler;
#endif
    cc->cpu_exec_enter = x86_cpu_exec_enter;
    cc->cpu_exec_exit = x86_cpu_exec_exit;

    dc->cannot_instantiate_with_device_add_yet = false;
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


/* "base" CPU model, used by query-cpu-model-expansion */
static void x86_cpu_base_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->static_model = true;
    xcc->migration_safe = true;
    xcc->model_description = "base CPU model type with no features enabled";
    xcc->ordering = 8;
}

static const TypeInfo x86_base_cpu_type_info = {
        .name = X86_CPU_TYPE_NAME("base"),
        .parent = TYPE_X86_CPU,
        .class_init = x86_cpu_base_class_init,
};

static void x86_cpu_register_types(void)
{
    int i;

    type_register_static(&x86_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        x86_register_cpudef_type(&builtin_x86_defs[i]);
    }
    type_register_static(&max_x86_cpu_type_info);
    type_register_static(&x86_base_cpu_type_info);
#ifdef CONFIG_KVM
    type_register_static(&host_x86_cpu_type_info);
#endif
}

type_init(x86_cpu_register_types)
