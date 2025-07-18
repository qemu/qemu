/*
 * QEMU TDX support
 *
 * Copyright (c) 2025 Intel Corporation
 *
 * Author:
 *      Xiaoyao Li <xiaoyao.li@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/base64.h"
#include "qemu/mmap-alloc.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qom/object_interfaces.h"
#include "crypto/hash.h"
#include "system/kvm_int.h"
#include "system/runstate.h"
#include "system/system.h"
#include "system/ramblock.h"
#include "system/address-spaces.h"

#include <linux/kvm_para.h>

#include "cpu.h"
#include "cpu-internal.h"
#include "host-cpu.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic-msidef.h"
#include "hw/i386/e820_memory_layout.h"
#include "hw/i386/tdvf.h"
#include "hw/i386/x86.h"
#include "hw/i386/tdvf-hob.h"
#include "hw/pci/msi.h"
#include "kvm_i386.h"
#include "tdx.h"
#include "tdx-quote-generator.h"

#include "standard-headers/asm-x86/kvm_para.h"

#define TDX_MIN_TSC_FREQUENCY_KHZ   (100 * 1000)
#define TDX_MAX_TSC_FREQUENCY_KHZ   (10 * 1000 * 1000)

#define TDX_TD_ATTRIBUTES_DEBUG             BIT_ULL(0)
#define TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE   BIT_ULL(28)
#define TDX_TD_ATTRIBUTES_PKS               BIT_ULL(30)
#define TDX_TD_ATTRIBUTES_PERFMON           BIT_ULL(63)

#define TDX_SUPPORTED_TD_ATTRS  (TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE |\
                                 TDX_TD_ATTRIBUTES_PKS | \
                                 TDX_TD_ATTRIBUTES_PERFMON)

#define TDX_SUPPORTED_KVM_FEATURES  ((1U << KVM_FEATURE_NOP_IO_DELAY) | \
                                     (1U << KVM_FEATURE_PV_UNHALT) | \
                                     (1U << KVM_FEATURE_PV_TLB_FLUSH) | \
                                     (1U << KVM_FEATURE_PV_SEND_IPI) | \
                                     (1U << KVM_FEATURE_POLL_CONTROL) | \
                                     (1U << KVM_FEATURE_PV_SCHED_YIELD) | \
                                     (1U << KVM_FEATURE_MSI_EXT_DEST_ID))

static TdxGuest *tdx_guest;

static struct kvm_tdx_capabilities *tdx_caps;
static struct kvm_cpuid2 *tdx_supported_cpuid;

/* Valid after kvm_arch_init()->confidential_guest_kvm_init()->tdx_kvm_init() */
bool is_tdx_vm(void)
{
    return !!tdx_guest;
}

enum tdx_ioctl_level {
    TDX_VM_IOCTL,
    TDX_VCPU_IOCTL,
};

static int tdx_ioctl_internal(enum tdx_ioctl_level level, void *state,
                              int cmd_id, __u32 flags, void *data,
                              Error **errp)
{
    struct kvm_tdx_cmd tdx_cmd = {};
    int r;

    const char *tdx_ioctl_name[] = {
        [KVM_TDX_CAPABILITIES] = "KVM_TDX_CAPABILITIES",
        [KVM_TDX_INIT_VM] = "KVM_TDX_INIT_VM",
        [KVM_TDX_INIT_VCPU] = "KVM_TDX_INIT_VCPU",
        [KVM_TDX_INIT_MEM_REGION] = "KVM_TDX_INIT_MEM_REGION",
        [KVM_TDX_FINALIZE_VM] = "KVM_TDX_FINALIZE_VM",
        [KVM_TDX_GET_CPUID] = "KVM_TDX_GET_CPUID",
    };

    tdx_cmd.id = cmd_id;
    tdx_cmd.flags = flags;
    tdx_cmd.data = (__u64)(unsigned long)data;

    switch (level) {
    case TDX_VM_IOCTL:
        r = kvm_vm_ioctl(kvm_state, KVM_MEMORY_ENCRYPT_OP, &tdx_cmd);
        break;
    case TDX_VCPU_IOCTL:
        r = kvm_vcpu_ioctl(state, KVM_MEMORY_ENCRYPT_OP, &tdx_cmd);
        break;
    default:
        error_setg(errp, "Invalid tdx_ioctl_level %d", level);
        return -EINVAL;
    }

    if (r < 0) {
        error_setg_errno(errp, -r, "TDX ioctl %s failed, hw_errors: 0x%llx",
                         tdx_ioctl_name[cmd_id], tdx_cmd.hw_error);
    }
    return r;
}

static inline int tdx_vm_ioctl(int cmd_id, __u32 flags, void *data,
                               Error **errp)
{
    return tdx_ioctl_internal(TDX_VM_IOCTL, NULL, cmd_id, flags, data, errp);
}

static inline int tdx_vcpu_ioctl(CPUState *cpu, int cmd_id, __u32 flags,
                                 void *data, Error **errp)
{
    return  tdx_ioctl_internal(TDX_VCPU_IOCTL, cpu, cmd_id, flags, data, errp);
}

static int get_tdx_capabilities(Error **errp)
{
    struct kvm_tdx_capabilities *caps;
    /* 1st generation of TDX reports 6 cpuid configs */
    int nr_cpuid_configs = 6;
    size_t size;
    int r;

    do {
        Error *local_err = NULL;
        size = sizeof(struct kvm_tdx_capabilities) +
                      nr_cpuid_configs * sizeof(struct kvm_cpuid_entry2);
        caps = g_malloc0(size);
        caps->cpuid.nent = nr_cpuid_configs;

        r = tdx_vm_ioctl(KVM_TDX_CAPABILITIES, 0, caps, &local_err);
        if (r == -E2BIG) {
            g_free(caps);
            nr_cpuid_configs *= 2;
            if (nr_cpuid_configs > KVM_MAX_CPUID_ENTRIES) {
                error_report("KVM TDX seems broken that number of CPUID entries"
                             " in kvm_tdx_capabilities exceeds limit: %d",
                             KVM_MAX_CPUID_ENTRIES);
                error_propagate(errp, local_err);
                return r;
            }
            error_free(local_err);
        } else if (r < 0) {
            g_free(caps);
            error_propagate(errp, local_err);
            return r;
        }
    } while (r == -E2BIG);

    tdx_caps = caps;

    return 0;
}

void tdx_set_tdvf_region(MemoryRegion *tdvf_mr)
{
    assert(!tdx_guest->tdvf_mr);
    tdx_guest->tdvf_mr = tdvf_mr;
}

static TdxFirmwareEntry *tdx_get_hob_entry(TdxGuest *tdx)
{
    TdxFirmwareEntry *entry;

    for_each_tdx_fw_entry(&tdx->tdvf, entry) {
        if (entry->type == TDVF_SECTION_TYPE_TD_HOB) {
            return entry;
        }
    }
    error_report("TDVF metadata doesn't specify TD_HOB location.");
    exit(1);
}

static void tdx_add_ram_entry(uint64_t address, uint64_t length,
                              enum TdxRamType type)
{
    uint32_t nr_entries = tdx_guest->nr_ram_entries;
    tdx_guest->ram_entries = g_renew(TdxRamEntry, tdx_guest->ram_entries,
                                     nr_entries + 1);

