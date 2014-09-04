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
 * write_list_to_kvmstate:
 * @cpu: ARMCPU
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
bool write_list_to_kvmstate(ARMCPU *cpu);

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

#endif

#endif
