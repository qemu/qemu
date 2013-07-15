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

#include "qemu-common.h"
#include "hw/hw.h"
#include "cpu.h"
#include "sysemu/kvm.h"

#ifndef CONFIG_USER_ONLY
#include "hw/pci/msi.h"
#endif

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_irqfds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;
bool kvm_gsi_direct_mapping;
bool kvm_allowed;
bool kvm_readonly_mem_allowed;

int kvm_init_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

int kvm_init(MachineClass *mc)
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

int kvm_has_sync_mmu(void)
{
    return 0;
}

int kvm_has_many_ioeventfds(void)
{
    return 0;
}

int kvm_has_pit_state2(void)
{
    return 0;
}

void kvm_setup_guest_memory(void *start, size_t size)
{
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

#ifndef _WIN32
int kvm_set_signal_mask(CPUState *cpu, const sigset_t *sigset)
{
    abort();
}
#endif

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_on_sigbus(int code, void *addr)
{
    return 1;
}

#ifndef CONFIG_USER_ONLY
int kvm_irqchip_add_msi_route(KVMState *s, MSIMessage msg)
{
    return -ENOSYS;
}

void kvm_init_irq_routing(KVMState *s)
{
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
{
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg)
{
    return -ENOSYS;
}

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter)
{
    return -ENOSYS;
}

int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, int virq)
{
    return -ENOSYS;
}

int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n, int virq)
{
    return -ENOSYS;
}
#endif
