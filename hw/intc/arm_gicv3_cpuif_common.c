/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM Generic Interrupt Controller v3
 *
 * Copyright (c) 2016 Linaro Limited
 * Written by Peter Maydell
 *
 * This code is licensed under the GPL, version 2 or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "gicv3_internal.h"
#include "cpu.h"

void gicv3_set_gicv3state(CPUState *cpu, GICv3CPUState *s)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;

    env->gicv3state = (void *)s;
};