    tdx_guest->ram_entries[nr_entries].address = address;
    tdx_guest->ram_entries[nr_entries].length = length;
    tdx_guest->ram_entries[nr_entries].type = type;
    tdx_guest->nr_ram_entries++;
}

static int tdx_accept_ram_range(uint64_t address, uint64_t length)
{
    uint64_t head_start, tail_start, head_length, tail_length;
    uint64_t tmp_address, tmp_length;
    TdxRamEntry *e;
    int i = 0;

    do {
        if (i == tdx_guest->nr_ram_entries) {
            return -1;
        }

        e = &tdx_guest->ram_entries[i++];
    } while (address + length <= e->address || address >= e->address + e->length);

    /*
     * The to-be-accepted ram range must be fully contained by one
     * RAM entry.
     */
    if (e->address > address ||
        e->address + e->length < address + length) {
        return -1;
    }

    if (e->type == TDX_RAM_ADDED) {
        return 0;
    }

    tmp_address = e->address;
    tmp_length = e->length;

    e->address = address;
    e->length = length;
    e->type = TDX_RAM_ADDED;

    head_length = address - tmp_address;
    if (head_length > 0) {
        head_start = tmp_address;
        tdx_add_ram_entry(head_start, head_length, TDX_RAM_UNACCEPTED);
    }

    tail_start = address + length;
    if (tail_start < tmp_address + tmp_length) {
        tail_length = tmp_address + tmp_length - tail_start;
        tdx_add_ram_entry(tail_start, tail_length, TDX_RAM_UNACCEPTED);
    }

    return 0;
}

static int tdx_ram_entry_compare(const void *lhs_, const void* rhs_)
{
    const TdxRamEntry *lhs = lhs_;
    const TdxRamEntry *rhs = rhs_;

    if (lhs->address == rhs->address) {
        return 0;
    }
    if (le64_to_cpu(lhs->address) > le64_to_cpu(rhs->address)) {
        return 1;
    }
    return -1;
}

static void tdx_init_ram_entries(void)
{
    unsigned i, j, nr_e820_entries;

    nr_e820_entries = e820_get_table(NULL);
    tdx_guest->ram_entries = g_new(TdxRamEntry, nr_e820_entries);

    for (i = 0, j = 0; i < nr_e820_entries; i++) {
        uint64_t addr, len;

        if (e820_get_entry(i, E820_RAM, &addr, &len)) {
            tdx_guest->ram_entries[j].address = addr;
            tdx_guest->ram_entries[j].length = len;
            tdx_guest->ram_entries[j].type = TDX_RAM_UNACCEPTED;
            j++;
        }
    }
    tdx_guest->nr_ram_entries = j;
}

static void tdx_post_init_vcpus(void)
{
    TdxFirmwareEntry *hob;
    CPUState *cpu;

    hob = tdx_get_hob_entry(tdx_guest);
    CPU_FOREACH(cpu) {
        tdx_vcpu_ioctl(cpu, KVM_TDX_INIT_VCPU, 0, (void *)(uintptr_t)hob->address,
                       &error_fatal);
    }
}

static void tdx_finalize_vm(Notifier *notifier, void *unused)
{
    TdxFirmware *tdvf = &tdx_guest->tdvf;
    TdxFirmwareEntry *entry;
    RAMBlock *ram_block;
    Error *local_err = NULL;
    int r;

    tdx_init_ram_entries();

    for_each_tdx_fw_entry(tdvf, entry) {
        switch (entry->type) {
        case TDVF_SECTION_TYPE_BFV:
        case TDVF_SECTION_TYPE_CFV:
            entry->mem_ptr = tdvf->mem_ptr + entry->data_offset;
            break;
        case TDVF_SECTION_TYPE_TD_HOB:
        case TDVF_SECTION_TYPE_TEMP_MEM:
            entry->mem_ptr = qemu_ram_mmap(-1, entry->size,
                                           qemu_real_host_page_size(), 0, 0);
            if (entry->mem_ptr == MAP_FAILED) {
                error_report("Failed to mmap memory for TDVF section %d",
                             entry->type);
                exit(1);
            }
            if (tdx_accept_ram_range(entry->address, entry->size)) {
                error_report("Failed to accept memory for TDVF section %d",
                             entry->type);
                qemu_ram_munmap(-1, entry->mem_ptr, entry->size);
                exit(1);
            }
            break;
        default:
            error_report("Unsupported TDVF section %d", entry->type);
            exit(1);
        }
    }

    qsort(tdx_guest->ram_entries, tdx_guest->nr_ram_entries,
          sizeof(TdxRamEntry), &tdx_ram_entry_compare);

    tdvf_hob_create(tdx_guest, tdx_get_hob_entry(tdx_guest));

    tdx_post_init_vcpus();

    for_each_tdx_fw_entry(tdvf, entry) {
        struct kvm_tdx_init_mem_region region;
        uint32_t flags;

        region = (struct kvm_tdx_init_mem_region) {
            .source_addr = (uintptr_t)entry->mem_ptr,
            .gpa = entry->address,
            .nr_pages = entry->size >> 12,
        };

        flags = entry->attributes & TDVF_SECTION_ATTRIBUTES_MR_EXTEND ?
                KVM_TDX_MEASURE_MEMORY_REGION : 0;

        do {
            error_free(local_err);
            local_err = NULL;
            r = tdx_vcpu_ioctl(first_cpu, KVM_TDX_INIT_MEM_REGION, flags,
                               &region, &local_err);
        } while (r == -EAGAIN || r == -EINTR);
        if (r < 0) {
            error_report_err(local_err);
            exit(1);
        }

        if (entry->type == TDVF_SECTION_TYPE_TD_HOB ||
            entry->type == TDVF_SECTION_TYPE_TEMP_MEM) {
            qemu_ram_munmap(-1, entry->mem_ptr, entry->size);
            entry->mem_ptr = NULL;
        }
    }

    /*
     * TDVF image has been copied into private region above via
     * KVM_MEMORY_MAPPING. It becomes useless.
     */
    ram_block = tdx_guest->tdvf_mr->ram_block;
    ram_block_discard_range(ram_block, 0, ram_block->max_length);

    tdx_vm_ioctl(KVM_TDX_FINALIZE_VM, 0, NULL, &error_fatal);
    CONFIDENTIAL_GUEST_SUPPORT(tdx_guest)->ready = true;
}

static Notifier tdx_machine_done_notify = {
    .notify = tdx_finalize_vm,
};

/*
 * Some CPUID bits change from fixed1 to configurable bits when TDX module
 * supports TDX_FEATURES0.VE_REDUCTION. e.g., MCA/MCE/MTRR/CORE_CAPABILITY.
 *
 * To make QEMU work with all the versions of TDX module, keep the fixed1 bits
 * here if they are ever fixed1 bits in any of the version though not fixed1 in
 * the latest version. Otherwise, with the older version of TDX module, QEMU may
 * treat the fixed1 bit as unsupported.
 *
 * For newer TDX module, it does no harm to keep them in tdx_fixed1_bits even
 * though they changed to configurable bits. Because tdx_fixed1_bits is used to
 * setup the supported bits.
 */
