/*
 * QEMU ARM CPU QOM header (target agnostic)
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

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_ARM_CPU "arm-cpu"

OBJECT_DECLARE_CPU_TYPE(ARMCPU, ARMCPUClass, ARM_CPU)

#define TYPE_ARM_MAX_CPU "max-" TYPE_ARM_CPU

typedef struct ARMCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, void *data);
} ARMCPUInfo;

/**
 * ARMCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * An ARM CPU model.
 */
struct ARMCPUClass {
    CPUClass parent_class;

    const ARMCPUInfo *info;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};


#define TYPE_AARCH64_CPU "aarch64-cpu"
typedef struct AArch64CPUClass AArch64CPUClass;
DECLARE_CLASS_CHECKERS(AArch64CPUClass, AARCH64_CPU,
                       TYPE_AARCH64_CPU)

struct AArch64CPUClass {
    ARMCPUClass parent_class;
};

#endif
