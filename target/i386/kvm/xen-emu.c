/*
 * Xen HVM emulation support in KVM
 *
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/kvm_int.h"
#include "sysemu/kvm_xen.h"
#include "kvm/kvm_i386.h"
#include "xen-emu.h"

int kvm_xen_init(KVMState *s, uint32_t hypercall_msr)
{
    const int required_caps = KVM_XEN_HVM_CONFIG_HYPERCALL_MSR |
        KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL | KVM_XEN_HVM_CONFIG_SHARED_INFO;
    struct kvm_xen_hvm_config cfg = {
        .msr = hypercall_msr,
        .flags = KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL,
    };
    int xen_caps, ret;

    xen_caps = kvm_check_extension(s, KVM_CAP_XEN_HVM);
    if (required_caps & ~xen_caps) {
        error_report("kvm: Xen HVM guest support not present or insufficient");
        return -ENOSYS;
    }

    if (xen_caps & KVM_XEN_HVM_CONFIG_EVTCHN_SEND) {
        struct kvm_xen_hvm_attr ha = {
            .type = KVM_XEN_ATTR_TYPE_XEN_VERSION,
            .u.xen_version = s->xen_version,
        };
        (void)kvm_vm_ioctl(s, KVM_XEN_HVM_SET_ATTR, &ha);

        cfg.flags |= KVM_XEN_HVM_CONFIG_EVTCHN_SEND;
    }

    ret = kvm_vm_ioctl(s, KVM_XEN_HVM_CONFIG, &cfg);
    if (ret < 0) {
        error_report("kvm: Failed to enable Xen HVM support: %s",
                     strerror(-ret));
        return ret;
    }

    s->xen_caps = xen_caps;
    return 0;
}

int kvm_xen_init_vcpu(CPUState *cs)
{
    int err;

    /*
     * The kernel needs to know the Xen/ACPI vCPU ID because that's
     * what the guest uses in hypercalls such as timers. It doesn't
     * match the APIC ID which is generally used for talking to the
     * kernel about vCPUs. And if vCPU threads race with creating
     * their KVM vCPUs out of order, it doesn't necessarily match
     * with the kernel's internal vCPU indices either.
     */
    if (kvm_xen_has_cap(EVTCHN_SEND)) {
        struct kvm_xen_vcpu_attr va = {
            .type = KVM_XEN_VCPU_ATTR_TYPE_VCPU_ID,
            .u.vcpu_id = cs->cpu_index,
        };
        err = kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_SET_ATTR, &va);
        if (err) {
            error_report("kvm: Failed to set Xen vCPU ID attribute: %s",
                         strerror(-err));
            return err;
        }
    }

    return 0;
}

uint32_t kvm_xen_get_caps(void)
{
    return kvm_state->xen_caps;
}
