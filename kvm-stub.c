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
#include "hw/pci/msi.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "sysemu/kvm.h"

KVMState *kvm_state;
bool kvm_kernel_irqchip;
bool kvm_async_interrupts_allowed;
bool kvm_irqfds_allowed;
bool kvm_msi_via_irqfd_allowed;
bool kvm_gsi_routing_allowed;

int kvm_init_vcpu(CPUState *cpu)
{
    return -ENOSYS;
}

int kvm_init(void)
{
    return -ENOSYS;
}

void kvm_flush_coalesced_mmio_buffer(void)
{
}

void kvm_cpu_synchronize_state(CPUArchState *env)
{
}

void kvm_cpu_synchronize_post_reset(CPUArchState *env)
{
}

void kvm_cpu_synchronize_post_init(CPUArchState *env)
{
}

int kvm_cpu_exec(CPUArchState *env)
{
    abort ();
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

int kvm_update_guest_debug(CPUArchState *env, unsigned long reinject_trap)
{
    return -ENOSYS;
}

int kvm_insert_breakpoint(CPUArchState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

int kvm_remove_breakpoint(CPUArchState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

void kvm_remove_all_breakpoints(CPUArchState *current_env)
{
}

#ifndef _WIN32
int kvm_set_signal_mask(CPUArchState *env, const sigset_t *sigset)
{
    abort();
}
#endif

int kvm_set_ioeventfd_pio_word(int fd, uint16_t addr, uint16_t val, bool assign)
{
    return -ENOSYS;
}

int kvm_set_ioeventfd_mmio(int fd, uint32_t adr, uint32_t val, bool assign, uint32_t len)
{
    return -ENOSYS;
}

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_on_sigbus(int code, void *addr)
{
    return 1;
}

int kvm_irqchip_add_msi_route(KVMState *s, MSIMessage msg)
{
    return -ENOSYS;
}

void kvm_irqchip_release_virq(KVMState *s, int virq)
{
}

int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg)
{
    return -ENOSYS;
}

int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n, int virq)
{
    return -ENOSYS;
}

int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n, int virq)
{
    return -ENOSYS;
}