KvmCpuidInfo tdx_fixed1_bits = {
    .cpuid.nent = 8,
    .entries[0] = {
        .function = 0x1,
        .index = 0,
        .ecx = CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_DTES64 |
               CPUID_EXT_DSCPL | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 |
               CPUID_EXT_PDCM | CPUID_EXT_PCID | CPUID_EXT_SSE41 |
               CPUID_EXT_SSE42 | CPUID_EXT_X2APIC | CPUID_EXT_MOVBE |
               CPUID_EXT_POPCNT | CPUID_EXT_AES | CPUID_EXT_XSAVE |
               CPUID_EXT_RDRAND | CPUID_EXT_HYPERVISOR,
        .edx = CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC |
               CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC |
               CPUID_SEP | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
               CPUID_PAT | CPUID_CLFLUSH | CPUID_DTS | CPUID_MMX | CPUID_FXSR |
               CPUID_SSE | CPUID_SSE2,
    },
    .entries[1] = {
        .function = 0x6,
        .index = 0,
        .eax = CPUID_6_EAX_ARAT,
    },
    .entries[2] = {
        .function = 0x7,
        .index = 0,
        .flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX,
        .ebx = CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_FDP_EXCPTN_ONLY |
               CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_INVPCID |
               CPUID_7_0_EBX_ZERO_FCS_FDS | CPUID_7_0_EBX_RDSEED |
               CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
               CPUID_7_0_EBX_CLWB | CPUID_7_0_EBX_SHA_NI,
        .ecx = CPUID_7_0_ECX_BUS_LOCK_DETECT | CPUID_7_0_ECX_MOVDIRI |
               CPUID_7_0_ECX_MOVDIR64B,
        .edx = CPUID_7_0_EDX_MD_CLEAR | CPUID_7_0_EDX_SPEC_CTRL |
               CPUID_7_0_EDX_STIBP | CPUID_7_0_EDX_FLUSH_L1D |
               CPUID_7_0_EDX_ARCH_CAPABILITIES | CPUID_7_0_EDX_CORE_CAPABILITY |
               CPUID_7_0_EDX_SPEC_CTRL_SSBD,
    },
    .entries[3] = {
        .function = 0x7,
        .index = 2,
        .flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX,
        .edx = CPUID_7_2_EDX_PSFD | CPUID_7_2_EDX_IPRED_CTRL |
               CPUID_7_2_EDX_RRSBA_CTRL | CPUID_7_2_EDX_BHI_CTRL,
    },
    .entries[4] = {
        .function = 0xD,
        .index = 0,
        .flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX,
        .eax = XSTATE_FP_MASK | XSTATE_SSE_MASK,
    },
    .entries[5] = {
        .function = 0xD,
        .index = 1,
        .flags = KVM_CPUID_FLAG_SIGNIFCANT_INDEX,
        .eax = CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC|
               CPUID_XSAVE_XGETBV1 | CPUID_XSAVE_XSAVES,
    },
    .entries[6] = {
        .function = 0x80000001,
        .index = 0,
        .ecx = CPUID_EXT3_LAHF_LM | CPUID_EXT3_ABM | CPUID_EXT3_3DNOWPREFETCH,
        /*
         * Strictly speaking, SYSCALL is not fixed1 bit since it depends on
         * the CPU to be in 64-bit mode. But here fixed1 is used to serve the
         * purpose of supported bits for TDX. In this sense, SYACALL is always
         * supported.
         */
        .edx = CPUID_EXT2_SYSCALL | CPUID_EXT2_NX | CPUID_EXT2_PDPE1GB |
               CPUID_EXT2_RDTSCP | CPUID_EXT2_LM,
    },
    .entries[7] = {
        .function = 0x80000007,
        .index = 0,
        .edx = CPUID_APM_INVTSC,
    },
};

typedef struct TdxAttrsMap {
    uint32_t attr_index;
    uint32_t cpuid_leaf;
    uint32_t cpuid_subleaf;
    int cpuid_reg;
    uint32_t feat_mask;
} TdxAttrsMap;

static TdxAttrsMap tdx_attrs_maps[] = {
    {.attr_index = 27,
     .cpuid_leaf = 7,
     .cpuid_subleaf = 1,
     .cpuid_reg = R_EAX,
     .feat_mask = CPUID_7_1_EAX_LASS,},

    {.attr_index = 30,
     .cpuid_leaf = 7,
     .cpuid_subleaf = 0,
     .cpuid_reg = R_ECX,
     .feat_mask = CPUID_7_0_ECX_PKS,},

    {.attr_index = 31,
     .cpuid_leaf = 7,
     .cpuid_subleaf = 0,
     .cpuid_reg = R_ECX,
     .feat_mask = CPUID_7_0_ECX_KeyLocker,},
};

typedef struct TdxXFAMDep {
    int xfam_bit;
    FeatureMask feat_mask;
} TdxXFAMDep;

/*
 * Note, only the CPUID bits whose virtualization type are "XFAM & Native" are
 * defiend here.
 *
 * For those whose virtualization type are "XFAM & Configured & Native", they
 * are reported as configurable bits. And they are not supported if not in the
 * configureable bits list from KVM even if the corresponding XFAM bit is
 * supported.
 */
TdxXFAMDep tdx_xfam_deps[] = {
    { XSTATE_YMM_BIT,       { FEAT_1_ECX, CPUID_EXT_FMA }},
    { XSTATE_YMM_BIT,       { FEAT_7_0_EBX, CPUID_7_0_EBX_AVX2 }},
    { XSTATE_OPMASK_BIT,    { FEAT_7_0_ECX, CPUID_7_0_ECX_AVX512_VBMI}},
    { XSTATE_OPMASK_BIT,    { FEAT_7_0_EDX, CPUID_7_0_EDX_AVX512_FP16}},
    { XSTATE_PT_BIT,        { FEAT_7_0_EBX, CPUID_7_0_EBX_INTEL_PT}},
    { XSTATE_PKRU_BIT,      { FEAT_7_0_ECX, CPUID_7_0_ECX_PKU}},
    { XSTATE_XTILE_CFG_BIT, { FEAT_7_0_EDX, CPUID_7_0_EDX_AMX_BF16 }},
    { XSTATE_XTILE_CFG_BIT, { FEAT_7_0_EDX, CPUID_7_0_EDX_AMX_TILE }},
    { XSTATE_XTILE_CFG_BIT, { FEAT_7_0_EDX, CPUID_7_0_EDX_AMX_INT8 }},
};

static struct kvm_cpuid_entry2 *find_in_supported_entry(uint32_t function,
                                                        uint32_t index)
{
    struct kvm_cpuid_entry2 *e;

    e = cpuid_find_entry(tdx_supported_cpuid, function, index);
    if (!e) {
        if (tdx_supported_cpuid->nent >= KVM_MAX_CPUID_ENTRIES) {
            error_report("tdx_supported_cpuid requries more space than %d entries",
                          KVM_MAX_CPUID_ENTRIES);
            exit(1);
        }
        e = &tdx_supported_cpuid->entries[tdx_supported_cpuid->nent++];
        e->function = function;
        e->index = index;
    }

    return e;
}

