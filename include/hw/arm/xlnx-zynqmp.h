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
#include "hw/net/xlnx-zynqmp-can.h"
#include "hw/ide/ahci.h"
#include "hw/sd/sdhci.h"
#include "hw/ssi/xilinx_spips.h"
#include "hw/dma/xlnx_dpdma.h"
#include "hw/dma/xlnx-zdma.h"
#include "hw/display/xlnx_dp.h"
#include "hw/intc/xlnx-zynqmp-ipi.h"
#include "hw/rtc/xlnx-zynqmp-rtc.h"
#include "hw/cpu/cluster.h"
#include "target/arm/cpu.h"
#include "qom/object.h"
#include "net/can_emu.h"
#include "hw/dma/xlnx_csu_dma.h"
#include "hw/nvram/xlnx-bbram.h"
#include "hw/nvram/xlnx-zynqmp-efuse.h"
#include "hw/or-irq.h"
#include "hw/misc/xlnx-zynqmp-apu-ctrl.h"
#include "hw/misc/xlnx-zynqmp-crf.h"
#include "hw/timer/cadence_ttc.h"
#include "hw/usb/hcd-dwc3.h"

#define TYPE_XLNX_ZYNQMP "xlnx-zynqmp"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxZynqMPState, XLNX_ZYNQMP)

#define XLNX_ZYNQMP_NUM_APU_CPUS 4
#define XLNX_ZYNQMP_NUM_RPU_CPUS 2
#define XLNX_ZYNQMP_NUM_GEMS 4
#define XLNX_ZYNQMP_NUM_UARTS 2
#define XLNX_ZYNQMP_NUM_CAN 2
#define XLNX_ZYNQMP_CAN_REF_CLK (24 * 1000 * 1000)
#define XLNX_ZYNQMP_NUM_SDHCI 2
#define XLNX_ZYNQMP_NUM_SPIS 2
#define XLNX_ZYNQMP_NUM_GDMA_CH 8
#define XLNX_ZYNQMP_NUM_ADMA_CH 8
#define XLNX_ZYNQMP_NUM_USB 2

#define XLNX_ZYNQMP_NUM_QSPI_BUS 2
#define XLNX_ZYNQMP_NUM_QSPI_BUS_CS 2
#define XLNX_ZYNQMP_NUM_QSPI_FLASH 4

#define XLNX_ZYNQMP_NUM_OCM_BANKS 4
#define XLNX_ZYNQMP_OCM_RAM_0_ADDRESS 0xFFFC0000
#define XLNX_ZYNQMP_OCM_RAM_SIZE 0x10000

#define XLNX_ZYNQMP_GIC_REGIONS 6

/*
 * ZynqMP maps the ARM GIC regions (GICC, GICD ...) at consecutive 64k offsets
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

#define XLNX_ZYNQMP_NUM_TTC 4

/*
 * Unimplemented mmio regions needed to boot some images.
 */
#define XLNX_ZYNQMP_NUM_UNIMP_AREAS 1

struct XlnxZynqMPState {
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
    XlnxBBRam bbram;
    XlnxEFuse efuse;
    XlnxZynqMPEFuse efuse_ctrl;

    MemoryRegion mr_unimp[XLNX_ZYNQMP_NUM_UNIMP_AREAS];

    CadenceGEMState gem[XLNX_ZYNQMP_NUM_GEMS];
    CadenceUARTState uart[XLNX_ZYNQMP_NUM_UARTS];
    XlnxZynqMPCANState can[XLNX_ZYNQMP_NUM_CAN];
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
    XlnxCSUDMA qspi_dma;
    OrIRQState qspi_irq_orgate;
    XlnxZynqMPAPUCtrl apu_ctrl;
    XlnxZynqMPCRF crf;
    CadenceTTCState ttc[XLNX_ZYNQMP_NUM_TTC];
    USBDWC3 usb[XLNX_ZYNQMP_NUM_USB];

    char *boot_cpu;
    ARMCPU *boot_cpu_ptr;

    /* Has the ARM Security extensions?  */
    bool secure;
    /* Has the ARM Virtualization extensions?  */
    bool virt;

    /* CAN bus. */
    CanBusState *canbus[XLNX_ZYNQMP_NUM_CAN];
};

#endif
