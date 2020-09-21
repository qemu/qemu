/*
 * Allwinner Security ID emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MISC_ALLWINNER_SID_H
#define HW_MISC_ALLWINNER_SID_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "qemu/uuid.h"

/**
 * Object model
 * @{
 */

#define TYPE_AW_SID    "allwinner-sid"
OBJECT_DECLARE_SIMPLE_TYPE(AwSidState, AW_SID)

/** @} */

/**
 * Allwinner Security ID object instance state
 */
struct AwSidState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Control register defines how and what to read */
    uint32_t control;

    /** RdKey register contains the data retrieved by the device */
    uint32_t rdkey;

    /** Stores the emulated device identifier */
    QemuUUID identifier;

};

#endif /* HW_MISC_ALLWINNER_SID_H */
