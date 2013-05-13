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

#include <errno.h>
#include "config-host.h"
#include "qemu/queue.h"
#include "qom/cpu.h"

#ifdef CONFIG_KVM
#include <linux/kvm.h>
#include <linux/kvm_para.h>
#else
/* These constants must never be used at runtime if kvm_enabled() is false.
 * They exist so we don't need #ifdefs around KVM-specific code that already
 * checks kvm_enabled() properly.
 */
#define KVM_CPUID_SIGNATURE      0
#define KVM_CPUID_FEATURES       0
#define KVM_FEATURE_CLOCKSOURCE  0
#define KVM_FEATURE_NOP_IO_DELAY 0
#define KVM_FEATURE_MMU_OP       0
#define KVM_FEATURE_CLOCKSOURCE2 0
#define KVM_FEATURE_ASYNC_PF     0
#define KVM_FEATURE_STEAL_TIME   0
#define KVM_FEATURE_PV_EOI       0
#define KVM_FEATURE_CLOCKSOURCE_STABLE_BIT 0
#endif

extern bool kvm_allowed;
extern bool kvm_kernel_irqchip;
extern bool kvm_async_interrupts_allowed;
extern bool kvm_irqfds_allowed;
extern bool kvm_msi_via_irqfd_allowed;
extern bool kvm_gsi_routing_allowed;

#if defined CONFIG_KVM || !defined NEED_CPU_H
#define kvm_enabled()           (kvm_allowed)
/**
 * kvm_irqchip_in_kernel:
 *
 * Returns: true if the user asked us to create an in-kernel
 * irqchip via the "kernel_irqchip=on" machine option.
 * What this actually means is architecture and machine model
 * specific: on PC, for instance, it means that the LAPIC,
 * IOAPIC and PIT are all in kernel. This function should never
 * be used from generic target-independent code: use one of the
 * following functions or some other specific check instead.
 */
#define kvm_irqchip_in_kernel() (kvm_kernel_irqchip)

/**
 * kvm_async_interrupts_enabled:
 *
 * Returns: true if we can deliver interrupts to KVM
 * asynchronously (ie by ioctl from any thread at any time)
 * rather than having to do interrupt delivery synchronously
 * (where the vcpu must be stopped at a suitable point first).
 */
#define kvm_async_interrupts_enabled() (kvm_async_interrupts_allowed)

/**
 * kvm_irqfds_enabled:
 *
 * Returns: true if we can use irqfds to inject interrupts into
 * a KVM CPU (ie the kernel supports irqfds and we are running
 * with a configuration where it is meaningful to use them).
 */
#define kvm_irqfds_enabled() (kvm_irqfds_allowed)

/**
 * kvm_msi_via_irqfd_enabled:
 *
 * Returns: true if we can route a PCI MSI (Message Signaled Interrupt)
 * to a KVM CPU via an irqfd. This requires that the kernel supports
 * this and that we're running in a configuration that permits it.
 */
#define kvm_msi_via_irqfd_enabled() (kvm_msi_via_irqfd_allowed)

/**
 * kvm_gsi_routing_enabled:
 *
 * Returns: true if GSI routing is enabled (ie the kernel supports
 * it and we're running in a configuration that permits it).
 */
#define kvm_gsi_routing_enabled() (kvm_gsi_routing_allowed)

#else
#define kvm_enabled()           (0)
#define kvm_irqchip_in_kernel() (false)
#define kvm_async_interrupts_enabled() (false)
#define kvm_irqfds_enabled() (false)
#define kvm_msi_via_irqfd_enabled() (false)
#define kvm_gsi_routing_allowed() (false)
#endif

struct kvm_run;
struct kvm_lapic_state;

typedef struct KVMCapabilityInfo {
    const char *name;
    int value;
} KVMCapabilityInfo;

#define KVM_CAP_INFO(CAP) { "KVM_CAP_" stringify(CAP), KVM_CAP_##CAP }
#define KVM_CAP_LAST_INFO { NULL, 0 }

struct KVMState;
typedef struct KVMState KVMState;
extern KVMState *kvm_state;

/* external API */

int kvm_init(void);

int kvm_has_sync_mmu(void);
int kvm_has_vcpu_events(void);
int kvm_has_robust_singlestep(void);
int kvm_has_debugregs(void);
int kvm_has_xsave(void);
int kvm_has_xcrs(void);
int kvm_has_pit_state2(void);
int kvm_has_many_ioeventfds(void);
int kvm_has_gsi_routing(void);
int kvm_has_intx_set_mask(void);

int kvm_init_vcpu(CPUState *cpu);

#ifdef NEED_CPU_H
int kvm_cpu_exec(CPUArchState *env);

#if !defined(CONFIG_USER_ONLY)
void *kvm_ram_alloc(ram_addr_t size);
void *kvm_arch_ram_alloc(ram_addr_t size);
#endif

