/*
 * QEMU S/390 CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_S390_CPU_QOM_H
#define QEMU_S390_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_S390_CPU "s390x-cpu"

OBJECT_DECLARE_CPU_TYPE(S390CPU, S390CPUClass, S390_CPU)

typedef struct S390CPUModel S390CPUModel;
typedef struct S390CPUDef S390CPUDef;

typedef struct CPUArchState CPUS390XState;

typedef enum cpu_reset_type {
    S390_CPU_RESET_NORMAL,
    S390_CPU_RESET_INITIAL,
    S390_CPU_RESET_CLEAR,
} cpu_reset_type;

/**
 * S390CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 * @load_normal: Performs a load normal.
 * @cpu_reset: Performs a CPU reset.
 * @initial_cpu_reset: Performs an initial CPU reset.
 *
 * An S/390 CPU model.
 */
struct S390CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    const S390CPUDef *cpu_def;
    bool kvm_required;
    bool is_static;
    bool is_migration_safe;
    const char *desc;

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
    void (*load_normal)(CPUState *cpu);
    void (*reset)(CPUState *cpu, cpu_reset_type type);
};

#endif
