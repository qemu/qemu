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
#include "system/whpx.h"
#include "system/nvmm.h"

void cpu_synchronize_state(CPUState *cpu);
void cpu_synchronize_post_reset(CPUState *cpu);
void cpu_synchronize_post_init(CPUState *cpu);
void cpu_synchronize_pre_loadvm(CPUState *cpu);

#endif /* QEMU_HW_ACCEL_H */