static void tdx_add_supported_cpuid_by_fixed1_bits(void)
{
    struct kvm_cpuid_entry2 *e, *e1;
    int i;

    for (i = 0; i < tdx_fixed1_bits.cpuid.nent; i++) {
        e = &tdx_fixed1_bits.entries[i];

        e1 = find_in_supported_entry(e->function, e->index);
        e1->eax |= e->eax;
        e1->ebx |= e->ebx;
        e1->ecx |= e->ecx;
        e1->edx |= e->edx;
    }
}

static void tdx_add_supported_cpuid_by_attrs(void)
{
    struct kvm_cpuid_entry2 *e;
    TdxAttrsMap *map;
    int i;

    for (i = 0; i < ARRAY_SIZE(tdx_attrs_maps); i++) {
        map = &tdx_attrs_maps[i];
        if (!((1ULL << map->attr_index) & tdx_caps->supported_attrs)) {
            continue;
        }

        e = find_in_supported_entry(map->cpuid_leaf, map->cpuid_subleaf);

        switch(map->cpuid_reg) {
        case R_EAX:
            e->eax |= map->feat_mask;
            break;
        case R_EBX:
            e->ebx |= map->feat_mask;
            break;
        case R_ECX:
            e->ecx |= map->feat_mask;
            break;
        case R_EDX:
            e->edx |= map->feat_mask;
            break;
        }
    }
}

static void tdx_add_supported_cpuid_by_xfam(void)
{
    struct kvm_cpuid_entry2 *e;
    int i;

    const TdxXFAMDep *xfam_dep;
    const FeatureWordInfo *f;
    for (i = 0; i < ARRAY_SIZE(tdx_xfam_deps); i++) {
        xfam_dep = &tdx_xfam_deps[i];
        if (!((1ULL << xfam_dep->xfam_bit) & tdx_caps->supported_xfam)) {
            continue;
        }

        f = &feature_word_info[xfam_dep->feat_mask.index];
        if (f->type != CPUID_FEATURE_WORD) {
            continue;
        }

        e = find_in_supported_entry(f->cpuid.eax, f->cpuid.ecx);
        switch(f->cpuid.reg) {
        case R_EAX:
            e->eax |= xfam_dep->feat_mask.mask;
            break;
        case R_EBX:
            e->ebx |= xfam_dep->feat_mask.mask;
            break;
        case R_ECX:
            e->ecx |= xfam_dep->feat_mask.mask;
            break;
        case R_EDX:
            e->edx |= xfam_dep->feat_mask.mask;
            break;
        }
    }

    e = find_in_supported_entry(0xd, 0);
    e->eax |= (tdx_caps->supported_xfam & CPUID_XSTATE_XCR0_MASK);
    e->edx |= (tdx_caps->supported_xfam & CPUID_XSTATE_XCR0_MASK) >> 32;

    e = find_in_supported_entry(0xd, 1);
    /*
     * Mark XFD always support for TDX, it will be cleared finally in
     * tdx_adjust_cpuid_features() if XFD is unavailable on the hardware
     * because in this case the original data has it as 0.
     */
    e->eax |= CPUID_XSAVE_XFD;
    e->ecx |= (tdx_caps->supported_xfam & CPUID_XSTATE_XSS_MASK);
    e->edx |= (tdx_caps->supported_xfam & CPUID_XSTATE_XSS_MASK) >> 32;
}

static void tdx_add_supported_kvm_features(void)
{
    struct kvm_cpuid_entry2 *e;

    e = find_in_supported_entry(0x40000001, 0);
    e->eax = TDX_SUPPORTED_KVM_FEATURES;
}

static void tdx_setup_supported_cpuid(void)
{
    if (tdx_supported_cpuid) {
        return;
    }

    tdx_supported_cpuid = g_malloc0(sizeof(*tdx_supported_cpuid) +
                    KVM_MAX_CPUID_ENTRIES * sizeof(struct kvm_cpuid_entry2));

    memcpy(tdx_supported_cpuid->entries, tdx_caps->cpuid.entries,
           tdx_caps->cpuid.nent * sizeof(struct kvm_cpuid_entry2));
    tdx_supported_cpuid->nent = tdx_caps->cpuid.nent;

    tdx_add_supported_cpuid_by_fixed1_bits();
    tdx_add_supported_cpuid_by_attrs();
    tdx_add_supported_cpuid_by_xfam();

    tdx_add_supported_kvm_features();
}

static int tdx_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    X86MachineState *x86ms = X86_MACHINE(ms);
    TdxGuest *tdx = TDX_GUEST(cgs);
    int r = 0;

    kvm_mark_guest_state_protected();

    if (x86ms->smm == ON_OFF_AUTO_AUTO) {
        x86ms->smm = ON_OFF_AUTO_OFF;
    } else if (x86ms->smm == ON_OFF_AUTO_ON) {
        error_setg(errp, "TDX VM doesn't support SMM");
        return -EINVAL;
    }

    if (x86ms->pic == ON_OFF_AUTO_AUTO) {
        x86ms->pic = ON_OFF_AUTO_OFF;
    } else if (x86ms->pic == ON_OFF_AUTO_ON) {
        error_setg(errp, "TDX VM doesn't support PIC");
        return -EINVAL;
    }

    if (kvm_state->kernel_irqchip_split == ON_OFF_AUTO_AUTO) {
        kvm_state->kernel_irqchip_split = ON_OFF_AUTO_ON;
    } else if (kvm_state->kernel_irqchip_split != ON_OFF_AUTO_ON) {
        error_setg(errp, "TDX VM requires kernel_irqchip to be split");
        return -EINVAL;
    }

    if (!tdx_caps) {
        r = get_tdx_capabilities(errp);
        if (r) {
            return r;
        }
    }

    tdx_setup_supported_cpuid();

    /* TDX relies on KVM_HC_MAP_GPA_RANGE to handle TDG.VP.VMCALL<MapGPA> */
    if (!kvm_enable_hypercall(BIT_ULL(KVM_HC_MAP_GPA_RANGE))) {
        return -EOPNOTSUPP;
    }

    /*
     * Set kvm_readonly_mem_allowed to false, because TDX only supports readonly
     * memory for shared memory but not for private memory. Besides, whether a
     * memslot is private or shared is not determined by QEMU.
     *
     * Thus, just mark readonly memory not supported for simplicity.
     */
    kvm_readonly_mem_allowed = false;

    qemu_add_machine_init_done_notifier(&tdx_machine_done_notify);

    tdx_guest = tdx;
    return 0;
}

static int tdx_kvm_type(X86ConfidentialGuest *cg)
{
    /* Do the object check */
    TDX_GUEST(cg);

    return KVM_X86_TDX_VM;
}

