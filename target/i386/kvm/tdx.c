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
#include "qom/object_interfaces.h"
#include "crypto/hash.h"
#include "system/system.h"
#include "system/ramblock.h"

#include "hw/i386/e820_memory_layout.h"
#include "hw/i386/tdvf.h"
#include "hw/i386/x86.h"
#include "hw/i386/tdvf-hob.h"
#include "kvm_i386.h"
#include "tdx.h"

#define TDX_MIN_TSC_FREQUENCY_KHZ   (100 * 1000)
#define TDX_MAX_TSC_FREQUENCY_KHZ   (10 * 1000 * 1000)

#define TDX_TD_ATTRIBUTES_DEBUG             BIT_ULL(0)
#define TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE   BIT_ULL(28)
#define TDX_TD_ATTRIBUTES_PKS               BIT_ULL(30)
#define TDX_TD_ATTRIBUTES_PERFMON           BIT_ULL(63)

#define TDX_SUPPORTED_TD_ATTRS  (TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE |\
                                 TDX_TD_ATTRIBUTES_PKS | \
                                 TDX_TD_ATTRIBUTES_PERFMON)

static TdxGuest *tdx_guest;

static struct kvm_tdx_capabilities *tdx_caps;

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

    for_each_tdx_fw_entry(tdvf, entry) {
        struct kvm_tdx_init_mem_region region;
        uint32_t flags;

        region = (struct kvm_tdx_init_mem_region) {
            .source_addr = (uint64_t)entry->mem_ptr,
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
}

static Notifier tdx_machine_done_notify = {
    .notify = tdx_finalize_vm,
};

static int tdx_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    TdxGuest *tdx = TDX_GUEST(cgs);
    int r = 0;

    kvm_mark_guest_state_protected();

    if (!tdx_caps) {
        r = get_tdx_capabilities(errp);
        if (r) {
            return r;
        }
    }

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

static int tdx_validate_attributes(TdxGuest *tdx, Error **errp)
{
    if ((tdx->attributes & ~tdx_caps->supported_attrs)) {
        error_setg(errp, "Invalid attributes 0x%lx for TDX VM "
                   "(KVM supported: 0x%llx)", tdx->attributes,
                   tdx_caps->supported_attrs);
        return -1;
    }

    if (tdx->attributes & ~TDX_SUPPORTED_TD_ATTRS) {
        error_setg(errp, "Some QEMU unsupported TD attribute bits being "
                    "requested: 0x%lx (QEMU supported: 0x%llx)",
                    tdx->attributes, TDX_SUPPORTED_TD_ATTRS);
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
        error_setg(errp, "Invalid XFAM 0x%lx for TDX VM (supported: 0x%llx))",
                   xfam, tdx_caps->supported_xfam);
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
        error_setg(errp, "Invalid TSC %ld KHz, must specify cpu_frequency "
                         "between [%d, %d] kHz", env->tsc_khz,
                         TDX_MIN_TSC_FREQUENCY_KHZ, TDX_MAX_TSC_FREQUENCY_KHZ);
       return -EINVAL;
    }

    if (env->tsc_khz % (25 * 1000)) {
        error_setg(errp, "Invalid TSC %ld KHz, it must be multiple of 25MHz",
                   env->tsc_khz);
        return -EINVAL;
    }

    /* it's safe even env->tsc_khz is 0. KVM uses host's tsc_khz in this case */
    r = kvm_vm_ioctl(kvm_state, KVM_SET_TSC_KHZ, env->tsc_khz);
    if (r < 0) {
        error_setg_errno(errp, -r, "Unable to set TSC frequency to %ld kHz",
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
            error_setg(errp, "TDX: failed to decode mrconfigid");
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
            error_setg(errp, "TDX: failed to decode mrowner");
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
            error_setg(errp, "TDX: failed to decode mrownerconfig");
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
}
