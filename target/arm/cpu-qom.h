/*
 * QEMU ARM CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_ARM_CPU_QOM_H
#define QEMU_ARM_CPU_QOM_H

#include "qom/cpu.h"

struct arm_boot_info;

#define TYPE_ARM_CPU "arm-cpu"

#define ARM_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(ARMCPUClass, (klass), TYPE_ARM_CPU)
#define ARM_CPU(obj) \
    OBJECT_CHECK(ARMCPU, (obj), TYPE_ARM_CPU)
#define ARM_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ARMCPUClass, (obj), TYPE_ARM_CPU)

#define TYPE_ARM_MAX_CPU "max-" TYPE_ARM_CPU

typedef struct ARMCPUInfo ARMCPUInfo;

/**
 * ARMCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * An ARM CPU model.
 */
typedef struct ARMCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    const ARMCPUInfo *info;
    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} ARMCPUClass;

typedef struct ARMCPU ARMCPU;

#define TYPE_AARCH64_CPU "aarch64-cpu"
#define AARCH64_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(AArch64CPUClass, (klass), TYPE_AARCH64_CPU)
#define AARCH64_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AArch64CPUClass, (obj), TYPE_AArch64_CPU)

typedef struct AArch64CPUClass {
    /*< private >*/
    ARMCPUClass parent_class;
    /*< public >*/
} AArch64CPUClass;

void register_cp_regs_for_features(ARMCPU *cpu);
void init_cpreg_list(ARMCPU *cpu);

/* Callback functions for the generic timer's timers. */
void arm_gt_ptimer_cb(void *opaque);
void arm_gt_vtimer_cb(void *opaque);
void arm_gt_htimer_cb(void *opaque);
void arm_gt_stimer_cb(void *opaque);

#define ARM_AFF0_SHIFT 0
#define ARM_AFF0_MASK  (0xFFULL << ARM_AFF0_SHIFT)
#define ARM_AFF1_SHIFT 8
#define ARM_AFF1_MASK  (0xFFULL << ARM_AFF1_SHIFT)
#define ARM_AFF2_SHIFT 16
#define ARM_AFF2_MASK  (0xFFULL << ARM_AFF2_SHIFT)
#define ARM_AFF3_SHIFT 32
#define ARM_AFF3_MASK  (0xFFULL << ARM_AFF3_SHIFT)
#define ARM_DEFAULT_CPUS_PER_CLUSTER 8

#define ARM32_AFFINITY_MASK (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK)
#define ARM64_AFFINITY_MASK \
    (ARM_AFF0_MASK|ARM_AFF1_MASK|ARM_AFF2_MASK|ARM_AFF3_MASK)
#define ARM64_AFFINITY_INVALID (~ARM64_AFFINITY_MASK)

#endif
