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
 * kvm_arm_register_device:
 * @mr: memory region for this device
 * @devid: the KVM device ID
 *
 * Remember the memory region @mr, and when it is mapped by the
 * machine model, tell the kernel that base address using the
 * KVM_SET_DEVICE_ADDRESS ioctl. @devid should be the ID of
 * the device as defined by KVM_SET_DEVICE_ADDRESS.
 * The machine model may map and unmap the device multiple times;
 * the kernel will only be told the final address at the point
 * where machine init is complete.
 */
void kvm_arm_register_device(MemoryRegion *mr, uint64_t devid);

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

#endif
