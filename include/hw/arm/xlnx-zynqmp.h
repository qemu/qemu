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

#include "qemu-common.h"
#include "hw/arm/arm.h"
#include "hw/intc/arm_gic.h"
#include "hw/net/cadence_gem.h"
#include "hw/char/cadence_uart.h"

#define TYPE_XLNX_ZYNQMP "xlnx,zynqmp"
#define XLNX_ZYNQMP(obj) OBJECT_CHECK(XlnxZynqMPState, (obj), \
                                       TYPE_XLNX_ZYNQMP)

#define XLNX_ZYNQMP_NUM_APU_CPUS 4
#define XLNX_ZYNQMP_NUM_RPU_CPUS 2
#define XLNX_ZYNQMP_NUM_GEMS 4
#define XLNX_ZYNQMP_NUM_UARTS 2

#define XLNX_ZYNQMP_GIC_REGIONS 2

/* ZynqMP maps the ARM GIC regions (GICC, GICD ...) at consecutive 64k offsets
 * and under-decodes the 64k region. This mirrors the 4k regions to every 4k
 * aligned address in the 64k region. To implement each GIC region needs a
 * number of memory region aliases.
 */

#define XLNX_ZYNQMP_GIC_REGION_SIZE 0x4000
#define XLNX_ZYNQMP_GIC_ALIASES     (0x10000 / XLNX_ZYNQMP_GIC_REGION_SIZE - 1)

typedef struct XlnxZynqMPState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMCPU apu_cpu[XLNX_ZYNQMP_NUM_APU_CPUS];
    ARMCPU rpu_cpu[XLNX_ZYNQMP_NUM_RPU_CPUS];
    GICState gic;
    MemoryRegion gic_mr[XLNX_ZYNQMP_GIC_REGIONS][XLNX_ZYNQMP_GIC_ALIASES];
    CadenceGEMState gem[XLNX_ZYNQMP_NUM_GEMS];
    CadenceUARTState uart[XLNX_ZYNQMP_NUM_UARTS];

    char *boot_cpu;
    ARMCPU *boot_cpu_ptr;
}  XlnxZynqMPState;

#define XLNX_ZYNQMP_H
#endif
