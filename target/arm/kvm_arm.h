/*
 * QEMU KVM support -- ARM specific functions.
 *
 * Copyright (c) 2012 Linaro Limited
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_KVM_ARM_H
#define QEMU_KVM_ARM_H

#include "system/kvm.h"
#include "target/arm/cpu-qom.h"

#define KVM_ARM_VGIC_V2   (1 << 0)
#define KVM_ARM_VGIC_V3   (1 << 1)

/**
 * kvm_arm_register_device:
 * @mr: memory region for this device
 * @devid: the KVM device ID
 * @group: device control API group for setting addresses
 * @attr: device control API address type
 * @dev_fd: device control device file descriptor
 * @addr_ormask: value to be OR'ed with resolved address
 *
 * Remember the memory region @mr, and when it is mapped by the machine
 * model, tell the kernel that base address using the device control API.
 * @devid should be the ID of the device as defined by  the arm-vgic device
 * in the device control API.  The machine model may map and unmap the device
 * multiple times; the kernel will only be told the final address at the
 * point where machine init is complete.
 */
void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid, uint64_t group,
                             uint64_t attr, int dev_fd, uint64_t addr_ormask);

/**
 * write_list_to_kvmstate:
 * @cpu: ARMCPU
 * @level: the state level to sync
 *
 * For each register listed in the ARMCPU cpreg_indexes list, write
 * its value from the cpreg_values list into the kernel (via ioctl).
 * This updates KVM's working data structures from TCG data or
 * from incoming migration state.
 *
 * Returns: true if all register values were updated correctly,
 * false if some register was unknown to the kernel or could not
 * be written (eg constant register with the wrong value).
 * Note that we do not stop early on failure -- we will attempt
 * writing all registers in the list.
 */
bool write_list_to_kvmstate(ARMCPU *cpu, int level);

/**
 * write_kvmstate_to_list:
 * @cpu: ARMCPU
 *
 * For each register listed in the ARMCPU cpreg_indexes list, write
 * its value from the kernel into the cpreg_values list. This is used to
 * copy info from KVM's working data structures into TCG or
 * for outbound migration.
 *
 * Returns: true if all register values were read correctly,
 * false if some register was unknown or could not be read.
 * Note that we do not stop early on failure -- we will attempt
 * reading all registers in the list.
 */
bool write_kvmstate_to_list(ARMCPU *cpu);

/**
 * kvm_arm_cpu_pre_save:
 * @cpu: ARMCPU
 *
 * Called after write_kvmstate_to_list() from cpu_pre_save() to update
 * the cpreg list with KVM CPU state.
 */
void kvm_arm_cpu_pre_save(ARMCPU *cpu);

/**
 * kvm_arm_cpu_post_load:
 * @cpu: ARMCPU
 *
 * Called from cpu_post_load() to update KVM CPU state from the cpreg list.
 *
 * Returns: true on success, or false if write_list_to_kvmstate failed.
 */
bool kvm_arm_cpu_post_load(ARMCPU *cpu);

/**
 * kvm_arm_reset_vcpu:
 * @cpu: ARMCPU
 *
 * Called at reset time to kernel registers to their initial values.
 */
void kvm_arm_reset_vcpu(ARMCPU *cpu);

struct kvm_vcpu_init;
/**
 * kvm_arm_create_scratch_host_vcpu:
 * @fdarray: filled in with kvmfd, vmfd, cpufd file descriptors in that order
 * @init: filled in with the necessary values for creating a host
 * vcpu. If NULL is provided, will not init the vCPU (though the cpufd
 * will still be set up).
 *
 * Create a scratch vcpu in its own VM of the type preferred by the host
 * kernel (as would be used for '-cpu host'), for purposes of probing it
 * for capabilities.
 *
 * Returns: true on success (and fdarray and init are filled in),
 * false on failure (and fdarray and init are not valid).
 */
bool kvm_arm_create_scratch_host_vcpu(int *fdarray,
                                      struct kvm_vcpu_init *init);

/**
 * kvm_arm_destroy_scratch_host_vcpu:
 * @fdarray: array of fds as set up by kvm_arm_create_scratch_host_vcpu
 *
 * Tear down the scratch vcpu created by kvm_arm_create_scratch_host_vcpu.
 */
