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

#include "qemu/queue.h"
#include "hw/core/cpu.h"
#include "exec/memattrs.h"

#ifdef NEED_CPU_H
# ifdef CONFIG_KVM
#  include <linux/kvm.h>
#  define CONFIG_KVM_IS_POSSIBLE
# endif
#else
# define CONFIG_KVM_IS_POSSIBLE
#endif

#ifdef CONFIG_KVM_IS_POSSIBLE

extern bool kvm_allowed;
extern bool kvm_kernel_irqchip;
extern bool kvm_split_irqchip;
extern bool kvm_async_interrupts_allowed;
extern bool kvm_halt_in_kernel_allowed;
extern bool kvm_eventfds_allowed;
extern bool kvm_irqfds_allowed;
extern bool kvm_resamplefds_allowed;
extern bool kvm_msi_via_irqfd_allowed;
extern bool kvm_gsi_routing_allowed;
extern bool kvm_gsi_direct_mapping;
extern bool kvm_readonly_mem_allowed;
extern bool kvm_direct_msi_allowed;
extern bool kvm_ioeventfd_any_length_allowed;
extern bool kvm_msi_use_devid;

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
 * kvm_irqchip_is_split:
 *
 * Returns: true if the user asked us to split the irqchip
 * implementation between user and kernel space. The details are
 * architecture and machine specific. On PC, it means that the PIC,
 * IOAPIC, and PIT are in user space while the LAPIC is in the kernel.
 */
#define kvm_irqchip_is_split() (kvm_split_irqchip)

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
 * kvm_eventfds_enabled:
 *
 * Returns: true if we can use eventfds to receive notifications
 * from a KVM CPU (ie the kernel supports eventds and we are running
 * with a configuration where it is meaningful to use them).
 */
#define kvm_eventfds_enabled() (kvm_eventfds_allowed)

/**
 * kvm_irqfds_enabled:
 *
 * Returns: true if we can use irqfds to inject interrupts into
 * a KVM CPU (ie the kernel supports irqfds and we are running
 * with a configuration where it is meaningful to use them).
 */
#define kvm_irqfds_enabled() (kvm_irqfds_allowed)

/**
 * kvm_resamplefds_enabled:
 *
 * Returns: true if we can use resamplefds to inject interrupts into
 * a KVM CPU (ie the kernel supports resamplefds and we are running
 * with a configuration where it is meaningful to use them).
 */
#define kvm_resamplefds_enabled() (kvm_resamplefds_allowed)

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

/**
 * kvm_direct_msi_enabled:
 *
 * Returns: true if KVM allows direct MSI injection.
 */
#define kvm_direct_msi_enabled() (kvm_direct_msi_allowed)

/**
 * kvm_ioeventfd_any_length_enabled:
 * Returns: true if KVM allows any length io eventfd.
 */
#define kvm_ioeventfd_any_length_enabled() (kvm_ioeventfd_any_length_allowed)

/**
 * kvm_msi_devid_required:
 * Returns: true if KVM requires a device id to be provided while
 * defining an MSI routing entry.
 */
#define kvm_msi_devid_required() (kvm_msi_use_devid)

#else

#define kvm_enabled()           (0)
#define kvm_irqchip_in_kernel() (false)
#define kvm_irqchip_is_split() (false)
#define kvm_async_interrupts_enabled() (false)
#define kvm_halt_in_kernel() (false)
#define kvm_eventfds_enabled() (false)
#define kvm_irqfds_enabled() (false)
#define kvm_resamplefds_enabled() (false)
#define kvm_msi_via_irqfd_enabled() (false)
#define kvm_gsi_routing_allowed() (false)
#define kvm_gsi_direct_mapping() (false)
#define kvm_readonly_mem_enabled() (false)
#define kvm_direct_msi_enabled() (false)
#define kvm_ioeventfd_any_length_enabled() (false)
#define kvm_msi_devid_required() (false)

#endif  /* CONFIG_KVM_IS_POSSIBLE */

struct kvm_run;
struct kvm_lapic_state;
struct kvm_irq_routing_entry;

typedef struct KVMCapabilityInfo {
    const char *name;
    int value;
} KVMCapabilityInfo;

#define KVM_CAP_INFO(CAP) { "KVM_CAP_" stringify(CAP), KVM_CAP_##CAP }
#define KVM_CAP_LAST_INFO { NULL, 0 }

struct KVMState;
typedef struct KVMState KVMState;
extern KVMState *kvm_state;
typedef struct Notifier Notifier;

/* external API */

