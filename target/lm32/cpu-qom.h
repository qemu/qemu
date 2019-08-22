/*
 * QEMU LatticeMico32 CPU
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
#ifndef QEMU_LM32_CPU_QOM_H
#define QEMU_LM32_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_LM32_CPU "lm32-cpu"

#define LM32_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(LM32CPUClass, (klass), TYPE_LM32_CPU)
#define LM32_CPU(obj) \
    OBJECT_CHECK(LM32CPU, (obj), TYPE_LM32_CPU)
#define LM32_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(LM32CPUClass, (obj), TYPE_LM32_CPU)

/**
 * LM32CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A LatticeMico32 CPU model.
 */
typedef struct LM32CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} LM32CPUClass;

typedef struct LM32CPU LM32CPU;

#endif
