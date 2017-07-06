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

/* These constants must never be used at runtime if kvm_enabled() is false.
 * They exist so we don't need #ifdefs around KVM-specific code that already
 * checks kvm_enabled() properly.
 */
#define KVM_CPUID_FEATURES       0

#endif  /* CONFIG_KVM */

bool kvm_allows_irq0_override(void);
bool kvm_has_smm(void);
bool kvm_has_adjust_clock_stable(void);
void kvm_synchronize_all_tsc(void);
void kvm_arch_reset_vcpu(X86CPU *cs);
void kvm_arch_do_init_vcpu(X86CPU *cs);

int kvm_device_pci_assign(KVMState *s, PCIHostDeviceAddress *dev_addr,
                          uint32_t flags, uint32_t *dev_id);
int kvm_device_pci_deassign(KVMState *s, uint32_t dev_id);

int kvm_device_intx_assign(KVMState *s, uint32_t dev_id,
                           bool use_host_msi, uint32_t guest_irq);
int kvm_device_intx_set_mask(KVMState *s, uint32_t dev_id, bool masked);
int kvm_device_intx_deassign(KVMState *s, uint32_t dev_id, bool use_host_msi);

int kvm_device_msi_assign(KVMState *s, uint32_t dev_id, int virq);
int kvm_device_msi_deassign(KVMState *s, uint32_t dev_id);

bool kvm_device_msix_supported(KVMState *s);
int kvm_device_msix_init_vectors(KVMState *s, uint32_t dev_id,
                                 uint32_t nr_vectors);
int kvm_device_msix_set_vector(KVMState *s, uint32_t dev_id, uint32_t vector,
                               int virq);
int kvm_device_msix_assign(KVMState *s, uint32_t dev_id);
int kvm_device_msix_deassign(KVMState *s, uint32_t dev_id);

void kvm_put_apicbase(X86CPU *cpu, uint64_t value);

bool kvm_enable_x2apic(void);
bool kvm_has_x2apic_api(void);
#endif
