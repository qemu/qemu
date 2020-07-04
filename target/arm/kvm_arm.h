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
#include "qemu/error-report.h"

#define KVM_ARM_VGIC_V2   (1 << 0)
#define KVM_ARM_VGIC_V3   (1 << 1)

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
 * kvm_arm_vcpu_finalize:
 * @cs: CPUState
 * @feature: feature to finalize
 *
 * Finalizes the configuration of the specified VCPU feature by
 * invoking the KVM_ARM_VCPU_FINALIZE ioctl. Features requiring
 * this are documented in the "KVM_ARM_VCPU_FINALIZE" section of
 * KVM's API documentation.
 *
 * Returns: 0 if success else < 0 error code
 */
int kvm_arm_vcpu_finalize(CPUState *cs, int feature);

/**
 * kvm_arm_register_device:
 * @mr: memory region for this device
 * @devid: the KVM device ID
 * @group: device control API group for setting addresses
 * @attr: device control API address type
 * @dev_fd: device control device file descriptor (or -1 if not supported)
 * @addr_ormask: value to be OR'ed with resolved address
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
                             uint64_t attr, int dev_fd, uint64_t addr_ormask);

/**
 * kvm_arm_init_cpreg_list:
 * @cpu: ARMCPU
 *
 * Initialize the ARMCPU cpreg list according to the kernel's
 * definition of what CPU registers it knows about (and throw away
 * the previous TCG-created cpreg list).
 *
 * Returns: 0 if success, else < 0 error code
 */
int kvm_arm_init_cpreg_list(ARMCPU *cpu);

/**
 * kvm_arm_reg_syncs_via_cpreg_list:
 * @regidx: KVM register index
 *
 * Return true if this KVM register should be synchronized via the
 * cpreg list of arbitrary system registers, false if it is synchronized
 * by hand using code in kvm_arch_get/put_registers().
 */
bool kvm_arm_reg_syncs_via_cpreg_list(uint64_t regidx);

/**
 * kvm_arm_cpreg_level:
 * @regidx: KVM register index
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
 */
void kvm_arm_cpu_post_load(ARMCPU *cpu);

/**
 * kvm_arm_reset_vcpu:
 * @cpu: ARMCPU
 *
 * Called at reset time to kernel registers to their initial values.
 */
void kvm_arm_reset_vcpu(ARMCPU *cpu);

/**
 * kvm_arm_init_serror_injection:
 * @cs: CPUState
 *
 * Check whether KVM can set guest SError syndrome.
 */
void kvm_arm_init_serror_injection(CPUState *cs);

/**
 * kvm_get_vcpu_events:
 * @cpu: ARMCPU
 *
 * Get VCPU related state from kvm.
 *
 * Returns: 0 if success else < 0 error code
 */
int kvm_get_vcpu_events(ARMCPU *cpu);

/**
 * kvm_put_vcpu_events:
 * @cpu: ARMCPU
 *
 * Put VCPU related state to kvm.
 *
 * Returns: 0 if success else < 0 error code
 */
int kvm_put_vcpu_events(ARMCPU *cpu);

#ifdef CONFIG_KVM
/**
 * kvm_arm_create_scratch_host_vcpu:
 * @cpus_to_try: array of QEMU_KVM_ARM_TARGET_* values (terminated with
 * QEMU_KVM_ARM_TARGET_NONE) to try as fallback if the kernel does not
 * know the PREFERRED_TARGET ioctl. Passing NULL is the same as passing
 * an empty array.
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

/**
 * ARMHostCPUFeatures: information about the host CPU (identified
 * by asking the host kernel)
 */
typedef struct ARMHostCPUFeatures {
    ARMISARegisters isar;
    uint64_t features;
    uint32_t target;
    const char *dtb_compatible;
} ARMHostCPUFeatures;

/**
 * kvm_arm_get_host_cpu_features:
 * @ahcf: ARMHostCPUClass to fill in
 *
 * Probe the capabilities of the host kernel's preferred CPU and fill
 * in the ARMHostCPUClass struct accordingly.
 *
 * Returns true on success and false otherwise.
 */
bool kvm_arm_get_host_cpu_features(ARMHostCPUFeatures *ahcf);

/**
 * kvm_arm_sve_get_vls:
 * @cs: CPUState
 * @map: bitmap to fill in
 *
 * Get all the SVE vector lengths supported by the KVM host, setting
 * the bits corresponding to their length in quadwords minus one
 * (vq - 1) in @map up to ARM_MAX_VQ.
 */
