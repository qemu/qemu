/*
 * QEMU KVM support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_KVM_H
#define QEMU_KVM_H

#include "config.h"

#ifdef CONFIG_KVM
extern int kvm_allowed;

#define kvm_enabled() (kvm_allowed)
#else
#define kvm_enabled() (0)
#endif

struct kvm_run;

/* external API */

int kvm_init(int smp_cpus);

int kvm_init_vcpu(CPUState *env);

int kvm_cpu_exec(CPUState *env);

void kvm_set_phys_mem(target_phys_addr_t start_addr,
                      ram_addr_t size,
                      ram_addr_t phys_offset);

void kvm_physical_sync_dirty_bitmap(target_phys_addr_t start_addr, target_phys_addr_t end_addr);

int kvm_log_start(target_phys_addr_t phys_addr, target_phys_addr_t len);
int kvm_log_stop(target_phys_addr_t phys_addr, target_phys_addr_t len);

int kvm_has_sync_mmu(void);

/* internal API */

struct KVMState;
typedef struct KVMState KVMState;

int kvm_ioctl(KVMState *s, int type, ...);

int kvm_vm_ioctl(KVMState *s, int type, ...);

int kvm_vcpu_ioctl(CPUState *env, int type, ...);

/* Arch specific hooks */

int kvm_arch_post_run(CPUState *env, struct kvm_run *run);

int kvm_arch_handle_exit(CPUState *env, struct kvm_run *run);

int kvm_arch_pre_run(CPUState *env, struct kvm_run *run);

int kvm_arch_get_registers(CPUState *env);

int kvm_arch_put_registers(CPUState *env);

int kvm_arch_init(KVMState *s, int smp_cpus);

int kvm_arch_init_vcpu(CPUState *env);

#endif
