/*
 * Microchip PolarFire SoC IOSCB module emulation
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

#ifndef MCHP_PFSOC_IOSCB_H
#define MCHP_PFSOC_IOSCB_H

typedef struct MchpPfSoCIoscbState {
    SysBusDevice parent;
    MemoryRegion container;
    MemoryRegion lane01;
    MemoryRegion lane23;
    MemoryRegion ctrl;
    MemoryRegion cfg;
    MemoryRegion pll_mss;
    MemoryRegion cfm_mss;
    MemoryRegion pll_ddr;
    MemoryRegion bc_ddr;
    MemoryRegion io_calib_ddr;
    MemoryRegion pll_sgmii;
    MemoryRegion dll_sgmii;
    MemoryRegion cfm_sgmii;
    MemoryRegion bc_sgmii;
    MemoryRegion io_calib_sgmii;
} MchpPfSoCIoscbState;

#define TYPE_MCHP_PFSOC_IOSCB "mchp.pfsoc.ioscb"

#define MCHP_PFSOC_IOSCB(obj) \
    OBJECT_CHECK(MchpPfSoCIoscbState, (obj), TYPE_MCHP_PFSOC_IOSCB)

#endif /* MCHP_PFSOC_IOSCB_H */
