/*
 * QEMU KVM support -- x86 specific functions.
 *
 * Copyright (c) 2012 Linaro Limited
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_KVM_I386_H
#define QEMU_KVM_I386_H

#include "sysemu/kvm.h"

#define kvm_apic_in_kernel() (kvm_irqchip_in_kernel())

#ifdef CONFIG_KVM

#define kvm_pit_in_kernel() \
    (kvm_irqchip_in_kernel() && !kvm_irqchip_is_split())
#define kvm_pic_in_kernel()  \
    (kvm_irqchip_in_kernel() && !kvm_irqchip_is_split())
#define kvm_ioapic_in_kernel() \
    (kvm_irqchip_in_kernel() && !kvm_irqchip_is_split())

#else

#define kvm_pit_in_kernel()      0
#define kvm_pic_in_kernel()      0
#define kvm_ioapic_in_kernel()   0

#endif  /* CONFIG_KVM */

bool kvm_has_smm(void);
bool kvm_has_adjust_clock(void);
bool kvm_has_adjust_clock_stable(void);
bool kvm_has_exception_payload(void);
void kvm_synchronize_all_tsc(void);
void kvm_arch_reset_vcpu(X86CPU *cs);
void kvm_arch_do_init_vcpu(X86CPU *cs);

void kvm_put_apicbase(X86CPU *cpu, uint64_t value);

bool kvm_enable_x2apic(void);
bool kvm_has_x2apic_api(void);
bool kvm_has_waitpkg(void);

bool kvm_hv_vpindex_settable(void);
bool kvm_hyperv_expand_features(X86CPU *cpu, Error **errp);

uint64_t kvm_swizzle_msi_ext_dest_id(uint64_t address);

bool kvm_enable_sgx_provisioning(KVMState *s);
void kvm_request_xsave_components(X86CPU *cpu, uint64_t mask);

#endif
