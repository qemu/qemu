/*
 * Microchip PolarFire SoC machine interface
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MICROCHIP_PFSOC_H
#define HW_MICROCHIP_PFSOC_H

#include "hw/char/mchp_pfsoc_mmuart.h"
#include "hw/dma/sifive_pdma.h"
#include "hw/misc/mchp_pfsoc_dmc.h"
#include "hw/misc/mchp_pfsoc_ioscb.h"
#include "hw/misc/mchp_pfsoc_sysreg.h"
#include "hw/net/cadence_gem.h"
#include "hw/sd/cadence_sdhci.h"

typedef struct MicrochipPFSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CPUClusterState e_cluster;
    CPUClusterState u_cluster;
    RISCVHartArrayState e_cpus;
    RISCVHartArrayState u_cpus;
    DeviceState *plic;
    MchpPfSoCDdrSgmiiPhyState ddr_sgmii_phy;
    MchpPfSoCDdrCfgState ddr_cfg;
    MchpPfSoCIoscbState ioscb;
    MchpPfSoCMMUartState *serial0;
    MchpPfSoCMMUartState *serial1;
    MchpPfSoCMMUartState *serial2;
    MchpPfSoCMMUartState *serial3;
    MchpPfSoCMMUartState *serial4;
    MchpPfSoCSysregState sysreg;
    SiFivePDMAState dma;
    CadenceGEMState gem0;
    CadenceGEMState gem1;
    CadenceSDHCIState sdhci;
} MicrochipPFSoCState;

#define TYPE_MICROCHIP_PFSOC    "microchip.pfsoc"
#define MICROCHIP_PFSOC(obj) \
    OBJECT_CHECK(MicrochipPFSoCState, (obj), TYPE_MICROCHIP_PFSOC)

typedef struct MicrochipIcicleKitState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    MicrochipPFSoCState soc;
} MicrochipIcicleKitState;

#define TYPE_MICROCHIP_ICICLE_KIT_MACHINE \
    MACHINE_TYPE_NAME("microchip-icicle-kit")
#define MICROCHIP_ICICLE_KIT_MACHINE(obj) \
    OBJECT_CHECK(MicrochipIcicleKitState, (obj), \
                 TYPE_MICROCHIP_ICICLE_KIT_MACHINE)

enum {
    MICROCHIP_PFSOC_RSVD0,
    MICROCHIP_PFSOC_DEBUG,
    MICROCHIP_PFSOC_E51_DTIM,
    MICROCHIP_PFSOC_BUSERR_UNIT0,
    MICROCHIP_PFSOC_BUSERR_UNIT1,
    MICROCHIP_PFSOC_BUSERR_UNIT2,
    MICROCHIP_PFSOC_BUSERR_UNIT3,
    MICROCHIP_PFSOC_BUSERR_UNIT4,
    MICROCHIP_PFSOC_CLINT,
    MICROCHIP_PFSOC_L2CC,
    MICROCHIP_PFSOC_DMA,
    MICROCHIP_PFSOC_L2LIM,
    MICROCHIP_PFSOC_PLIC,
    MICROCHIP_PFSOC_MMUART0,
    MICROCHIP_PFSOC_SYSREG,
    MICROCHIP_PFSOC_MPUCFG,
    MICROCHIP_PFSOC_DDR_SGMII_PHY,
    MICROCHIP_PFSOC_EMMC_SD,
    MICROCHIP_PFSOC_DDR_CFG,
    MICROCHIP_PFSOC_MMUART1,
    MICROCHIP_PFSOC_MMUART2,
    MICROCHIP_PFSOC_MMUART3,
    MICROCHIP_PFSOC_MMUART4,
    MICROCHIP_PFSOC_SPI0,
    MICROCHIP_PFSOC_SPI1,
    MICROCHIP_PFSOC_I2C1,
    MICROCHIP_PFSOC_GEM0,
    MICROCHIP_PFSOC_GEM1,
    MICROCHIP_PFSOC_GPIO0,
    MICROCHIP_PFSOC_GPIO1,
    MICROCHIP_PFSOC_GPIO2,
    MICROCHIP_PFSOC_ENVM_CFG,
    MICROCHIP_PFSOC_ENVM_DATA,
    MICROCHIP_PFSOC_QSPI_XIP,
    MICROCHIP_PFSOC_IOSCB,
    MICROCHIP_PFSOC_DRAM_LO,
    MICROCHIP_PFSOC_DRAM_LO_ALIAS,
    MICROCHIP_PFSOC_DRAM_HI,
    MICROCHIP_PFSOC_DRAM_HI_ALIAS
};

enum {
    MICROCHIP_PFSOC_DMA_IRQ0 = 5,
    MICROCHIP_PFSOC_DMA_IRQ1 = 6,
    MICROCHIP_PFSOC_DMA_IRQ2 = 7,
    MICROCHIP_PFSOC_DMA_IRQ3 = 8,
    MICROCHIP_PFSOC_DMA_IRQ4 = 9,
    MICROCHIP_PFSOC_DMA_IRQ5 = 10,
    MICROCHIP_PFSOC_DMA_IRQ6 = 11,
    MICROCHIP_PFSOC_DMA_IRQ7 = 12,
    MICROCHIP_PFSOC_GEM0_IRQ = 64,
    MICROCHIP_PFSOC_GEM1_IRQ = 70,
    MICROCHIP_PFSOC_EMMC_SD_IRQ = 88,
    MICROCHIP_PFSOC_MMUART0_IRQ = 90,
    MICROCHIP_PFSOC_MMUART1_IRQ = 91,
    MICROCHIP_PFSOC_MMUART2_IRQ = 92,
    MICROCHIP_PFSOC_MMUART3_IRQ = 93,
    MICROCHIP_PFSOC_MMUART4_IRQ = 94,
};

#define MICROCHIP_PFSOC_MANAGEMENT_CPU_COUNT    1
#define MICROCHIP_PFSOC_COMPUTE_CPU_COUNT       4

#define MICROCHIP_PFSOC_PLIC_HART_CONFIG        "MS"
#define MICROCHIP_PFSOC_PLIC_NUM_SOURCES        185
#define MICROCHIP_PFSOC_PLIC_NUM_PRIORITIES     7
#define MICROCHIP_PFSOC_PLIC_PRIORITY_BASE      0x04
#define MICROCHIP_PFSOC_PLIC_PENDING_BASE       0x1000
#define MICROCHIP_PFSOC_PLIC_ENABLE_BASE        0x2000
#define MICROCHIP_PFSOC_PLIC_ENABLE_STRIDE      0x80
#define MICROCHIP_PFSOC_PLIC_CONTEXT_BASE       0x200000
#define MICROCHIP_PFSOC_PLIC_CONTEXT_STRIDE     0x1000

#endif /* HW_MICROCHIP_PFSOC_H */
