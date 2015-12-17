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

#include "sysemu/kvm.h"
#include "exec/memory.h"

/**
 * kvm_arm_vcpu_init:
 * @cs: CPUState
 *
 * Initialize (or reinitialize) the VCPU by invoking the
 * KVM_ARM_VCPU_INIT ioctl with the CPU type and feature
 * bitmask specified in the CPUState.
 *
 * Returns: 0 if success else < 0 error code
 */
int kvm_arm_vcpu_init(CPUState *cs);

/**
 * kvm_arm_register_device:
 * @mr: memory region for this device
 * @devid: the KVM device ID
 * @group: device control API group for setting addresses
 * @attr: device control API address type
 * @dev_fd: device control device file descriptor (or -1 if not supported)
 *
 * Remember the memory region @mr, and when it is mapped by the
 * machine model, tell the kernel that base address using the
 * KVM_ARM_SET_DEVICE_ADDRESS ioctl or the newer device control API.  @devid
 * should be the ID of the device as defined by KVM_ARM_SET_DEVICE_ADDRESS or
 * the arm-vgic device in the device control API.
 * The machine model may map
 * and unmap the device multiple times; the kernel will only be told the final
 * address at the point where machine init is complete.
 */
void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid, uint64_t group,
                             uint64_t attr, int dev_fd);

/**
 * kvm_arm_init_cpreg_list:
 * @cs: CPUState
 *
 * Initialize the CPUState's cpreg list according to the kernel's
 * definition of what CPU registers it knows about (and throw away
 * the previous TCG-created cpreg list).
 *
 * Returns: 0 if success, else < 0 error code
 */
int kvm_arm_init_cpreg_list(ARMCPU *cpu);

/**
 * kvm_arm_reg_syncs_via_cpreg_list
 * regidx: KVM register index
 *
 * Return true if this KVM register should be synchronized via the
 * cpreg list of arbitrary system registers, false if it is synchronized
 * by hand using code in kvm_arch_get/put_registers().
 */
bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx);

/**
 * kvm_arm_cpreg_level
 * regidx: KVM register index
 *
 * Return the level of this coprocessor/system register.  Return value is
 * either KVM_PUT_RUNTIME_STATE, KVM_PUT_RESET_STATE, or KVM_PUT_FULL_STATE.
 */
int kvm_arm_cpreg_level(uint64_t regidx);

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
 * kvm_arm_reset_vcpu:
 * @cpu: ARMCPU
 *
 * Called at reset time to kernel registers to their initial values.
 */
void kvm_arm_reset_vcpu(ARMCPU *cpu);

#ifdef CONFIG_KVM
/**
 * kvm_arm_create_scratch_host_vcpu:
 * @cpus_to_try: array of QEMU_KVM_ARM_TARGET_* values (terminated with
 * QEMU_KVM_ARM_TARGET_NONE) to try as fallback if the kernel does not
 * know the PREFERRED_TARGET ioctl
 * @fdarray: filled in with kvmfd, vmfd, cpufd file descriptors in that order
 * @init: filled in with the necessary values for creating a host vcpu
 *
 * Create a scratch vcpu in its own VM of the type preferred by the host
 * kernel (as would be used for '-cpu host'), for purposes of probing it
 * for capabilities.
 *
 * Returns: true on success (and fdarray and init are filled in),
 * false on failure (and fdarray and init are not valid).
 */
bool kvm_arm_create_scratch_host_vcpu(const uint32_t *cpus_to_try,
                                      int *fdarray,
                                      struct kvm_vcpu_init *init);

/**
 * kvm_arm_destroy_scratch_host_vcpu:
 * @fdarray: array of fds as set up by kvm_arm_create_scratch_host_vcpu
 *
 * Tear down the scratch vcpu created by kvm_arm_create_scratch_host_vcpu.
 */
void kvm_arm_destroy_scratch_host_vcpu(int *fdarray);

#define TYPE_ARM_HOST_CPU "host-" TYPE_ARM_CPU
#define ARM_HOST_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(ARMHostCPUClass, (klass), TYPE_ARM_HOST_CPU)
#define ARM_HOST_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ARMHostCPUClass, (obj), TYPE_ARM_HOST_CPU)

typedef struct ARMHostCPUClass {
    /*< private >*/
    ARMCPUClass parent_class;
    /*< public >*/

    uint64_t features;
    uint32_t target;
    const char *dtb_compatible;
} ARMHostCPUClass;

/**
 * kvm_arm_get_host_cpu_features:
 * @ahcc: ARMHostCPUClass to fill in
 *
 * Probe the capabilities of the host kernel's preferred CPU and fill
 * in the ARMHostCPUClass struct accordingly.
 */
bool kvm_arm_get_host_cpu_features(ARMHostCPUClass *ahcc);


/**
 * kvm_arm_sync_mpstate_to_kvm
 * @cpu: ARMCPU
 *
 * If supported set the KVM MP_STATE based on QEMU's model.
 */
int kvm_arm_sync_mpstate_to_kvm(ARMCPU *cpu);

/**
 * kvm_arm_sync_mpstate_to_qemu
 * @cpu: ARMCPU
 *
 * If supported get the MP_STATE from KVM and store in QEMU's model.
 */
int kvm_arm_sync_mpstate_to_qemu(ARMCPU *cpu);

int kvm_arm_vgic_probe(void);

#else

static inline int kvm_arm_vgic_probe(void)
{
    return 0;
}

#endif

static inline const char *gic_class_name(void)
{
    return kvm_irqchip_in_kernel() ? "kvm-arm-gic" : "arm_gic";
}

/**
 * gicv3_class_name
 *
 * Return name of GICv3 class to use depending on whether KVM acceleration is
 * in use. May throw an error if the chosen implementation is not available.
 *
 * Returns: class name to use
 */
const char *gicv3_class_name(void);

/**
 * kvm_arm_handle_debug:
 * @cs: CPUState
 * @debug_exit: debug part of the KVM exit structure
 *
 * Returns: TRUE if the debug exception was handled.
 */
bool kvm_arm_handle_debug(CPUState *cs, struct kvm_debug_exit_arch *debug_exit);

/**
 * kvm_arm_hw_debug_active:
 * @cs: CPU State
 *
 * Return: TRUE if any hardware breakpoints in use.
 */

bool kvm_arm_hw_debug_active(CPUState *cs);

/**
 * kvm_arm_copy_hw_debug_data:
 *
 * @ptr: kvm_guest_debug_arch structure
 *
 * Copy the architecture specific debug registers into the
 * kvm_guest_debug ioctl structure.
 */
struct kvm_guest_debug_arch;

void kvm_arm_copy_hw_debug_data(struct kvm_guest_debug_arch *ptr);

#endif
