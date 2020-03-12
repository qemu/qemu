/*
 * Allwinner H3 SDRAM Controller emulation
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

#ifndef HW_MISC_ALLWINNER_H3_DRAMC_H
#define HW_MISC_ALLWINNER_H3_DRAMC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "exec/hwaddr.h"

/**
 * Constants
 * @{
 */

/** Highest register address used by DRAMCOM module */
#define AW_H3_DRAMCOM_REGS_MAXADDR  (0x804)

/** Total number of known DRAMCOM registers */
#define AW_H3_DRAMCOM_REGS_NUM      (AW_H3_DRAMCOM_REGS_MAXADDR / \
                                     sizeof(uint32_t))

/** Highest register address used by DRAMCTL module */
#define AW_H3_DRAMCTL_REGS_MAXADDR  (0x88c)

/** Total number of known DRAMCTL registers */
#define AW_H3_DRAMCTL_REGS_NUM      (AW_H3_DRAMCTL_REGS_MAXADDR / \
                                     sizeof(uint32_t))

/** Highest register address used by DRAMPHY module */
#define AW_H3_DRAMPHY_REGS_MAXADDR  (0x4)

/** Total number of known DRAMPHY registers */
#define AW_H3_DRAMPHY_REGS_NUM      (AW_H3_DRAMPHY_REGS_MAXADDR / \
                                     sizeof(uint32_t))

/** @} */

/**
 * Object model
 * @{
 */

#define TYPE_AW_H3_DRAMC "allwinner-h3-dramc"
#define AW_H3_DRAMC(obj) \
    OBJECT_CHECK(AwH3DramCtlState, (obj), TYPE_AW_H3_DRAMC)

/** @} */

/**
 * Allwinner H3 SDRAM Controller object instance state.
 */
typedef struct AwH3DramCtlState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Physical base address for start of RAM */
    hwaddr ram_addr;

    /** Total RAM size in megabytes */
    uint32_t ram_size;

    /**
     * @name Memory Regions
     * @{
     */

    MemoryRegion row_mirror;       /**< Simulates rows for RAM size detection */
    MemoryRegion row_mirror_alias; /**< Alias of the row which is mirrored */
    MemoryRegion dramcom_iomem;    /**< DRAMCOM module I/O registers */
    MemoryRegion dramctl_iomem;    /**< DRAMCTL module I/O registers */
    MemoryRegion dramphy_iomem;    /**< DRAMPHY module I/O registers */

    /** @} */

    /**
     * @name Hardware Registers
     * @{
     */

    uint32_t dramcom[AW_H3_DRAMCOM_REGS_NUM]; /**< Array of DRAMCOM registers */
    uint32_t dramctl[AW_H3_DRAMCTL_REGS_NUM]; /**< Array of DRAMCTL registers */
    uint32_t dramphy[AW_H3_DRAMPHY_REGS_NUM] ;/**< Array of DRAMPHY registers */

    /** @} */

} AwH3DramCtlState;

#endif /* HW_MISC_ALLWINNER_H3_DRAMC_H */
