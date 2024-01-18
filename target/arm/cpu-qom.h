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

#define TYPE_ARM_CPU "arm-cpu"

OBJECT_DECLARE_CPU_TYPE(ARMCPU, ARMCPUClass, ARM_CPU)

#define TYPE_ARM_MAX_CPU "max-" TYPE_ARM_CPU

#define TYPE_AARCH64_CPU "aarch64-cpu"
typedef struct AArch64CPUClass AArch64CPUClass;
DECLARE_CLASS_CHECKERS(AArch64CPUClass, AARCH64_CPU,
                       TYPE_AARCH64_CPU)

#define ARM_CPU_TYPE_SUFFIX "-" TYPE_ARM_CPU
#define ARM_CPU_TYPE_NAME(name) (name ARM_CPU_TYPE_SUFFIX)

/* Meanings of the ARMCPU object's four inbound GPIO lines */
#define ARM_CPU_IRQ 0
#define ARM_CPU_FIQ 1
#define ARM_CPU_VIRQ 2
#define ARM_CPU_VFIQ 3

/* For M profile, some registers are banked secure vs non-secure;
 * these are represented as a 2-element array where the first element
 * is the non-secure copy and the second is the secure copy.
 * When the CPU does not have implement the security extension then
 * only the first element is used.
 * This means that the copy for the current security state can be
 * accessed via env->registerfield[env->v7m.secure] (whether the security
 * extension is implemented or not).
 */
enum {
    M_REG_NS = 0,
    M_REG_S = 1,
    M_REG_NUM_BANKS = 2,
};

#endif
