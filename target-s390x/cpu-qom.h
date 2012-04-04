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

#include "qemu/cpu.h"
#include "cpu.h"

#define TYPE_S390_CPU "s390-cpu"

#define S390_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390CPUClass, (klass), TYPE_S390_CPU)
#define S390_CPU(obj) \
    OBJECT_CHECK(S390CPU, (obj), TYPE_S390_CPU)
#define S390_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390CPUClass, (obj), TYPE_S390_CPU)

/**
 * S390CPUClass:
 * @parent_reset: The parent class' reset handler.
 *
 * An S/390 CPU model.
 */
typedef struct S390CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    void (*parent_reset)(CPUState *cpu);
} S390CPUClass;

/**
 * S390CPU:
 * @env: #CPUS390XState.
 *
 * An S/390 CPU.
 */
typedef struct S390CPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUS390XState env;
} S390CPU;

static inline S390CPU *s390_env_get_cpu(CPUS390XState *env)
{
    return S390_CPU(container_of(env, S390CPU, env));
}

#define ENV_GET_CPU(e) CPU(s390_env_get_cpu(e))


#endif
