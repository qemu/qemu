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
#include "qapi/error.h"
#include "qom/object_interfaces.h"

#include "hw/i386/x86.h"
#include "kvm_i386.h"
#include "tdx.h"

#define TDX_TD_ATTRIBUTES_SEPT_VE_DISABLE   BIT_ULL(28)
#define TDX_TD_ATTRIBUTES_PKS               BIT_ULL(30)
#define TDX_TD_ATTRIBUTES_PERFMON           BIT_ULL(63)

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

    tdx_guest = tdx;
    return 0;
}

static int tdx_kvm_type(X86ConfidentialGuest *cg)
{
    /* Do the object check */
    TDX_GUEST(cg);

    return KVM_X86_TDX_VM;
}

static void setup_td_guest_attributes(X86CPU *x86cpu)
{
    CPUX86State *env = &x86cpu->env;

    tdx_guest->attributes |= (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKS) ?
                             TDX_TD_ATTRIBUTES_PKS : 0;
    tdx_guest->attributes |= x86cpu->enable_pmu ? TDX_TD_ATTRIBUTES_PERFMON : 0;
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
    int retry = 10000;
    int r = 0;

    QEMU_LOCK_GUARD(&tdx_guest->lock);
    if (tdx_guest->initialized) {
        return r;
    }

    init_vm = g_malloc0(sizeof(struct kvm_tdx_init_vm) +
                        sizeof(struct kvm_cpuid_entry2) * KVM_MAX_CPUID_ENTRIES);

    setup_td_guest_attributes(x86cpu);

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