static void tdx_cpu_instance_init(X86ConfidentialGuest *cg, CPUState *cpu)
{
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);
    X86CPU *x86cpu = X86_CPU(cpu);

    if (xcc->model) {
        error_report("Named cpu model is not supported for TDX yet!");
        exit(1);
    }

    object_property_set_bool(OBJECT(cpu), "pmu", false, &error_abort);

    /* invtsc is fixed1 for TD guest */
    object_property_set_bool(OBJECT(cpu), "invtsc", true, &error_abort);

    x86cpu->force_cpuid_0x1f = true;
}

static uint32_t tdx_adjust_cpuid_features(X86ConfidentialGuest *cg,
                                          uint32_t feature, uint32_t index,
                                          int reg, uint32_t value)
{
    struct kvm_cpuid_entry2 *e;

    e = cpuid_find_entry(&tdx_fixed1_bits.cpuid, feature, index);
    if (e) {
        value |= cpuid_entry_get_reg(e, reg);
    }

    if (is_feature_word_cpuid(feature, index, reg)) {
        e = cpuid_find_entry(tdx_supported_cpuid, feature, index);
        if (e) {
            value &= cpuid_entry_get_reg(e, reg);
        }
    }

    return value;
}

static struct kvm_cpuid2 *tdx_fetch_cpuid(CPUState *cpu, int *ret)
{
    struct kvm_cpuid2 *fetch_cpuid;
    int size = KVM_MAX_CPUID_ENTRIES;
    Error *local_err = NULL;
    int r;

    do {
        error_free(local_err);
        local_err = NULL;

        fetch_cpuid = g_malloc0(sizeof(*fetch_cpuid) +
                                sizeof(struct kvm_cpuid_entry2) * size);
        fetch_cpuid->nent = size;
        r = tdx_vcpu_ioctl(cpu, KVM_TDX_GET_CPUID, 0, fetch_cpuid, &local_err);
        if (r == -E2BIG) {
            g_free(fetch_cpuid);
            size = fetch_cpuid->nent;
        }
    } while (r == -E2BIG);

    if (r < 0) {
        error_report_err(local_err);
        *ret = r;
        return NULL;
    }

    return fetch_cpuid;
}

static int tdx_check_features(X86ConfidentialGuest *cg, CPUState *cs)
{
    uint64_t actual, requested, unavailable, forced_on;
    g_autofree struct kvm_cpuid2 *fetch_cpuid;
    const char *forced_on_prefix = NULL;
    const char *unav_prefix = NULL;
    struct kvm_cpuid_entry2 *entry;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    FeatureWordInfo *wi;
    FeatureWord w;
    bool mismatch = false;
    int r;

    fetch_cpuid = tdx_fetch_cpuid(cs, &r);
    if (!fetch_cpuid) {
        return r;
    }

    if (cpu->check_cpuid || cpu->enforce_cpuid) {
        unav_prefix = "TDX doesn't support requested feature";
        forced_on_prefix = "TDX forcibly sets the feature";
    }

    for (w = 0; w < FEATURE_WORDS; w++) {
        wi = &feature_word_info[w];
        actual = 0;

        switch (wi->type) {
        case CPUID_FEATURE_WORD:
            entry = cpuid_find_entry(fetch_cpuid, wi->cpuid.eax, wi->cpuid.ecx);
            if (!entry) {
                /*
                 * If KVM doesn't report it means it's totally configurable
                 * by QEMU
                 */
                continue;
            }

            actual = cpuid_entry_get_reg(entry, wi->cpuid.reg);
            break;
        case MSR_FEATURE_WORD:
            /*
             * TODO:
             * validate MSR features when KVM has interface report them.
             */
            continue;
        }

        /* Fixup for special cases */
        switch (w) {
        case FEAT_8000_0001_EDX:
            /*
             * Intel enumerates SYSCALL bit as 1 only when processor in 64-bit
             * mode and before vcpu running it's not in 64-bit mode.
             */
            actual |= CPUID_EXT2_SYSCALL;
            break;
        default:
            break;
        }

        requested = env->features[w];
        unavailable = requested & ~actual;
        mark_unavailable_features(cpu, w, unavailable, unav_prefix);
        if (unavailable) {
            mismatch = true;
        }

        forced_on = actual & ~requested;
        mark_forced_on_features(cpu, w, forced_on, forced_on_prefix);
        if (forced_on) {
            mismatch = true;
        }
    }

    if (cpu->enforce_cpuid && mismatch) {
        return -EINVAL;
    }

    if (cpu->phys_bits != host_cpu_phys_bits()) {
        error_report("TDX requires guest CPU physical bits (%u) "
                     "to match host CPU physical bits (%u)",
                     cpu->phys_bits, host_cpu_phys_bits());
        return -EINVAL;
    }

    return 0;
}

static int tdx_validate_attributes(TdxGuest *tdx, Error **errp)
{
    if ((tdx->attributes & ~tdx_caps->supported_attrs)) {
        error_setg(errp, "Invalid attributes 0x%"PRIx64" for TDX VM "
                   "(KVM supported: 0x%"PRIx64")", tdx->attributes,
                   (uint64_t)tdx_caps->supported_attrs);
        return -1;
    }

    if (tdx->attributes & ~TDX_SUPPORTED_TD_ATTRS) {
        error_setg(errp, "Some QEMU unsupported TD attribute bits being "
                    "requested: 0x%"PRIx64" (QEMU supported: 0x%"PRIx64")",
                    tdx->attributes, (uint64_t)TDX_SUPPORTED_TD_ATTRS);
        return -1;
    }

    return 0;
}

static int setup_td_guest_attributes(X86CPU *x86cpu, Error **errp)
{
    CPUX86State *env = &x86cpu->env;

    tdx_guest->attributes |= (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKS) ?
                             TDX_TD_ATTRIBUTES_PKS : 0;
    tdx_guest->attributes |= x86cpu->enable_pmu ? TDX_TD_ATTRIBUTES_PERFMON : 0;

    return tdx_validate_attributes(tdx_guest, errp);
}

static int setup_td_xfam(X86CPU *x86cpu, Error **errp)
{
    CPUX86State *env = &x86cpu->env;
    uint64_t xfam;

    xfam = env->features[FEAT_XSAVE_XCR0_LO] |
           env->features[FEAT_XSAVE_XCR0_HI] |
           env->features[FEAT_XSAVE_XSS_LO] |
           env->features[FEAT_XSAVE_XSS_HI];

    if (xfam & ~tdx_caps->supported_xfam) {
        error_setg(errp, "Invalid XFAM 0x%"PRIx64" for TDX VM (supported: 0x%"PRIx64"))",
                   xfam, (uint64_t)tdx_caps->supported_xfam);
        return -1;
    }

    tdx_guest->xfam = xfam;
    return 0;
}

static void tdx_filter_cpuid(struct kvm_cpuid2 *cpuids)
{
    int i, dest_cnt = 0;
    struct kvm_cpuid_entry2 *src, *dest, *conf;

    for (i = 0; i < cpuids->nent; i++) {
        src = cpuids->entries + i;
        conf = cpuid_find_entry(&tdx_caps->cpuid, src->function, src->index);
        if (!conf) {
            continue;
        }
        dest = cpuids->entries + dest_cnt;

        dest->function = src->function;
        dest->index = src->index;
        dest->flags = src->flags;
        dest->eax = src->eax & conf->eax;
        dest->ebx = src->ebx & conf->ebx;
        dest->ecx = src->ecx & conf->ecx;
        dest->edx = src->edx & conf->edx;

        dest_cnt++;
    }
    cpuids->nent = dest_cnt++;
}

