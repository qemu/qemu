/*
 *  Copyright (c) 2018-2021  Bastian Koppelmann Paderborn University
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

#ifndef HW_TRICORE_TESTDEVICE_H
#define HW_TRICORE_TESTDEVICE_H

#include "hw/sysbus.h"
#include "hw/hw.h"

#define TYPE_TRICORE_TESTDEVICE "tricore_testdevice"
#define TRICORE_TESTDEVICE(obj) \
    OBJECT_CHECK(TriCoreTestDeviceState, (obj), TYPE_TRICORE_TESTDEVICE)

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

} TriCoreTestDeviceState;

#endif
