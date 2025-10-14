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

#include "system/kvm.h"

#define KVM_MAX_CPUID_ENTRIES  100

/* always false if !CONFIG_KVM */
#define kvm_pit_in_kernel() \
    (kvm_irqchip_in_kernel() && !kvm_irqchip_is_split())
#define kvm_pic_in_kernel()  \
    (kvm_irqchip_in_kernel() && !kvm_irqchip_is_split())
#define kvm_ioapic_in_kernel() \
    (kvm_irqchip_in_kernel() && !kvm_irqchip_is_split())

bool kvm_has_smm(void);
bool kvm_enable_x2apic(void);
bool kvm_hv_vpindex_settable(void);
bool kvm_enable_hypercall(uint64_t enable_mask);

bool kvm_enable_sgx_provisioning(KVMState *s);
bool kvm_hyperv_expand_features(X86CPU *cpu, Error **errp);

int kvm_get_vm_type(MachineState *ms);
void kvm_arch_reset_vcpu(X86CPU *cs);
void kvm_arch_after_reset_vcpu(X86CPU *cpu);
void kvm_arch_do_init_vcpu(X86CPU *cs);
uint32_t kvm_arch_get_supported_cpuid(KVMState *env, uint32_t function,
                                      uint32_t index, int reg);
uint64_t kvm_arch_get_supported_msr_feature(KVMState *s, uint32_t index);

void kvm_set_max_apic_id(uint32_t max_apic_id);
void kvm_request_xsave_components(X86CPU *cpu, uint64_t mask);

#ifdef CONFIG_KVM

#include <linux/kvm.h>

typedef struct KvmCpuidInfo {
    struct kvm_cpuid2 cpuid;
    struct kvm_cpuid_entry2 entries[KVM_MAX_CPUID_ENTRIES];
} KvmCpuidInfo;

bool kvm_is_vm_type_supported(int type);
bool kvm_has_adjust_clock_stable(void);
bool kvm_has_exception_payload(void);
void kvm_synchronize_all_tsc(void);

void kvm_get_apic_state(APICCommonState *s, struct kvm_lapic_state *kapic);
void kvm_put_apicbase(X86CPU *cpu, uint64_t value);

bool kvm_has_x2apic_api(void);
bool kvm_has_waitpkg(void);

uint64_t kvm_swizzle_msi_ext_dest_id(uint64_t address);
void kvm_update_msi_routes_all(void *private, bool global,
                               uint32_t index, uint32_t mask);

struct kvm_cpuid_entry2 *cpuid_find_entry(struct kvm_cpuid2 *cpuid,
                                          uint32_t function,
                                          uint32_t index);
uint32_t cpuid_entry_get_reg(struct kvm_cpuid_entry2 *entry, int reg);
uint32_t kvm_x86_build_cpuid(CPUX86State *env, struct kvm_cpuid_entry2 *entries,
                             uint32_t cpuid_i);
#endif /* CONFIG_KVM */

void kvm_smm_cpu_address_space_init(X86CPU *cpu);
void kvm_pc_setup_irq_routing(bool pci_enabled);

#endif
