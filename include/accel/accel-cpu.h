/*
 * Accelerator interface, specializes CPUClass
 *
 * Copyright 2021 SUSE LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_CPU_H
#define ACCEL_CPU_H

#include "qom/object.h"
#include "hw/core/cpu.h"

typedef struct AccelCPUClass {
    ObjectClass parent_class;

    void (*cpu_class_init)(CPUClass *cc);
    void (*cpu_instance_init)(CPUState *cpu);
    bool (*cpu_target_realize)(CPUState *cpu, Error **errp);
} AccelCPUClass;

#endif /* ACCEL_CPU_H */
