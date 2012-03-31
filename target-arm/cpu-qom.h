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

#include "qemu/cpu.h"
#include "cpu.h"

#define TYPE_ARM_CPU "arm-cpu"

#define ARM_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(ARMCPUClass, (klass), TYPE_ARM_CPU)
#define ARM_CPU(obj) \
    OBJECT_CHECK(ARMCPU, (obj), TYPE_ARM_CPU)
#define ARM_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ARMCPUClass, (obj), TYPE_ARM_CPU)

/**
 * ARMCPUClass:
 * @parent_reset: The parent class' reset handler.
 *
 * An ARM CPU model.
 */
typedef struct ARMCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    void (*parent_reset)(CPUState *cpu);
} ARMCPUClass;

/**
 * ARMCPU:
 * @env: #CPUARMState
 *
 * An ARM CPU core.
 */
typedef struct ARMCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUARMState env;
} ARMCPU;

static inline ARMCPU *arm_env_get_cpu(CPUARMState *env)
{
    return ARM_CPU(container_of(env, ARMCPU, env));
}

#define ENV_GET_CPU(e) CPU(arm_env_get_cpu(e))


#endif
