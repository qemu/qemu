/*
 * QEMU Motorola 68k CPU
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
#ifndef QEMU_M68K_CPU_QOM_H
#define QEMU_M68K_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_M68K_CPU "m68k-cpu"

#define M68K_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(M68kCPUClass, (klass), TYPE_M68K_CPU)
#define M68K_CPU(obj) \
    OBJECT_CHECK(M68kCPU, (obj), TYPE_M68K_CPU)
#define M68K_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(M68kCPUClass, (obj), TYPE_M68K_CPU)

/*
 * M68kCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A Motorola 68k CPU model.
 */
typedef struct M68kCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} M68kCPUClass;

typedef struct M68kCPU M68kCPU;

#endif
