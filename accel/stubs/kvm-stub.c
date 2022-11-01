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
#include "sysemu/kvm.h"
#include "hw/pci/msi.h"

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

void kvm_flush_coalesced_mmio_buffer(void)
{
}

void kvm_cpu_synchronize_state(CPUState *cpu)
{
}

bool kvm_has_sync_mmu(void)
{
    return false;
}

int kvm_has_many_ioeventfds(void)
{
    return 0;
}

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_on_sigbus(int code, void *addr)
{
    return 1;
}

int kvm_irqchip_add_msi_route(KVMRouteChange *c, int vector, PCIDevice *dev)
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

void kvm_irqchip_add_change_notifier(Notifier *n)
{
}

void kvm_irqchip_remove_change_notifier(Notifier *n)
{
}

void kvm_irqchip_change_notify(void)
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

bool kvm_dirty_ring_enabled(void)
{
    return false;
}

uint32_t kvm_dirty_ring_size(void)
{
    return 0;
}