void kvm_arm_sve_get_vls(CPUState *cs, unsigned long *map);

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
 * @obj: The CPU object to add the properties to
 *
 * Add all KVM specific CPU properties to the CPU object. These
 * are the CPU properties with "kvm-" prefixed names.
 */
void kvm_arm_add_vcpu_properties(Object *obj);

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
 * kvm_arm_get_max_vm_ipa_size:
 * @ms: Machine state handle
 *
 * Returns the number of bits in the IPA address space supported by KVM
 */
int kvm_arm_get_max_vm_ipa_size(MachineState *ms);

/**
 * kvm_arm_sync_mpstate_to_kvm:
 * @cpu: ARMCPU
 *
 * If supported set the KVM MP_STATE based on QEMU's model.
 *
 * Returns 0 on success and -1 on failure.
 */
int kvm_arm_sync_mpstate_to_kvm(ARMCPU *cpu);

/**
 * kvm_arm_sync_mpstate_to_qemu:
 * @cpu: ARMCPU
 *
 * If supported get the MP_STATE from KVM and store in QEMU's model.
 *
 * Returns 0 on success and aborts on failure.
 */
int kvm_arm_sync_mpstate_to_qemu(ARMCPU *cpu);

/**
 * kvm_arm_get_virtual_time:
 * @cs: CPUState
 *
 * Gets the VCPU's virtual counter and stores it in the KVM CPU state.
 */
void kvm_arm_get_virtual_time(CPUState *cs);

/**
 * kvm_arm_put_virtual_time:
 * @cs: CPUState
 *
 * Sets the VCPU's virtual counter to the value stored in the KVM CPU state.
 */
void kvm_arm_put_virtual_time(CPUState *cs);

void kvm_arm_vm_state_change(void *opaque, int running, RunState state);

int kvm_arm_vgic_probe(void);

void kvm_arm_pmu_set_irq(CPUState *cs, int irq);
void kvm_arm_pmu_init(CPUState *cs);
int kvm_arm_set_irq(int cpu, int irqtype, int irq, int level);

#else

static inline void kvm_arm_set_cpu_features_from_host(ARMCPU *cpu)
{
    /*
     * This should never actually be called in the "not KVM" case,
     * but set up the fields to indicate an error anyway.
     */
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_NONE;
    cpu->host_cpu_probe_failed = true;
}

static inline void kvm_arm_add_vcpu_properties(Object *obj) {}

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

static inline int kvm_arm_get_max_vm_ipa_size(MachineState *ms)
{
    return -ENOENT;
}

static inline int kvm_arm_vgic_probe(void)
{
    return 0;
}

static inline void kvm_arm_pmu_set_irq(CPUState *cs, int irq) {}
static inline void kvm_arm_pmu_init(CPUState *cs) {}

static inline void kvm_arm_sve_get_vls(CPUState *cs, unsigned long *map) {}

static inline void kvm_arm_get_virtual_time(CPUState *cs) {}
static inline void kvm_arm_put_virtual_time(CPUState *cs) {}
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
static inline const char *gicv3_class_name(void)
{
    if (kvm_irqchip_in_kernel()) {
#ifdef TARGET_AARCH64
        return "kvm-arm-gicv3";
#else
        error_report("KVM GICv3 acceleration is not supported on this "
                     "platform");
        exit(1);
#endif
    } else {
        if (kvm_enabled()) {
            error_report("Userspace GICv3 is not supported with KVM");
            exit(1);
        }
        return "arm-gicv3";
    }
}

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
 * @ptr: kvm_guest_debug_arch structure
 *
 * Copy the architecture specific debug registers into the
 * kvm_guest_debug ioctl structure.
 */
struct kvm_guest_debug_arch;
void kvm_arm_copy_hw_debug_data(struct kvm_guest_debug_arch *ptr);

/**
 * kvm_arm_verify_ext_dabt_pending:
 * @cs: CPUState
 *
 * Verify the fault status code wrt the Ext DABT injection
 *
 * Returns: true if the fault status code is as expected, false otherwise
 */
bool kvm_arm_verify_ext_dabt_pending(CPUState *cs);

/**
 * its_class_name:
 *
 * Return the ITS class name to use depending on whether KVM acceleration
 * and KVM CAP_SIGNAL_MSI are supported
 *
 * Returns: class name to use or NULL
 */
static inline const char *its_class_name(void)
{
    if (kvm_irqchip_in_kernel()) {
        /* KVM implementation requires this capability */
        return kvm_direct_msi_enabled() ? "arm-its-kvm" : NULL;
    } else {
        /* Software emulation is not implemented yet */
        return NULL;
    }
}

#endif
