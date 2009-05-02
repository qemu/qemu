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
#include "sys-queue.h"

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
int kvm_sync_vcpus(void);

int kvm_cpu_exec(CPUState *env);

void kvm_set_phys_mem(target_phys_addr_t start_addr,
                      ram_addr_t size,
                      ram_addr_t phys_offset);

int kvm_physical_sync_dirty_bitmap(target_phys_addr_t start_addr,
                                   target_phys_addr_t end_addr);

int kvm_log_start(target_phys_addr_t phys_addr, ram_addr_t size);
int kvm_log_stop(target_phys_addr_t phys_addr, ram_addr_t size);
int kvm_set_migration_log(int enable);

int kvm_has_sync_mmu(void);

void kvm_setup_guest_memory(void *start, size_t size);

int kvm_coalesce_mmio_region(target_phys_addr_t start, ram_addr_t size);
int kvm_uncoalesce_mmio_region(target_phys_addr_t start, ram_addr_t size);

int kvm_insert_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type);
int kvm_remove_breakpoint(CPUState *current_env, target_ulong addr,
                          target_ulong len, int type);
void kvm_remove_all_breakpoints(CPUState *current_env);
int kvm_update_guest_debug(CPUState *env, unsigned long reinject_trap);

/* internal API */

struct KVMState;
typedef struct KVMState KVMState;

int kvm_ioctl(KVMState *s, int type, ...);

int kvm_vm_ioctl(KVMState *s, int type, ...);

int kvm_vcpu_ioctl(CPUState *env, int type, ...);

int kvm_get_mp_state(CPUState *env);
int kvm_put_mp_state(CPUState *env);

/* Arch specific hooks */

int kvm_arch_post_run(CPUState *env, struct kvm_run *run);

int kvm_arch_handle_exit(CPUState *env, struct kvm_run *run);

int kvm_arch_pre_run(CPUState *env, struct kvm_run *run);

int kvm_arch_get_registers(CPUState *env);

int kvm_arch_put_registers(CPUState *env);

int kvm_arch_init(KVMState *s, int smp_cpus);

int kvm_arch_init_vcpu(CPUState *env);

struct kvm_guest_debug;
struct kvm_debug_exit_arch;

struct kvm_sw_breakpoint {
    target_ulong pc;
    target_ulong saved_insn;
    int use_count;
    TAILQ_ENTRY(kvm_sw_breakpoint) entry;
};

TAILQ_HEAD(kvm_sw_breakpoint_head, kvm_sw_breakpoint);

int kvm_arch_debug(struct kvm_debug_exit_arch *arch_info);

struct kvm_sw_breakpoint *kvm_find_sw_breakpoint(CPUState *env,
                                                 target_ulong pc);

int kvm_sw_breakpoints_active(CPUState *env);

int kvm_arch_insert_sw_breakpoint(CPUState *current_env,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_remove_sw_breakpoint(CPUState *current_env,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type);
int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type);
void kvm_arch_remove_all_hw_breakpoints(void);

void kvm_arch_update_guest_debug(CPUState *env, struct kvm_guest_debug *dbg);

int kvm_check_extension(KVMState *s, unsigned int extension);

uint32_t kvm_arch_get_supported_cpuid(CPUState *env, uint32_t function,
                                      int reg);

/* generic hooks - to be moved/refactored once there are more users */

static inline void cpu_synchronize_state(CPUState *env, int modified)
{
    if (kvm_enabled()) {
        if (modified)
            kvm_arch_put_registers(env);
        else
            kvm_arch_get_registers(env);
    }
}

#endif
