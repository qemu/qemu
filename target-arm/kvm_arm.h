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

#endif
