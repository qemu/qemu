/*
 * QEMU support -- ARM Power Control specific functions.
 *
 * Copyright (c) 2016 Jean-Christophe Dubois
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_ARM_POWERCTL_H
#define QEMU_ARM_POWERCTL_H

#include "kvm-consts.h"

#define QEMU_ARM_POWERCTL_RET_SUCCESS QEMU_PSCI_RET_SUCCESS
#define QEMU_ARM_POWERCTL_INVALID_PARAM QEMU_PSCI_RET_INVALID_PARAMS
#define QEMU_ARM_POWERCTL_ALREADY_ON QEMU_PSCI_RET_ALREADY_ON
#define QEMU_ARM_POWERCTL_IS_OFF QEMU_PSCI_RET_DENIED

/*
 * arm_get_cpu_by_id:
 * @cpuid: the id of the CPU we want to retrieve the state
 *
 * Retrieve a CPUState object from its CPU ID provided in @cpuid.
 *
 * Returns: a pointer to the CPUState structure of the requested CPU.
 */
CPUState *arm_get_cpu_by_id(uint64_t cpuid);

/*
 * arm_set_cpu_on:
 * @cpuid: the id of the CPU we want to start/wake up.
 * @entry: the address the CPU shall start from.
 * @context_id: the value to put in r0/x0.
 * @target_el: The desired exception level.
 * @target_aa64: 1 if the requested mode is AArch64. 0 otherwise.
 *
 * Start the cpu designated by @cpuid in @target_el exception level. The mode
 * shall be AArch64 if @target_aa64 is set to 1. Otherwise the mode is
 * AArch32. The CPU shall start at @entry with @context_id in r0/x0.
 *
 * Returns: QEMU_ARM_POWERCTL_RET_SUCCESS on success.
 * QEMU_ARM_POWERCTL_INVALID_PARAM if bad parameters are provided.
 * QEMU_ARM_POWERCTL_ALREADY_ON if the CPU was already started.
 */
int arm_set_cpu_on(uint64_t cpuid, uint64_t entry, uint64_t context_id,
                   uint32_t target_el, bool target_aa64);

/*
 * arm_set_cpu_off:
 * @cpuid: the id of the CPU we want to stop/shut down.
 *
 * Stop the cpu designated by @cpuid.
 *
 * Returns: QEMU_ARM_POWERCTL_RET_SUCCESS on success.
 * QEMU_ARM_POWERCTL_INVALID_PARAM if bad parameters are provided.
 * QEMU_ARM_POWERCTL_IS_OFF if CPU is already off
 */

int arm_set_cpu_off(uint64_t cpuid);

/*
 * arm_reset_cpu:
 * @cpuid: the id of the CPU we want to reset.
 *
 * Reset the cpu designated by @cpuid.
 *
 * Returns: QEMU_ARM_POWERCTL_RET_SUCCESS on success.
 * QEMU_ARM_POWERCTL_INVALID_PARAM if bad parameters are provided.
 * QEMU_ARM_POWERCTL_IS_OFF if CPU is off
 */
int arm_reset_cpu(uint64_t cpuid);

#endif
