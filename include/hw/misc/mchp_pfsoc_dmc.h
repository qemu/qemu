/*
 * Microchip PolarFire SoC DDR Memory Controller module emulation
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

#ifndef MCHP_PFSOC_DMC_H
#define MCHP_PFSOC_DMC_H

/* DDR SGMII PHY module */

#define MCHP_PFSOC_DDR_SGMII_PHY_REG_SIZE   0x1000

typedef struct MchpPfSoCDdrSgmiiPhyState {
    SysBusDevice parent;
    MemoryRegion sgmii_phy;
} MchpPfSoCDdrSgmiiPhyState;

#define TYPE_MCHP_PFSOC_DDR_SGMII_PHY "mchp.pfsoc.ddr_sgmii_phy"

#define MCHP_PFSOC_DDR_SGMII_PHY(obj) \
    OBJECT_CHECK(MchpPfSoCDdrSgmiiPhyState, (obj), \
                 TYPE_MCHP_PFSOC_DDR_SGMII_PHY)

/* DDR CFG module */

#define MCHP_PFSOC_DDR_CFG_REG_SIZE         0x40000

typedef struct MchpPfSoCDdrCfgState {
    SysBusDevice parent;
    MemoryRegion cfg;
} MchpPfSoCDdrCfgState;

#define TYPE_MCHP_PFSOC_DDR_CFG "mchp.pfsoc.ddr_cfg"

#define MCHP_PFSOC_DDR_CFG(obj) \
    OBJECT_CHECK(MchpPfSoCDdrCfgState, (obj), \
                 TYPE_MCHP_PFSOC_DDR_CFG)

#endif /* MCHP_PFSOC_DMC_H */
