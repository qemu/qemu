/*
 * CSKY CPU qom
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


#ifndef QEMU_CSKY_CPU_QOM_H
#define QEMU_CSKY_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_CSKY_CPU "csky-cpu"

#define CSKY_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(CSKYCPUClass, (klass), TYPE_CSKY_CPU)
#define CSKY_CPU(obj) \
    OBJECT_CHECK(CSKYCPU, (obj), TYPE_CSKY_CPU)
#define CSKY_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(CSKYCPUClass, (obj), TYPE_CSKY_CPU)

/**
 * CSKYCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A CSKY model.
 */
typedef struct CSKYCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} CSKYCPUClass;

typedef struct CSKYCPU CSKYCPU;

#endif
