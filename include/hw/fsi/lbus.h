/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Local bus and connected device structures.
 */
#ifndef FSI_LBUS_H
#define FSI_LBUS_H

#include "hw/qdev-core.h"
#include "qemu/units.h"
#include "system/memory.h"

#define TYPE_FSI_LBUS_DEVICE "fsi.lbus.device"
OBJECT_DECLARE_SIMPLE_TYPE(FSILBusDevice, FSI_LBUS_DEVICE)

typedef struct FSILBusDevice {
    DeviceState parent;

    MemoryRegion iomem;
} FSILBusDevice;

#define TYPE_FSI_LBUS "fsi.lbus"
OBJECT_DECLARE_SIMPLE_TYPE(FSILBus, FSI_LBUS)

typedef struct FSILBus {
    BusState bus;

    MemoryRegion mr;
} FSILBus;

#define TYPE_FSI_SCRATCHPAD "fsi.scratchpad"
#define SCRATCHPAD(obj) OBJECT_CHECK(FSIScratchPad, (obj), TYPE_FSI_SCRATCHPAD)

#define FSI_SCRATCHPAD_NR_REGS 4

typedef struct FSIScratchPad {
        FSILBusDevice parent;

        uint32_t regs[FSI_SCRATCHPAD_NR_REGS];
} FSIScratchPad;

#endif /* FSI_LBUS_H */
