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
#include "sysemu/hax.h"
#include "sysemu/kvm.h"
#include "sysemu/hvf.h"
#include "sysemu/whpx.h"
#include "sysemu/nvmm.h"

void cpu_synchronize_state(CPUState *cpu);
void cpu_synchronize_post_reset(CPUState *cpu);
void cpu_synchronize_post_init(CPUState *cpu);
void cpu_synchronize_pre_loadvm(CPUState *cpu);

#endif /* QEMU_HW_ACCEL_H */
