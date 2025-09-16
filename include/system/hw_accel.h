/*
 * QEMU Hardware accelerators support
 *
 * Copyright 2016 Google, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_HW_ACCEL_H
#define QEMU_HW_ACCEL_H

#include "hw/core/cpu.h"
#include "system/kvm.h"
#include "system/hvf.h"
#include "system/mshv.h"
#include "system/whpx.h"
#include "system/nvmm.h"

/**
 * cpu_synchronize_state:
 * cpu_synchronize_pre_loadvm:
 * @cpu: The vCPU to synchronize.
 *
 * Request to synchronize QEMU vCPU registers from the hardware accelerator
 * (the hardware accelerator is the reference).
 */
void cpu_synchronize_state(CPUState *cpu);
void cpu_synchronize_pre_loadvm(CPUState *cpu);

/**
 * cpu_synchronize_post_reset:
 * cpu_synchronize_post_init:
 * @cpu: The vCPU to synchronize.
 *
 * Request to synchronize QEMU vCPU registers to the hardware accelerator
 * (QEMU is the reference).
 */
void cpu_synchronize_post_reset(CPUState *cpu);
void cpu_synchronize_post_init(CPUState *cpu);

#endif /* QEMU_HW_ACCEL_H */