bool kvm_has_free_slot(MachineState *ms);
bool kvm_has_sync_mmu(void);
int kvm_has_vcpu_events(void);
int kvm_has_robust_singlestep(void);
int kvm_has_debugregs(void);
int kvm_max_nested_state_length(void);
int kvm_has_pit_state2(void);
int kvm_has_many_ioeventfds(void);
int kvm_has_gsi_routing(void);
int kvm_has_intx_set_mask(void);

int kvm_init_vcpu(CPUState *cpu);
int kvm_cpu_exec(CPUState *cpu);
int kvm_destroy_vcpu(CPUState *cpu);

/**
 * kvm_arm_supports_user_irq
 *
 * Not all KVM implementations support notifications for kernel generated
 * interrupt events to user space. This function indicates whether the current
 * KVM implementation does support them.
 *
 * Returns: true if KVM supports using kernel generated IRQs from user space
 */
bool kvm_arm_supports_user_irq(void);

/**
 * kvm_memcrypt_enabled - return boolean indicating whether memory encryption
 *                        is enabled
 * Returns: 1 memory encryption is enabled
 *          0 memory encryption is disabled
 */
bool kvm_memcrypt_enabled(void);

/**
 * kvm_memcrypt_encrypt_data: encrypt the memory range
 *
 * Return: 1 failed to encrypt the range
 *         0 succesfully encrypted memory region
 */
int kvm_memcrypt_encrypt_data(uint8_t *ptr, uint64_t len);


#ifdef NEED_CPU_H
#include "cpu.h"

void kvm_flush_coalesced_mmio_buffer(void);

int kvm_insert_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type);
int kvm_remove_breakpoint(CPUState *cpu, target_ulong addr,
                          target_ulong len, int type);
void kvm_remove_all_breakpoints(CPUState *cpu);
int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap);

int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_on_sigbus(int code, void *addr);

/* interface with exec.c */

void phys_mem_set_alloc(void *(*alloc)(size_t, uint64_t *align, bool shared));

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
 * kvm_vm_check_attr - check for existence of a specific vm attribute
 * @s: The KVMState pointer
 * @group: the group
 * @attr: the attribute of that group to query for
 *
 * Returns: 1 if the attribute exists
 *          0 if the attribute either does not exist or if the vm device
 *            interface is unavailable
 */
int kvm_vm_check_attr(KVMState *s, uint32_t group, uint64_t attr);

/**
 * kvm_device_check_attr - check for existence of a specific device attribute
 * @fd: The device file descriptor
 * @group: the group
 * @attr: the attribute of that group to query for
 *
 * Returns: 1 if the attribute exists
 *          0 if the attribute either does not exist or if the vm device
 *            interface is unavailable
 */
int kvm_device_check_attr(int fd, uint32_t group, uint64_t attr);

/**
 * kvm_device_access - set or get value of a specific device attribute
 * @fd: The device file descriptor
 * @group: the group
 * @attr: the attribute of that group to set or get
 * @val: pointer to a storage area for the value
 * @write: true for set and false for get operation
 * @errp: error object handle
 *
 * Returns: 0 on success
 *          < 0 on error
 * Use kvm_device_check_attr() in order to check for the availability
 * of optional attributes.
 */
int kvm_device_access(int fd, int group, uint64_t attr,
                      void *val, bool write, Error **errp);

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

/**
 * kvm_device_supported - probe whether KVM supports specific device
 *
 * @vmfd: The fd handler for VM
 * @type: type of device
 *
 * @return: true if supported, otherwise false.
 */
bool kvm_device_supported(int vmfd, uint64_t type);

/* Arch specific hooks */

extern const KVMCapabilityInfo kvm_arch_required_capabilities[];

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run);
MemTxAttrs kvm_arch_post_run(CPUState *cpu, struct kvm_run *run);

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

int kvm_arch_init(MachineState *ms, KVMState *s);

int kvm_arch_init_vcpu(CPUState *cpu);
int kvm_arch_destroy_vcpu(CPUState *cpu);

bool kvm_vcpu_id_is_valid(int vcpu_id);

/* Returns VCPU ID to be used on KVM_CREATE_VCPU ioctl() */
unsigned long kvm_arch_vcpu_id(CPUState *cpu);

#ifdef KVM_HAVE_MCE_INJECTION
void kvm_arch_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
#endif

void kvm_arch_init_irq_routing(KVMState *s);

int kvm_arch_fixup_msi_route(struct kvm_irq_routing_entry *route,
                             uint64_t address, uint32_t data, PCIDevice *dev);

/* Notify arch about newly added MSI routes */
int kvm_arch_add_msi_route_post(struct kvm_irq_routing_entry *route,
                                int vector, PCIDevice *dev);
/* Notify arch about released MSI routes */
int kvm_arch_release_virq_post(int virq);

int kvm_arch_msi_data_to_gsi(uint32_t data);

