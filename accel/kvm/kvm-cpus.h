/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef KVM_CPUS_H
#define KVM_CPUS_H

#include "sysemu/cpus.h"

int kvm_init_vcpu(CPUState *cpu, Error **errp);
int kvm_cpu_exec(CPUState *cpu);
void kvm_destroy_vcpu(CPUState *cpu);
void kvm_cpu_synchronize_post_reset(CPUState *cpu);
void kvm_cpu_synchronize_post_init(CPUState *cpu);
void kvm_cpu_synchronize_pre_loadvm(CPUState *cpu);
bool kvm_supports_guest_debug(void);
int kvm_insert_breakpoint(CPUState *cpu, int type, hwaddr addr, hwaddr len);
int kvm_remove_breakpoint(CPUState *cpu, int type, hwaddr addr, hwaddr len);
void kvm_remove_all_breakpoints(CPUState *cpu);

#endif /* KVM_CPUS_H */
