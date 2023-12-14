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

#ifndef QEMU_I386_KVM_XEN_EMU_H
#define QEMU_I386_KVM_XEN_EMU_H

#define XEN_HYPERCALL_MSR               0x40000000
#define XEN_HYPERCALL_MSR_HYPERV        0x40000200

#define XEN_CPUID_SIGNATURE        0
#define XEN_CPUID_VENDOR           1
#define XEN_CPUID_HVM_MSR          2
#define XEN_CPUID_TIME             3
#define XEN_CPUID_HVM              4

#define XEN_VERSION(maj, min) ((maj) << 16 | (min))

int kvm_xen_init(KVMState *s, uint32_t hypercall_msr);
int kvm_xen_init_vcpu(CPUState *cs);
int kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit);
int kvm_put_xen_state(CPUState *cs);
int kvm_get_xen_state(CPUState *cs);
void kvm_xen_maybe_deassert_callback(CPUState *cs);

#endif /* QEMU_I386_KVM_XEN_EMU_H */
