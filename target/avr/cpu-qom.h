/*
 * QEMU AVR CPU QOM header (target agnostic)
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

#ifndef TARGET_AVR_CPU_QOM_H
#define TARGET_AVR_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_AVR_CPU "avr-cpu"

OBJECT_DECLARE_CPU_TYPE(AVRCPU, AVRCPUClass, AVR_CPU)

#define AVR_CPU_TYPE_SUFFIX "-" TYPE_AVR_CPU
#define AVR_CPU_TYPE_NAME(name) (name AVR_CPU_TYPE_SUFFIX)

#endif /* TARGET_AVR_CPU_QOM_H */