int tdx_pre_create_vcpu(CPUState *cpu, Error **errp)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    CPUX86State *env = &x86cpu->env;
    g_autofree struct kvm_tdx_init_vm *init_vm = NULL;
    Error *local_err = NULL;
    size_t data_len;
    int retry = 10000;
    int r = 0;

    QEMU_LOCK_GUARD(&tdx_guest->lock);
    if (tdx_guest->initialized) {
        return r;
    }

    init_vm = g_malloc0(sizeof(struct kvm_tdx_init_vm) +
                        sizeof(struct kvm_cpuid_entry2) * KVM_MAX_CPUID_ENTRIES);

    if (!kvm_check_extension(kvm_state, KVM_CAP_X86_APIC_BUS_CYCLES_NS)) {
        error_setg(errp, "KVM doesn't support KVM_CAP_X86_APIC_BUS_CYCLES_NS");
        return -EOPNOTSUPP;
    }

    r = kvm_vm_enable_cap(kvm_state, KVM_CAP_X86_APIC_BUS_CYCLES_NS,
                          0, TDX_APIC_BUS_CYCLES_NS);
    if (r < 0) {
        error_setg_errno(errp, -r,
                         "Unable to set core crystal clock frequency to 25MHz");
        return r;
    }

    if (env->tsc_khz && (env->tsc_khz < TDX_MIN_TSC_FREQUENCY_KHZ ||
                         env->tsc_khz > TDX_MAX_TSC_FREQUENCY_KHZ)) {
        error_setg(errp, "Invalid TSC %"PRId64" KHz, must specify cpu_frequency "
                         "between [%d, %d] kHz", env->tsc_khz,
                         TDX_MIN_TSC_FREQUENCY_KHZ, TDX_MAX_TSC_FREQUENCY_KHZ);
       return -EINVAL;
    }

    if (env->tsc_khz % (25 * 1000)) {
        error_setg(errp, "Invalid TSC %"PRId64" KHz, it must be multiple of 25MHz",
                   env->tsc_khz);
        return -EINVAL;
    }

    /* it's safe even env->tsc_khz is 0. KVM uses host's tsc_khz in this case */
    r = kvm_vm_ioctl(kvm_state, KVM_SET_TSC_KHZ, env->tsc_khz);
    if (r < 0) {
        error_setg_errno(errp, -r, "Unable to set TSC frequency to %"PRId64" kHz",
                         env->tsc_khz);
        return r;
    }

    if (tdx_guest->mrconfigid) {
        g_autofree uint8_t *data = qbase64_decode(tdx_guest->mrconfigid,
                              strlen(tdx_guest->mrconfigid), &data_len, errp);
        if (!data) {
            return -1;
        }
        if (data_len != QCRYPTO_HASH_DIGEST_LEN_SHA384) {
            error_setg(errp, "TDX 'mrconfigid' sha384 digest was %ld bytes, "
                             "expected %d bytes", data_len,
                             QCRYPTO_HASH_DIGEST_LEN_SHA384);
            return -1;
        }
        memcpy(init_vm->mrconfigid, data, data_len);
    }

    if (tdx_guest->mrowner) {
        g_autofree uint8_t *data = qbase64_decode(tdx_guest->mrowner,
                              strlen(tdx_guest->mrowner), &data_len, errp);
        if (!data) {
            return -1;
        }
        if (data_len != QCRYPTO_HASH_DIGEST_LEN_SHA384) {
            error_setg(errp, "TDX 'mrowner' sha384 digest was %ld bytes, "
                             "expected %d bytes", data_len,
                             QCRYPTO_HASH_DIGEST_LEN_SHA384);
            return -1;
        }
        memcpy(init_vm->mrowner, data, data_len);
    }

    if (tdx_guest->mrownerconfig) {
        g_autofree uint8_t *data = qbase64_decode(tdx_guest->mrownerconfig,
                            strlen(tdx_guest->mrownerconfig), &data_len, errp);
        if (!data) {
            return -1;
        }
        if (data_len != QCRYPTO_HASH_DIGEST_LEN_SHA384) {
            error_setg(errp, "TDX 'mrownerconfig' sha384 digest was %ld bytes, "
                             "expected %d bytes", data_len,
                             QCRYPTO_HASH_DIGEST_LEN_SHA384);
            return -1;
        }
        memcpy(init_vm->mrownerconfig, data, data_len);
    }

    r = setup_td_guest_attributes(x86cpu, errp);
    if (r) {
        return r;
    }

    r = setup_td_xfam(x86cpu, errp);
    if (r) {
        return r;
    }

    init_vm->cpuid.nent = kvm_x86_build_cpuid(env, init_vm->cpuid.entries, 0);
    tdx_filter_cpuid(&init_vm->cpuid);

    init_vm->attributes = tdx_guest->attributes;
    init_vm->xfam = tdx_guest->xfam;

    /*
     * KVM_TDX_INIT_VM gets -EAGAIN when KVM side SEAMCALL(TDH_MNG_CREATE)
     * gets TDX_RND_NO_ENTROPY due to Random number generation (e.g., RDRAND or
     * RDSEED) is busy.
     *
     * Retry for the case.
     */
    do {
        error_free(local_err);
        local_err = NULL;
        r = tdx_vm_ioctl(KVM_TDX_INIT_VM, 0, init_vm, &local_err);
    } while (r == -EAGAIN && --retry);

    if (r < 0) {
        if (!retry) {
            error_append_hint(&local_err, "Hardware RNG (Random Number "
            "Generator) is busy occupied by someone (via RDRAND/RDSEED) "
            "maliciously, which leads to KVM_TDX_INIT_VM keeping failure "
            "due to lack of entropy.\n");
        }
        error_propagate(errp, local_err);
        return r;
    }

    tdx_guest->initialized = true;

    return 0;
}

int tdx_parse_tdvf(void *flash_ptr, int size)
{
    return tdvf_parse_metadata(&tdx_guest->tdvf, flash_ptr, size);
}

static void tdx_inject_interrupt(TdxGuest *tdx)
{
    int ret;
    uint32_t apicid, vector;

    qemu_mutex_lock(&tdx->lock);
    vector = tdx->event_notify_vector;
    apicid = tdx->event_notify_apicid;
    qemu_mutex_unlock(&tdx->lock);
    if (vector < 32 || vector > 255) {
        return;
    }

    MSIMessage msg = {
        .address = ((apicid & 0xff) << MSI_ADDR_DEST_ID_SHIFT) |
                   (((uint64_t)apicid & 0xffffff00) << 32),
        .data = vector | (APIC_DM_FIXED << MSI_DATA_DELIVERY_MODE_SHIFT),
    };

    ret = kvm_irqchip_send_msi(kvm_state, msg);
    if (ret < 0) {
        /* In this case, no better way to tell it to guest. Log it. */
        error_report("TDX: injection interrupt %d failed, interrupt lost (%s).",
                     vector, strerror(-ret));
    }
}

