/*
 * QEMU SuperH CPU
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
#ifndef QEMU_SUPERH_CPU_QOM_H
#define QEMU_SUPERH_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_SUPERH_CPU "superh-cpu"

#define SUPERH_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(SuperHCPUClass, (klass), TYPE_SUPERH_CPU)
#define SUPERH_CPU(obj) \
    OBJECT_CHECK(SuperHCPU, (obj), TYPE_SUPERH_CPU)
#define SUPERH_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SuperHCPUClass, (obj), TYPE_SUPERH_CPU)

/**
 * SuperHCPUClass:
 * @parent_reset: The parent class' reset handler.
 *
 * A SuperH CPU model.
 */
typedef struct SuperHCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    void (*parent_reset)(CPUState *cpu);
} SuperHCPUClass;

/**
 * SuperHCPU:
 * @env: #CPUSH4State
 *
 * A SuperH CPU.
 */
typedef struct SuperHCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUSH4State env;
} SuperHCPU;

static inline SuperHCPU *sh_env_get_cpu(CPUSH4State *env)
{
    return SUPERH_CPU(container_of(env, SuperHCPU, env));
}

#define ENV_GET_CPU(e) CPU(sh_env_get_cpu(e))


#endif
