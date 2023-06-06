/*
 * Allwinner R40 SDRAM Controller emulation
 *
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
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

#ifndef HW_MISC_ALLWINNER_R40_DRAMC_H
#define HW_MISC_ALLWINNER_R40_DRAMC_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "exec/hwaddr.h"

/**
 * Constants
 * @{
 */

/** Highest register address used by DRAMCOM module */
#define AW_R40_DRAMCOM_REGS_MAXADDR  (0x804)

/** Total number of known DRAMCOM registers */
#define AW_R40_DRAMCOM_REGS_NUM      (AW_R40_DRAMCOM_REGS_MAXADDR / \
                                     sizeof(uint32_t))

/** Highest register address used by DRAMCTL module */
#define AW_R40_DRAMCTL_REGS_MAXADDR  (0x88c)

/** Total number of known DRAMCTL registers */
#define AW_R40_DRAMCTL_REGS_NUM      (AW_R40_DRAMCTL_REGS_MAXADDR / \
                                     sizeof(uint32_t))

/** Highest register address used by DRAMPHY module */
#define AW_R40_DRAMPHY_REGS_MAXADDR  (0x4)

/** Total number of known DRAMPHY registers */
#define AW_R40_DRAMPHY_REGS_NUM      (AW_R40_DRAMPHY_REGS_MAXADDR / \
                                     sizeof(uint32_t))

/** @} */

/**
 * Object model
 * @{
 */

#define TYPE_AW_R40_DRAMC "allwinner-r40-dramc"
OBJECT_DECLARE_SIMPLE_TYPE(AwR40DramCtlState, AW_R40_DRAMC)

/** @} */

/**
 * Allwinner R40 SDRAM Controller object instance state.
 */
struct AwR40DramCtlState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    /** Physical base address for start of RAM */
    hwaddr ram_addr;

    /** Total RAM size in megabytes */
    uint32_t ram_size;

    uint8_t set_row_bits;
    uint8_t set_bank_bits;
    uint8_t set_col_bits;

    /**
     * @name Memory Regions
     * @{
     */
    MemoryRegion dramcom_iomem;    /**< DRAMCOM module I/O registers */
    MemoryRegion dramctl_iomem;    /**< DRAMCTL module I/O registers */
    MemoryRegion dramphy_iomem;    /**< DRAMPHY module I/O registers */
    MemoryRegion dram_high;        /**< The high 1G dram for dualrank detect */
    MemoryRegion detect_cells;     /**< DRAM memory cells for auto detect */

    /** @} */

    /**
     * @name Hardware Registers
     * @{
     */

    uint32_t dramcom[AW_R40_DRAMCOM_REGS_NUM]; /**< DRAMCOM registers */
    uint32_t dramctl[AW_R40_DRAMCTL_REGS_NUM]; /**< DRAMCTL registers */
    uint32_t dramphy[AW_R40_DRAMPHY_REGS_NUM] ;/**< DRAMPHY registers */

    /** @} */

};

#endif /* HW_MISC_ALLWINNER_R40_DRAMC_H */
