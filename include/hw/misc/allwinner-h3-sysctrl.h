/*
 * Allwinner H3 System Control emulation
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

#ifndef HW_MISC_ALLWINNER_H3_SYSCTRL_H
#define HW_MISC_ALLWINNER_H3_SYSCTRL_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * @name Constants
 * @{
 */

/** Highest register address used by System Control device */
#define AW_H3_SYSCTRL_REGS_MAXADDR   (0x30)

/** Total number of known registers */
#define AW_H3_SYSCTRL_REGS_NUM       ((AW_H3_SYSCTRL_REGS_MAXADDR / \
                                      sizeof(uint32_t)) + 1)

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_AW_H3_SYSCTRL    "allwinner-h3-sysctrl"
#define AW_H3_SYSCTRL(obj) \
    OBJECT_CHECK(AwH3SysCtrlState, (obj), TYPE_AW_H3_SYSCTRL)

/** @} */

/**
 * Allwinner H3 System Control object instance state
 */
typedef struct AwH3SysCtrlState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;

    /** Array of hardware registers */
    uint32_t regs[AW_H3_SYSCTRL_REGS_NUM];

} AwH3SysCtrlState;

#endif /* HW_MISC_ALLWINNER_H3_SYSCTRL_H */