static void tdx_get_quote_completion(TdxGenerateQuoteTask *task)
{
    TdxGuest *tdx = task->opaque;
    int ret;

    /* Maintain the number of in-flight requests. */
    qemu_mutex_lock(&tdx->lock);
    tdx->num--;
    qemu_mutex_unlock(&tdx->lock);

    if (task->status_code == TDX_VP_GET_QUOTE_SUCCESS) {
        ret = address_space_write(&address_space_memory, task->payload_gpa,
                                  MEMTXATTRS_UNSPECIFIED, task->receive_buf,
                                  task->receive_buf_received);
        if (ret != MEMTX_OK) {
            error_report("TDX: get-quote: failed to write quote data.");
        } else {
            task->hdr.out_len = cpu_to_le64(task->receive_buf_received);
        }
    }
    task->hdr.error_code = cpu_to_le64(task->status_code);

    /* Publish the response contents before marking this request completed. */
    smp_wmb();
    ret = address_space_write(&address_space_memory, task->buf_gpa,
                              MEMTXATTRS_UNSPECIFIED, &task->hdr,
                              TDX_GET_QUOTE_HDR_SIZE);
    if (ret != MEMTX_OK) {
        error_report("TDX: get-quote: failed to update GetQuote header.");
    }

    tdx_inject_interrupt(tdx);

    g_free(task->send_data);
    g_free(task->receive_buf);
    g_free(task);
    object_unref(tdx);
}

void tdx_handle_get_quote(X86CPU *cpu, struct kvm_run *run)
{
    TdxGenerateQuoteTask *task;
    struct tdx_get_quote_header hdr;
    hwaddr buf_gpa = run->tdx.get_quote.gpa;
    uint64_t buf_len = run->tdx.get_quote.size;

    QEMU_BUILD_BUG_ON(sizeof(struct tdx_get_quote_header) != TDX_GET_QUOTE_HDR_SIZE);

    run->tdx.get_quote.ret = TDG_VP_VMCALL_INVALID_OPERAND;

    if (buf_len == 0) {
        return;
    }

    if (!QEMU_IS_ALIGNED(buf_gpa, 4096) || !QEMU_IS_ALIGNED(buf_len, 4096)) {
        run->tdx.get_quote.ret = TDG_VP_VMCALL_ALIGN_ERROR;
        return;
    }

    if (address_space_read(&address_space_memory, buf_gpa, MEMTXATTRS_UNSPECIFIED,
                           &hdr, TDX_GET_QUOTE_HDR_SIZE) != MEMTX_OK) {
        error_report("TDX: get-quote: failed to read GetQuote header.");
        return;
    }

    if (le64_to_cpu(hdr.structure_version) != TDX_GET_QUOTE_STRUCTURE_VERSION) {
        return;
    }

    /* Only safe-guard check to avoid too large buffer size. */
    if (buf_len > TDX_GET_QUOTE_MAX_BUF_LEN ||
        le32_to_cpu(hdr.in_len) > buf_len - TDX_GET_QUOTE_HDR_SIZE) {
        return;
    }

    if (!tdx_guest->qg_sock_addr) {
        hdr.error_code = cpu_to_le64(TDX_VP_GET_QUOTE_QGS_UNAVAILABLE);
        if (address_space_write(&address_space_memory, buf_gpa,
                                MEMTXATTRS_UNSPECIFIED,
                                &hdr, TDX_GET_QUOTE_HDR_SIZE) != MEMTX_OK) {
            error_report("TDX: failed to update GetQuote header.");
            return;
        }
        run->tdx.get_quote.ret = TDG_VP_VMCALL_SUCCESS;
        return;
    }

    qemu_mutex_lock(&tdx_guest->lock);
    if (tdx_guest->num >= TDX_MAX_GET_QUOTE_REQUEST) {
        qemu_mutex_unlock(&tdx_guest->lock);
        run->tdx.get_quote.ret = TDG_VP_VMCALL_RETRY;
        return;
    }
    tdx_guest->num++;
    qemu_mutex_unlock(&tdx_guest->lock);

    task = g_new(TdxGenerateQuoteTask, 1);
    task->buf_gpa = buf_gpa;
    task->payload_gpa = buf_gpa + TDX_GET_QUOTE_HDR_SIZE;
    task->payload_len = buf_len - TDX_GET_QUOTE_HDR_SIZE;
    task->hdr = hdr;
    task->completion = tdx_get_quote_completion;

    task->send_data_size = le32_to_cpu(hdr.in_len);
    task->send_data = g_malloc(task->send_data_size);
    task->send_data_sent = 0;

    if (address_space_read(&address_space_memory, task->payload_gpa,
                           MEMTXATTRS_UNSPECIFIED, task->send_data,
                           task->send_data_size) != MEMTX_OK) {
        goto out_free;
    }

    /* Mark the buffer in-flight. */
    hdr.error_code = cpu_to_le64(TDX_VP_GET_QUOTE_IN_FLIGHT);
    if (address_space_write(&address_space_memory, buf_gpa,
                            MEMTXATTRS_UNSPECIFIED,
                            &hdr, TDX_GET_QUOTE_HDR_SIZE) != MEMTX_OK) {
        goto out_free;
    }

    task->receive_buf = g_malloc0(task->payload_len);
    task->receive_buf_received = 0;
    task->opaque = tdx_guest;

    object_ref(tdx_guest);
    tdx_generate_quote(task, tdx_guest->qg_sock_addr);
    run->tdx.get_quote.ret = TDG_VP_VMCALL_SUCCESS;
    return;

out_free:
    g_free(task->send_data);
    g_free(task);
}

#define SUPPORTED_TDVMCALLINFO_1_R11    (TDG_VP_VMCALL_SUBFUNC_SET_EVENT_NOTIFY_INTERRUPT)
#define SUPPORTED_TDVMCALLINFO_1_R12    (0)

void tdx_handle_get_tdvmcall_info(X86CPU *cpu, struct kvm_run *run)
{
    if (run->tdx.get_tdvmcall_info.leaf != 1) {
        return;
    }

    run->tdx.get_tdvmcall_info.r11 = (tdx_caps->user_tdvmcallinfo_1_r11 &
                                      SUPPORTED_TDVMCALLINFO_1_R11) |
                                      tdx_caps->kernel_tdvmcallinfo_1_r11;
    run->tdx.get_tdvmcall_info.r12 = (tdx_caps->user_tdvmcallinfo_1_r12 &
                                      SUPPORTED_TDVMCALLINFO_1_R12) |
                                      tdx_caps->kernel_tdvmcallinfo_1_r12;
    run->tdx.get_tdvmcall_info.r13 = 0;
    run->tdx.get_tdvmcall_info.r14 = 0;

    run->tdx.get_tdvmcall_info.ret = TDG_VP_VMCALL_SUCCESS;
}

