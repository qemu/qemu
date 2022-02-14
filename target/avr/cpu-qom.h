/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016-2020 Michael Rolnik
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

#ifndef QEMU_AVR_QOM_H
#define QEMU_AVR_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_AVR_CPU "avr-cpu"

OBJECT_DECLARE_CPU_TYPE(AVRCPU, AVRCPUClass, AVR_CPU)

/**
 *  AVRCPUClass:
 *  @parent_realize: The parent class' realize handler.
 *  @parent_reset: The parent class' reset handler.
 *
 *  A AVR CPU model.
 */
struct AVRCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};


#endif /* !defined (QEMU_AVR_CPU_QOM_H) */
