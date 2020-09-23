/*
 * Allwinner H3 Clock Control Unit emulation
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

#ifndef HW_MISC_ALLWINNER_H3_CCU_H
#define HW_MISC_ALLWINNER_H3_CCU_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by CCU device */
#define AW_H3_CCU_IOSIZE        (0x400)

/** Total number of known registers */
#define AW_H3_CCU_REGS_NUM      (AW_H3_CCU_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_H3_CCU    "allwinner-h3-ccu"
OBJECT_DECLARE_SIMPLE_TYPE(AwH3ClockCtlState, AW_H3_CCU)

/** @} */

/**
 * Allwinner H3 CCU object instance state.
 */
struct AwH3ClockCtlState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Array of hardware registers */
    uint32_t regs[AW_H3_CCU_REGS_NUM];

};

#endif /* HW_MISC_ALLWINNER_H3_CCU_H */
