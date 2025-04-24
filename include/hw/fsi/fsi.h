/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#ifndef FSI_FSI_H
#define FSI_FSI_H

#include "system/memory.h"
#include "hw/qdev-core.h"
#include "hw/fsi/lbus.h"
#include "qemu/bitops.h"

/* Bitwise operations at the word level. */
#define BE_GENMASK(hb, lb)  MAKE_64BIT_MASK((lb), ((hb) - (lb) + 1))

#define TYPE_FSI_BUS "fsi.bus"
OBJECT_DECLARE_SIMPLE_TYPE(FSIBus, FSI_BUS)

typedef struct FSIBus {
    BusState bus;
} FSIBus;

#define TYPE_FSI_SLAVE "fsi.slave"
OBJECT_DECLARE_SIMPLE_TYPE(FSISlaveState, FSI_SLAVE)

#define FSI_SLAVE_CONTROL_NR_REGS ((0x40 >> 2) + 1)

typedef struct FSISlaveState {
    DeviceState parent;

    MemoryRegion iomem;
    uint32_t regs[FSI_SLAVE_CONTROL_NR_REGS];
} FSISlaveState;

#endif /* FSI_FSI_H */