int kvm_set_irq(KVMState *s, int irq, int level);
int kvm_irqchip_send_msi(KVMState *s, MSIMessage msg);

void kvm_irqchip_add_irq_route(KVMState *s, int gsi, int irqchip, int pin);

void kvm_irqchip_add_change_notifier(Notifier *n);
void kvm_irqchip_remove_change_notifier(Notifier *n);
void kvm_irqchip_change_notify(void);

void kvm_get_apic_state(DeviceState *d, struct kvm_lapic_state *kapic);

struct kvm_guest_debug;
struct kvm_debug_exit_arch;

struct kvm_sw_breakpoint {
    target_ulong pc;
    target_ulong saved_insn;
    int use_count;
    QTAILQ_ENTRY(kvm_sw_breakpoint) entry;
};

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

int kvm_vm_check_extension(KVMState *s, unsigned int extension);

#define kvm_vm_enable_cap(s, capability, cap_flags, ...)             \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        size_t n = MIN(ARRAY_SIZE(args_tmp), ARRAY_SIZE(cap.args));  \
        memcpy(cap.args, args_tmp, n * sizeof(cap.args[0]));         \
        kvm_vm_ioctl(s, KVM_ENABLE_CAP, &cap);                       \
    })

#define kvm_vcpu_enable_cap(cpu, capability, cap_flags, ...)         \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        size_t n = MIN(ARRAY_SIZE(args_tmp), ARRAY_SIZE(cap.args));  \
        memcpy(cap.args, args_tmp, n * sizeof(cap.args[0]));         \
        kvm_vcpu_ioctl(cpu, KVM_ENABLE_CAP, &cap);                   \
    })

uint32_t kvm_arch_get_supported_cpuid(KVMState *env, uint32_t function,
                                      uint32_t index, int reg);
uint64_t kvm_arch_get_supported_msr_feature(KVMState *s, uint32_t index);


void kvm_set_sigmask_len(KVMState *s, unsigned int sigmask_len);

#if !defined(CONFIG_USER_ONLY)
int kvm_physical_memory_addr_from_host(KVMState *s, void *ram_addr,
                                       hwaddr *phys_addr);
#endif

#endif /* NEED_CPU_H */

void kvm_cpu_synchronize_state(CPUState *cpu);
void kvm_cpu_synchronize_post_reset(CPUState *cpu);
void kvm_cpu_synchronize_post_init(CPUState *cpu);
void kvm_cpu_synchronize_pre_loadvm(CPUState *cpu);

void kvm_init_cpu_signals(CPUState *cpu);

/**
 * kvm_irqchip_add_msi_route - Add MSI route for specific vector
 * @s:      KVM state
 * @vector: which vector to add. This can be either MSI/MSIX
 *          vector. The function will automatically detect whether
 *          MSI/MSIX is enabled, and fetch corresponding MSI
 *          message.
 * @dev:    Owner PCI device to add the route. If @dev is specified
 *          as @NULL, an empty MSI message will be inited.
 * @return: virq (>=0) when success, errno (<0) when failed.
 */
int kvm_irqchip_add_msi_route(KVMState *s, int vector, PCIDevice *dev);
int kvm_irqchip_update_msi_route(KVMState *s, int virq, MSIMessage msg,
                                 PCIDevice *dev);
void kvm_irqchip_commit_routes(KVMState *s);
void kvm_irqchip_release_virq(KVMState *s, int virq);

int kvm_irqchip_add_adapter_route(KVMState *s, AdapterInfo *adapter);
int kvm_irqchip_add_hv_sint_route(KVMState *s, uint32_t vcpu, uint32_t sint);

int kvm_irqchip_add_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                       EventNotifier *rn, int virq);
int kvm_irqchip_remove_irqfd_notifier_gsi(KVMState *s, EventNotifier *n,
                                          int virq);
int kvm_irqchip_add_irqfd_notifier(KVMState *s, EventNotifier *n,
                                   EventNotifier *rn, qemu_irq irq);
int kvm_irqchip_remove_irqfd_notifier(KVMState *s, EventNotifier *n,
                                      qemu_irq irq);
void kvm_irqchip_set_qemuirq_gsi(KVMState *s, qemu_irq irq, int gsi);
void kvm_pc_setup_irq_routing(bool pci_enabled);
void kvm_init_irq_routing(KVMState *s);

bool kvm_kernel_irqchip_allowed(void);
bool kvm_kernel_irqchip_required(void);
bool kvm_kernel_irqchip_split(void);

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
struct ppc_radix_page_info *kvm_get_radix_page_info(void);
int kvm_get_max_memslots(void);

/* Notify resamplefd for EOI of specific interrupts. */
void kvm_resample_fd_notify(int gsi);

#endif