void kvm_arm_destroy_scratch_host_vcpu(int *fdarray);

/**
 * kvm_arm_sve_get_vls:
 * @cpu: ARMCPU
 *
 * Get all the SVE vector lengths supported by the KVM host, setting
 * the bits corresponding to their length in quadwords minus one
 * (vq - 1) up to ARM_MAX_VQ.  Return the resulting map.
 */
uint32_t kvm_arm_sve_get_vls(ARMCPU *cpu);

/**
 * kvm_arm_set_cpu_features_from_host:
 * @cpu: ARMCPU to set the features for
 *
 * Set up the ARMCPU struct fields up to match the information probed
 * from the host CPU.
 */
void kvm_arm_set_cpu_features_from_host(ARMCPU *cpu);

/**
 * kvm_arm_add_vcpu_properties:
 * @cpu: The CPU object to add the properties to
 *
 * Add all KVM specific CPU properties to the CPU object. These
 * are the CPU properties with "kvm-" prefixed names.
 */
void kvm_arm_add_vcpu_properties(ARMCPU *cpu);

/**
 * kvm_arm_steal_time_finalize:
 * @cpu: ARMCPU for which to finalize kvm-steal-time
 * @errp: Pointer to Error* for error propagation
 *
 * Validate the kvm-steal-time property selection and set its default
 * based on KVM support and guest configuration.
 */
void kvm_arm_steal_time_finalize(ARMCPU *cpu, Error **errp);

/*
 * These "is some KVM subfeature enabled?" functions may be called
 * when KVM support is not present, including in the user-mode
 * emulators. The kvm-stub.c file is only built into the system
 * emulators, so for user-mode emulation we provide "always false"
 * stubs here.
 */
#ifndef CONFIG_USER_ONLY
/**
 * kvm_arm_aarch32_supported:
 *
 * Returns: true if KVM can enable AArch32 mode
 * and false otherwise.
 */
bool kvm_arm_aarch32_supported(void);

/**
 * kvm_arm_pmu_supported:
 *
 * Returns: true if KVM can enable the PMU
 * and false otherwise.
 */
bool kvm_arm_pmu_supported(void);

/**
 * kvm_arm_sve_supported:
 *
 * Returns true if KVM can enable SVE and false otherwise.
 */
bool kvm_arm_sve_supported(void);

/**
 * kvm_arm_mte_supported:
 *
 * Returns: true if KVM can enable MTE, and false otherwise.
 */
bool kvm_arm_mte_supported(void);

/**
 * kvm_arm_el2_supported:
 *
 * Returns true if KVM can enable EL2 and false otherwise.
 */
bool kvm_arm_el2_supported(void);
#else

static inline bool kvm_arm_aarch32_supported(void)
{
    return false;
}

static inline bool kvm_arm_pmu_supported(void)
{
    return false;
}

static inline bool kvm_arm_sve_supported(void)
{
    return false;
}

static inline bool kvm_arm_mte_supported(void)
{
    return false;
}

static inline bool kvm_arm_el2_supported(void)
{
    return false;
}
#endif

/**
 * kvm_arm_get_max_vm_ipa_size:
 * @ms: Machine state handle
 * @fixed_ipa: True when the IPA limit is fixed at 40. This is the case
 * for legacy KVM.
 *
 * Returns the number of bits in the IPA address space supported by KVM
 */
int kvm_arm_get_max_vm_ipa_size(MachineState *ms, bool *fixed_ipa);

int kvm_arm_vgic_probe(void);

void kvm_arm_pmu_init(ARMCPU *cpu);
void kvm_arm_pmu_set_irq(ARMCPU *cpu, int irq);

/**
 * kvm_arm_pvtime_init:
 * @cpu: ARMCPU
 * @ipa: Per-vcpu guest physical base address of the pvtime structures
 *
 * Initializes PVTIME for the VCPU, setting the PVTIME IPA to @ipa.
 */
void kvm_arm_pvtime_init(ARMCPU *cpu, uint64_t ipa);

int kvm_arm_set_irq(int cpu, int irqtype, int irq, int level);

void kvm_arm_enable_mte(Object *cpuobj, Error **errp);

void arm_cpu_kvm_set_irq(void *arm_cpu, int irq, int level);

#endif
