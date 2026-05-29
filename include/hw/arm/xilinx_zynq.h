/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2010 Xilinx.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.croshtwaite@petalogix.com)
 * Copyright (c) 2012 Petalogix Pty Ltd.
 * Written by Haibing Ma
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_ARM_ZYNQ_H
#define QEMU_ARM_ZYNQ_H

#include "target/arm/cpu-qom.h"
#include "hw/core/qdev-clock.h"

#define TYPE_ZYNQ_MACHINE MACHINE_TYPE_NAME("xilinx-zynq-a9")
OBJECT_DECLARE_SIMPLE_TYPE(ZynqMachineState, ZYNQ_MACHINE)

#define ZYNQ_MAX_CPUS 2

struct ZynqMachineState {
    MachineState parent;
    Clock *ps_clk;
    ARMCPU *cpu[ZYNQ_MAX_CPUS];
    uint8_t boot_mode;
};

#endif /* QEMU_ARM_ZYNQ_H */
