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
#include "exec-all.h"
#include "gdbstub.h"
#include "kvm.h"

int kvm_irqchip_in_kernel(void)
{
    return 0;
}

int kvm_pit_in_kernel(void)
{
    return 0;
}


int kvm_init_vcpu(CPUState *env)
{
    return -ENOSYS;
}

int kvm_coalesce_mmio_region(target_phys_addr_t start, ram_addr_t size)
{
    return -ENOSYS;
}

int kvm_uncoalesce_mmio_region(target_phys_addr_t start, ram_addr_t size)
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

void kvm_cpu_synchronize_state(CPUState *env)
{
}

void kvm_cpu_synchronize_post_reset(CPUState *env)
{
}

void kvm_cpu_synchronize_post_init(CPUState *env)
{
}

int kvm_cpu_exec(CPUState *env)
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

void kvm_setup_guest_memory(void *start, size_t size)
{
}

int kvm_update_guest_debug(CPUState *env, unsigned long reinject_trap)
{
    return -ENOSYS;
}

int kvm_insert_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

int kvm_remove_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type)
{
    return -EINVAL;
}

void kvm_remove_all_breakpoints(CPUState *current_env)
{
}

#ifndef _WIN32
int kvm_set_signal_mask(CPUState *env, const sigset_t *sigset)
{
    abort();
}
#endif

int kvm_set_ioeventfd_pio_word(int fd, uint16_t addr, uint16_t val, bool assign)
{
    return -ENOSYS;
}

int kvm_set_ioeventfd_mmio_long(int fd, uint32_t adr, uint32_t val, bool assign)
{
    return -ENOSYS;
}

int kvm_on_sigbus_vcpu(CPUState *env, int code, void *addr)
{
    return 1;
}

int kvm_on_sigbus(int code, void *addr)
{
    return 1;
}