void tdx_handle_setup_event_notify_interrupt(X86CPU *cpu, struct kvm_run *run)
{
    uint64_t vector = run->tdx.setup_event_notify.vector;

    if (vector >= 32 && vector < 256) {
        qemu_mutex_lock(&tdx_guest->lock);
        tdx_guest->event_notify_vector = vector;
        tdx_guest->event_notify_apicid = cpu->apic_id;
        qemu_mutex_unlock(&tdx_guest->lock);
        run->tdx.setup_event_notify.ret = TDG_VP_VMCALL_SUCCESS;
    } else {
        run->tdx.setup_event_notify.ret = TDG_VP_VMCALL_INVALID_OPERAND;
    }
}

static void tdx_panicked_on_fatal_error(X86CPU *cpu, uint64_t error_code,
                                        char *message, bool has_gpa,
                                        uint64_t gpa)
{
    GuestPanicInformation *panic_info;

    panic_info = g_new0(GuestPanicInformation, 1);
    panic_info->type = GUEST_PANIC_INFORMATION_TYPE_TDX;
    panic_info->u.tdx.error_code = (uint32_t) error_code;
    panic_info->u.tdx.message = message;
    panic_info->u.tdx.gpa = gpa;
    panic_info->u.tdx.has_gpa = has_gpa;

    qemu_system_guest_panicked(panic_info);
}

/*
 * Only 8 registers can contain valid ASCII byte stream to form the fatal
 * message, and their sequence is: R14, R15, RBX, RDI, RSI, R8, R9, RDX
 */
#define TDX_FATAL_MESSAGE_MAX        64

#define TDX_REPORT_FATAL_ERROR_GPA_VALID    BIT_ULL(63)

int tdx_handle_report_fatal_error(X86CPU *cpu, struct kvm_run *run)
{
    uint64_t error_code = run->system_event.data[R_R12];
    uint64_t reg_mask = run->system_event.data[R_ECX];
    char *message = NULL;
    uint64_t *tmp;
    uint64_t gpa = -1ull;
    bool has_gpa = false;

    if (error_code & 0xffff) {
        error_report("TDX: REPORT_FATAL_ERROR: invalid error code: 0x%"PRIx64,
                     error_code);
        return -1;
    }

    if (reg_mask) {
        message = g_malloc0(TDX_FATAL_MESSAGE_MAX + 1);
        tmp = (uint64_t *)message;

#define COPY_REG(REG)                               \
    do {                                            \
        if (reg_mask & BIT_ULL(REG)) {              \
            *(tmp++) = run->system_event.data[REG]; \
        }                                           \
    } while (0)

        COPY_REG(R_R14);
        COPY_REG(R_R15);
        COPY_REG(R_EBX);
        COPY_REG(R_EDI);
        COPY_REG(R_ESI);
        COPY_REG(R_R8);
        COPY_REG(R_R9);
        COPY_REG(R_EDX);
        *((char *)tmp) = '\0';
    }
#undef COPY_REG

    if (error_code & TDX_REPORT_FATAL_ERROR_GPA_VALID) {
        gpa = run->system_event.data[R_R13];
        has_gpa = true;
    }

    tdx_panicked_on_fatal_error(cpu, error_code, message, has_gpa, gpa);

    return -1;
}

static bool tdx_guest_get_sept_ve_disable(Object *obj, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    return !!(tdx->attributes & TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE);
}

static void tdx_guest_set_sept_ve_disable(Object *obj, bool value, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    if (value) {
        tdx->attributes |= TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE;
    } else {
        tdx->attributes &= ~TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE;
    }
}

static char *tdx_guest_get_mrconfigid(Object *obj, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    return g_strdup(tdx->mrconfigid);
}

static void tdx_guest_set_mrconfigid(Object *obj, const char *value, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    g_free(tdx->mrconfigid);
    tdx->mrconfigid = g_strdup(value);
}

static char *tdx_guest_get_mrowner(Object *obj, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    return g_strdup(tdx->mrowner);
}

static void tdx_guest_set_mrowner(Object *obj, const char *value, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    g_free(tdx->mrowner);
    tdx->mrowner = g_strdup(value);
}

static char *tdx_guest_get_mrownerconfig(Object *obj, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    return g_strdup(tdx->mrownerconfig);
}

static void tdx_guest_set_mrownerconfig(Object *obj, const char *value, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    g_free(tdx->mrownerconfig);
    tdx->mrownerconfig = g_strdup(value);
}

static void tdx_guest_get_qgs(Object *obj, Visitor *v,
                              const char *name, void *opaque,
                              Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);

    if (!tdx->qg_sock_addr) {
        error_setg(errp, "quote-generation-socket is not set");
        return;
    }
    visit_type_SocketAddress(v, name, &tdx->qg_sock_addr, errp);
}

static void tdx_guest_set_qgs(Object *obj, Visitor *v,
                              const char *name, void *opaque,
                              Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(obj);
    SocketAddress *sock = NULL;

    if (!visit_type_SocketAddress(v, name, &sock, errp)) {
        return;
    }

    if (tdx->qg_sock_addr) {
        qapi_free_SocketAddress(tdx->qg_sock_addr);
    }

    tdx->qg_sock_addr = sock;
}

/* tdx guest */
OBJECT_DEFINE_TYPE_WITH_INTERFACES(TdxGuest,
                                   tdx_guest,
                                   TDX_GUEST,
                                   X86_CONFIDENTIAL_GUEST,
                                   { TYPE_USER_CREATABLE },
                                   { NULL })

static void tdx_guest_init(Object *obj)
{
    ConfidentialGuestSupport *cgs = CONFIDENTIAL_GUEST_SUPPORT(obj);
    TdxGuest *tdx = TDX_GUEST(obj);

    qemu_mutex_init(&tdx->lock);

    cgs->require_guest_memfd = true;
    tdx->attributes = TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE;

    object_property_add_uint64_ptr(obj, "attributes", &tdx->attributes,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_bool(obj, "sept-ve-disable",
                             tdx_guest_get_sept_ve_disable,
                             tdx_guest_set_sept_ve_disable);
    object_property_add_str(obj, "mrconfigid",
                            tdx_guest_get_mrconfigid,
                            tdx_guest_set_mrconfigid);
    object_property_add_str(obj, "mrowner",
                            tdx_guest_get_mrowner, tdx_guest_set_mrowner);
    object_property_add_str(obj, "mrownerconfig",
                            tdx_guest_get_mrownerconfig,
                            tdx_guest_set_mrownerconfig);

    object_property_add(obj, "quote-generation-socket", "SocketAddress",
                            tdx_guest_get_qgs,
                            tdx_guest_set_qgs,
                            NULL, NULL);

    tdx->event_notify_vector = -1;
    tdx->event_notify_apicid = -1;
}

static void tdx_guest_finalize(Object *obj)
{
}

static void tdx_guest_class_init(ObjectClass *oc, const void *data)
{
    ConfidentialGuestSupportClass *klass = CONFIDENTIAL_GUEST_SUPPORT_CLASS(oc);
    X86ConfidentialGuestClass *x86_klass = X86_CONFIDENTIAL_GUEST_CLASS(oc);

    klass->kvm_init = tdx_kvm_init;
    x86_klass->kvm_type = tdx_kvm_type;
    x86_klass->cpu_instance_init = tdx_cpu_instance_init;
    x86_klass->adjust_cpuid_features = tdx_adjust_cpuid_features;
    x86_klass->check_features = tdx_check_features;
}
