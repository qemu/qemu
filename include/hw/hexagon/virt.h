/*
 * Definitions for hexagon virt board.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGONVIRT_H
#define HW_HEXAGONVIRT_H

#include "hw/hexagon/hexagon.h"
#include "target/hexagon/cpu.h"

struct HexagonVirtMachineState {
    HexagonCommonMachineState parent_obj;

    int fdt_size;
    MemoryRegion *sys;
    MemoryRegion tcm;
    MemoryRegion vtcm;
    MemoryRegion bios;
    Clock *apb_clk;
};

void hexagon_load_fdt(const struct HexagonVirtMachineState *vms);

#define TYPE_HEXAGON_VIRT_MACHINE MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(HexagonVirtMachineState, HEXAGON_VIRT_MACHINE)

#endif /* HW_HEXAGONVIRT_H */
