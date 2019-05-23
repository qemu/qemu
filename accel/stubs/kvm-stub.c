/*
 * QEMU KVM stub
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Author: Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/kvm.h"

#ifndef CONFIG_USER_ONLY
#include "hw/pci/msi.h"
#endif

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_eventfds_allowed;
bool kvm_irqfds_allowed;
bool kvm_resamplefds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;
bool kvm_gsi_direct_mapping;
bool kvm_allowed;
bool kvm_readonly_mem_allowed;
bool kvm_ioeventfd_any_length_allowed;
bool kvm_msi_use_devid;

int kvm_destroy_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

int kvm_init_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

void kvm_flush_coalesced_mmio_buffer(void)
{
}

void kvm_cpu_synchronize_state(CPUState *cpu)
{
}

void kvm_cpu_synchronize_post_reset(CPUState *cpu)
{
}

void kvm_cpu_synchronize_post_init(CPUState *cpu)
{
}

int kvm_cpu_exec(CPUState *cpu)
{
    abort();
}

bool kvm_has_sync_mmu(void)
{
    return false;
}

int kvm_has_many_ioeventfds(void)
{
    return 0;
}

int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    return -ENOSYS;
}

int kvm_insert_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

int kvm_remove_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

void kvm_remove_all_breakpoints(CPUState *cpu)
{
}

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_on_sigbus(int code, void *addr)
{
    return 1;
}

bool kvm_memcrypt_enabled(void)
{
    return false;
}

int kvm_memcrypt_encrypt_data(uint8_t *ptr, uint64_t len)
{
  return 1;
}

#ifndef CONFIG_USER_ONLY
int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev)
{
    return -ENOSYS;
}

void kvm_init_irq_routing(KVMState *s)
{
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
{
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev)
{
    return -ENOSYS;
}

void kvm_irqchip_commit_routes(KVMState *s)
{
}

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter)
{
    return -ENOSYS;
}

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq)
{
    return -ENOSYS;
}

int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq)
{
    return -ENOSYS;
}

bool kvm_has_free_slot(MachineState *ms)
{
    return false;
}

void kvm_init_cpu_signals(CPUState *cpu)
{
    abort();
}

bool kvm_arm_supports_user_irq(void)
{
    return false;
}
#endif
