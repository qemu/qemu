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
extern bool kvm_halt_in_kernel_allowed;
extern bool kvm_irqfds_allowed;
extern bool kvm_msi_via_irqfd_allowed;
extern bool kvm_gsi_routing_allowed;
extern bool kvm_gsi_direct_mapping;
extern bool kvm_readonly_mem_allowed;

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
 * kvm_halt_in_kernel
 *
 * Returns: true if halted cpus should still get a KVM_RUN ioctl to run
 * inside of kernel space. This only works if MP state is implemented.
 */
#define kvm_halt_in_kernel() (kvm_halt_in_kernel_allowed)

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

/**
 * kvm_gsi_direct_mapping:
 *
 * Returns: true if GSI direct mapping is enabled.
 */
#define kvm_gsi_direct_mapping() (kvm_gsi_direct_mapping)

/**
 * kvm_readonly_mem_enabled:
 *
 * Returns: true if KVM readonly memory is enabled (ie the kernel
 * supports it and we're running in a configuration that permits it).
 */
#define kvm_readonly_mem_enabled() (kvm_readonly_mem_allowed)

#else
#define kvm_enabled()           (0)
#define kvm_irqchip_in_kernel() (false)
#define kvm_async_interrupts_enabled() (false)
#define kvm_halt_in_kernel() (false)
#define kvm_irqfds_enabled() (false)
#define kvm_msi_via_irqfd_enabled() (false)
#define kvm_gsi_routing_allowed() (false)
#define kvm_gsi_direct_mapping() (false)
#define kvm_readonly_mem_enabled() (false)
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

int kvm_init(MachineClass *mc);

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
int kvm_cpu_exec(CPUState *cpu);

#ifdef NEED_CPU_H

void kvm_setup_guest_memory(void *start, size_t size);
void kvm_flush_coalesced_mmio_buffer(void);

int kvm_insert_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type);
int kvm_remove_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type);
void kvm_remove_all_breakpoints(CPUState *cpu);
int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap);
#ifndef _WIN32
int kvm_set_signal_mask(CPUState *cpu, const sigset_t *sigset);
#endif

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_on_sigbus(int code, void *addr);

/* internal API */

int kvm_ioctl(KVMState *s, int type, ...);

int kvm_vm_ioctl(KVMState *s, int type, ...);

int kvm_vcpu_ioctl(CPUState *cpu, int type, ...);

/**
 * kvm_device_ioctl - call an ioctl on a kvm device
 * @fd: The KVM device file descriptor as returned from KVM_CREATE_DEVICE
 * @type: The device-ctrl ioctl number
 *
 * Returns: -errno on error, nonnegative on success
 */
int kvm_device_ioctl(int fd, int type, ...);

/**
 * kvm_create_device - create a KVM device for the device control API
 * @KVMState: The KVMState pointer
 * @type: The KVM device type (see Documentation/virtual/kvm/devices in the
 *        kernel source)
 * @test: If true, only test if device can be created, but don't actually
 *        create the device.
 *
 * Returns: -errno on error, nonnegative on success: @test ? 0 : device fd;
 */
int kvm_create_device(KVMState *s, uint64_t type, bool test);


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

int kvm_arch_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_arch_on_sigbus(int code, void *addr);

void kvm_arch_init_irq_routing(KVMState *s);

int kvm_set_irq(KVMState *s, int irq, int level);
int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg);

void kvm_irqchip_add_irq_route(KVMState *s, int gsi, int irqchip, int pin);
void kvm_irqchip_commit_routes(KVMState *s);

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

int kvm_arch_insert_sw_breakpoint(CPUState *cpu,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_remove_sw_breakpoint(CPUState *cpu,
                                  struct kvm_sw_breakpoint *bp);
int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type);
int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type);
void kvm_arch_remove_all_hw_breakpoints(void);

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg);

bool kvm_arch_stop_on_emulation_error(CPUState *cpu);

int kvm_check_extension(KVMState *s, unsigned int extension);

#define kvm_vm_enable_cap(s, capability, cap_flags, ...)             \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        int i;                                                       \
        for (i = 0; i < (int)ARRAY_SIZE(args_tmp) &&                 \
                     i < ARRAY_SIZE(cap.args); i++) {                \
            cap.args[i] = args_tmp[i];                               \
        }                                                            \
        kvm_vm_ioctl(s, KVM_ENABLE_CAP, &cap);                       \
    })

#define kvm_vcpu_enable_cap(cpu, capability, cap_flags, ...)         \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        int i;                                                       \
        for (i = 0; i < (int)ARRAY_SIZE(args_tmp) &&                 \
                     i < ARRAY_SIZE(cap.args); i++) {                \
            cap.args[i] = args_tmp[i];                               \
        }                                                            \
        kvm_vcpu_ioctl(cpu, KVM_ENABLE_CAP, &cap);                   \
    })

uint32_t kvm_arch_get_supported_cpuid(KVMState *env, uint32_t function,
                                      uint32_t index, int reg);

#if !defined(CONFIG_USER_ONLY)
int kvm_physical_memory_addr_from_host(KVMState *s, void *ram_addr,
                                       hwaddr *phys_addr);
#endif

#endif /* NEED_CPU_H */

void kvm_cpu_synchronize_state(CPUState *cpu);
void kvm_cpu_synchronize_post_reset(CPUState *cpu);
void kvm_cpu_synchronize_post_init(CPUState *cpu);

/* generic hooks - to be moved/refactored once there are more users */

static inline void cpu_synchronize_state(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_state(cpu);
    }
}

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

int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, int virq);
int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n, int virq);
void kvm_pc_gsi_handler(void *opaque, int n, int level);
void kvm_pc_setup_irq_routing(bool pci_enabled);
void kvm_init_irq_routing(KVMState *s);

/**
 * kvm_arch_irqchip_create:
 * @KVMState: The KVMState pointer
 *
 * Allow architectures to create an in-kernel irq chip themselves.
 *
 * Returns: < 0: error
 *            0: irq chip was not created
 *          > 0: irq chip was created
 */
int kvm_arch_irqchip_create(KVMState *s);

/**
 * kvm_set_one_reg - set a register value in KVM via KVM_SET_ONE_REG ioctl
 * @id: The register ID
 * @source: The pointer to the value to be set. It must point to a variable
 *          of the correct type/size for the register being accessed.
 *
 * Returns: 0 on success, or a negative errno on failure.
 */
int kvm_set_one_reg(CPUState *cs, uint64_t id, void *source);

/**
 * kvm_get_one_reg - get a register value from KVM via KVM_GET_ONE_REG ioctl
 * @id: The register ID
 * @target: The pointer where the value is to be stored. It must point to a
 *          variable of the correct type/size for the register being accessed.
 *
 * Returns: 0 on success, or a negative errno on failure.
 */
int kvm_get_one_reg(CPUState *cs, uint64_t id, void *target);
#endif
