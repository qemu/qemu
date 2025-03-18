/*
 * Definitions for hexagon virt board.
 *
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HEXAGONVIRT_H
#define HW_HEXAGONVIRT_H

#include "hw/boards.h"
#include "target/hexagon/cpu.h"

struct HexagonVirtMachineState {
    /*< private >*/
    MachineState parent_obj;

    int fdt_size;
    MemoryRegion *sys;
    MemoryRegion cfgtable;
    MemoryRegion ram;
    MemoryRegion tcm;
    MemoryRegion vtcm;
    DeviceState *l2vic;
};

void hexagon_load_fdt(const struct HexagonVirtMachineState *vms);

enum {
    VIRT_UART0,
    VIRT_QTMR0,
    VIRT_QTMR1,
    VIRT_GPT,
    VIRT_MMIO,
    VIRT_FDT,
};

#define TYPE_HEXAGON_VIRT_MACHINE MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(HexagonVirtMachineState, HEXAGON_VIRT_MACHINE)

#endif /* HW_HEXAGONVIRT_H */