void kvm_setup_guest_memory(void *start, size_t size);
void kvm_flush_coalesced_mmio_buffer(void);

int kvm_insert_breakpoint(CPUArchState *current_env, target_ulong addr,
                          target_ulong len, int type);
int kvm_remove_breakpoint(CPUArchState *current_env, target_ulong addr,
                          target_ulong len, int type);
void kvm_remove_all_breakpoints(CPUArchState *current_env);
int kvm_update_guest_debug(CPUArchState *env, unsigned long reinject_trap);
#ifndef _WIN32
int kvm_set_signal_mask(CPUArchState *env, const sigset_t *sigset);
#endif

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_on_sigbus(int code, void *addr);

/* internal API */

int kvm_ioctl(KVMState *s, int type, ...);

int kvm_vm_ioctl(KVMState *s, int type, ...);

int kvm_vcpu_ioctl(CPUState *cpu, int type, ...);

/* Arch specific hooks */

extern const KVMCapabilityInfo kvm_arch_required_capabilities[];

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run);
void kvm_arch_post_run(CPUState *cpu, struct kvm_run *run);

int kvm_arch_handle_exit(CPUState *cpu, struct kvm_run *run);

int kvm_arch_process_async_events(CPUState *cpu);

int kvm_arch_get_registers(CPUState *cpu);

/* state subset only touched by the VCPU itself during runtime */
#define KVM_PUT_RUNTIME_STATE   1
/* state subset modified during VCPU reset */
#define KVM_PUT_RESET_STATE     2
/* full state set, modified during initialization or on vmload */
#define KVM_PUT_FULL_STATE      3

int kvm_arch_put_registers(CPUState *cpu, int level);

int kvm_arch_init(KVMState *s);

int kvm_arch_init_vcpu(CPUState *cpu);

/* Returns VCPU ID to be used on KVM_CREATE_VCPU ioctl() */
unsigned long kvm_arch_vcpu_id(CPUState *cpu);

void kvm_arch_reset_vcpu(CPUState *cpu);

int kvm_arch_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_arch_on_sigbus(int code, void *addr);

void kvm_arch_init_irq_routing(KVMState *s);

int kvm_set_irq(KVMState *s, int irq, int level);
int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg);

void kvm_irqchip_add_irq_route(KVMState *s, int gsi, int irqchip, int pin);

void kvm_put_apic_state(DeviceState *d, struct kvm_lapic_state *kapic);
void kvm_get_apic_state(DeviceState *d, struct kvm_lapic_state *kapic);

struct kvm_guest_debug;
struct kvm_debug_exit_arch;

struct kvm_sw_breakpoint {
    target_ulong pc;
    target_ulong saved_insn;
    int use_count;
    QTAILQ_ENTRY(kvm_sw_breakpoint) entry;
};

QTAILQ_HEAD(kvm_sw_breakpoint_head, kvm_sw_breakpoint);

struct kvm_sw_breakpoint *kvm_find_sw_breakpoint(CPUState *cpu,
                                                 target_ulong pc);

int kvm_sw_breakpoints_active(CPUState *cpu);

int kvm_arch_insert_sw_breakpoint(CPUState *current_cpu,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_remove_sw_breakpoint(CPUState *current_cpu,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type);
int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type);
void kvm_arch_remove_all_hw_breakpoints(void);

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg);

bool kvm_arch_stop_on_emulation_error(CPUState *cpu);

int kvm_check_extension(KVMState *s, unsigned int extension);

uint32_t kvm_arch_get_supported_cpuid(KVMState *env, uint32_t function,
                                      uint32_t index, int reg);
void kvm_cpu_synchronize_state(CPUArchState *env);

/* generic hooks - to be moved/refactored once there are more users */

static inline void cpu_synchronize_state(CPUArchState *env)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_state(env);
    }
}

#if !defined(CONFIG_USER_ONLY)
int kvm_physical_memory_addr_from_host(KVMState *s, void *ram_addr,
                                       hwaddr *phys_addr);
#endif

#endif /* NEED_CPU_H */

void kvm_cpu_synchronize_post_reset(CPUState *cpu);
void kvm_cpu_synchronize_post_init(CPUState *cpu);

static inline void cpu_synchronize_post_reset(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_post_reset(cpu);
    }
}

static inline void cpu_synchronize_post_init(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_post_init(cpu);
    }
}

int kvm_irqchip_add_msi_route(KVMState *s, MSIMessage msg);
int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg);
void kvm_irqchip_release_virq(KVMState *s, int virq);

int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n, int virq);
int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n, int virq);
void kvm_pc_gsi_handler(void *opaque, int n, int level);
void kvm_pc_setup_irq_routing(bool pci_enabled);
#endif
