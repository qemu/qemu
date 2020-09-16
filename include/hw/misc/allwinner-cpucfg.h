/*
 * Allwinner CPU Configuration Module emulation
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

#ifndef HW_MISC_ALLWINNER_CPUCFG_H
#define HW_MISC_ALLWINNER_CPUCFG_H

#include "qom/object.h"
#include "hw/sysbus.h"

/**
 * Object model
 * @{
 */

#define TYPE_AW_CPUCFG   "allwinner-cpucfg"
OBJECT_DECLARE_SIMPLE_TYPE(AwCpuCfgState, AW_CPUCFG)

/** @} */

/**
 * Allwinner CPU Configuration Module instance state
 */
struct AwCpuCfgState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t gen_ctrl;
    uint32_t super_standby;
    uint32_t entry_addr;

};

#endif /* HW_MISC_ALLWINNER_CPUCFG_H */
