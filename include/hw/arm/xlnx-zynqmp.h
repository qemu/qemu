/*
 * Xilinx Zynq MPSoC emulation
 *
 * Copyright (C) 2015 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#ifndef XLNX_ZYNQMP_H
#define XLNX_ZYNQMP_H

#include "hw/arm/boot.h"
#include "hw/intc/arm_gic.h"
#include "hw/net/cadence_gem.h"
#include "hw/char/cadence_uart.h"
#include "hw/ide/pci.h"
#include "hw/ide/ahci.h"
#include "hw/sd/sdhci.h"
#include "hw/ssi/xilinx_spips.h"
#include "hw/dma/xlnx_dpdma.h"
#include "hw/dma/xlnx-zdma.h"
#include "hw/display/xlnx_dp.h"
#include "hw/intc/xlnx-zynqmp-ipi.h"
#include "hw/timer/xlnx-zynqmp-rtc.h"
#include "hw/cpu/cluster.h"

#define TYPE_XLNX_ZYNQMP "xlnx,zynqmp"
#define XLNX_ZYNQMP(obj) OBJECT_CHECK(XlnxZynqMPState, (obj), \
                                       TYPE_XLNX_ZYNQMP)

#define XLNX_ZYNQMP_NUM_APU_CPUS 4
#define XLNX_ZYNQMP_NUM_RPU_CPUS 2
#define XLNX_ZYNQMP_NUM_GEMS 4
#define XLNX_ZYNQMP_NUM_UARTS 2
#define XLNX_ZYNQMP_NUM_SDHCI 2
#define XLNX_ZYNQMP_NUM_SPIS 2
#define XLNX_ZYNQMP_NUM_GDMA_CH 8
#define XLNX_ZYNQMP_NUM_ADMA_CH 8

#define XLNX_ZYNQMP_NUM_QSPI_BUS 2
#define XLNX_ZYNQMP_NUM_QSPI_BUS_CS 2
#define XLNX_ZYNQMP_NUM_QSPI_FLASH 4

#define XLNX_ZYNQMP_NUM_OCM_BANKS 4
#define XLNX_ZYNQMP_OCM_RAM_0_ADDRESS 0xFFFC0000
#define XLNX_ZYNQMP_OCM_RAM_SIZE 0x10000

#define XLNX_ZYNQMP_GIC_REGIONS 6

/* ZynqMP maps the ARM GIC regions (GICC, GICD ...) at consecutive 64k offsets
 * and under-decodes the 64k region. This mirrors the 4k regions to every 4k
 * aligned address in the 64k region. To implement each GIC region needs a
 * number of memory region aliases.
 */

#define XLNX_ZYNQMP_GIC_REGION_SIZE 0x1000
#define XLNX_ZYNQMP_GIC_ALIASES     (0x10000 / XLNX_ZYNQMP_GIC_REGION_SIZE)

#define XLNX_ZYNQMP_MAX_LOW_RAM_SIZE    0x80000000ull

#define XLNX_ZYNQMP_MAX_HIGH_RAM_SIZE   0x800000000ull
#define XLNX_ZYNQMP_HIGH_RAM_START      0x800000000ull

#define XLNX_ZYNQMP_MAX_RAM_SIZE (XLNX_ZYNQMP_MAX_LOW_RAM_SIZE + \
                                  XLNX_ZYNQMP_MAX_HIGH_RAM_SIZE)

typedef struct XlnxZynqMPState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    CPUClusterState apu_cluster;
    CPUClusterState rpu_cluster;
    ARMCPU apu_cpu[XLNX_ZYNQMP_NUM_APU_CPUS];
    ARMCPU rpu_cpu[XLNX_ZYNQMP_NUM_RPU_CPUS];
    GICState gic;
    MemoryRegion gic_mr[XLNX_ZYNQMP_GIC_REGIONS][XLNX_ZYNQMP_GIC_ALIASES];

    MemoryRegion ocm_ram[XLNX_ZYNQMP_NUM_OCM_BANKS];

    MemoryRegion *ddr_ram;
    MemoryRegion ddr_ram_low, ddr_ram_high;

    CadenceGEMState gem[XLNX_ZYNQMP_NUM_GEMS];
    CadenceUARTState uart[XLNX_ZYNQMP_NUM_UARTS];
    SysbusAHCIState sata;
    SDHCIState sdhci[XLNX_ZYNQMP_NUM_SDHCI];
    XilinxSPIPS spi[XLNX_ZYNQMP_NUM_SPIS];
    XlnxZynqMPQSPIPS qspi;
    XlnxDPState dp;
    XlnxDPDMAState dpdma;
    XlnxZynqMPIPI ipi;
    XlnxZynqMPRTC rtc;
    XlnxZDMA gdma[XLNX_ZYNQMP_NUM_GDMA_CH];
    XlnxZDMA adma[XLNX_ZYNQMP_NUM_ADMA_CH];

    char *boot_cpu;
    ARMCPU *boot_cpu_ptr;

    /* Has the ARM Security extensions?  */
    bool secure;
    /* Has the ARM Virtualization extensions?  */
    bool virt;
    /* Has the RPU subsystem?  */
    bool has_rpu;
}  XlnxZynqMPState;

#endif
