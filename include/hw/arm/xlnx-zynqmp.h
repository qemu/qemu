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

#define TYPE_XLNX_ZYNQMP "xlnx,zynqmp"
#define XLNX_ZYNQMP(obj) OBJECT_CHECK(XlnxZynqMPState, (obj), \
                                       TYPE_XLNX_ZYNQMP)

#define XLNX_ZYNQMP_NUM_CPUS 4

typedef struct XlnxZynqMPState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    ARMCPU cpu[XLNX_ZYNQMP_NUM_CPUS];
}  XlnxZynqMPState;

#define XLNX_ZYNQMP_H
#endif
