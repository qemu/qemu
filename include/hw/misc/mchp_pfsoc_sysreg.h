/*
 * Microchip PolarFire SoC SYSREG module emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MCHP_PFSOC_SYSREG_H
#define MCHP_PFSOC_SYSREG_H

#include "hw/sysbus.h"

#define MCHP_PFSOC_SYSREG_REG_SIZE  0x2000

typedef struct MchpPfSoCSysregState {
    SysBusDevice parent;
    MemoryRegion sysreg;
    qemu_irq irq;
} MchpPfSoCSysregState;

#define TYPE_MCHP_PFSOC_SYSREG "mchp.pfsoc.sysreg"

#define MCHP_PFSOC_SYSREG(obj) \
    OBJECT_CHECK(MchpPfSoCSysregState, (obj), \
                 TYPE_MCHP_PFSOC_SYSREG)

#endif /* MCHP_PFSOC_SYSREG_H */
