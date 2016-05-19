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

#include "qom/cpu.h"

#define TYPE_S390_CPU "s390-cpu"

#define S390_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390CPUClass, (klass), TYPE_S390_CPU)
#define S390_CPU(obj) \
    OBJECT_CHECK(S390CPU, (obj), TYPE_S390_CPU)
#define S390_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390CPUClass, (obj), TYPE_S390_CPU)

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
typedef struct S390CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    int64_t next_cpu_id;

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
    void (*load_normal)(CPUState *cpu);
    void (*cpu_reset)(CPUState *cpu);
    void (*initial_cpu_reset)(CPUState *cpu);
} S390CPUClass;

typedef struct S390CPU S390CPU;

#endif
