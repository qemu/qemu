/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * ASPEED APB2OPB Bridge
 * IBM On-Chip Peripheral Bus
 */
#ifndef FSI_ASPEED_APB2OPB_H
#define FSI_ASPEED_APB2OPB_H

#include "system/memory.h"
#include "hw/fsi/fsi-master.h"
#include "hw/sysbus.h"

#define TYPE_FSI_OPB "fsi.opb"

#define TYPE_OP_BUS "opb"
OBJECT_DECLARE_SIMPLE_TYPE(OPBus, OP_BUS)

typedef struct OPBus {
    BusState bus;

    MemoryRegion mr;
    AddressSpace as;
} OPBus;

#define TYPE_ASPEED_APB2OPB "aspeed.apb2opb"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedAPB2OPBState, ASPEED_APB2OPB)

#define ASPEED_APB2OPB_NR_REGS ((0xe8 >> 2) + 1)

#define ASPEED_FSI_NUM 2

typedef struct AspeedAPB2OPBState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t regs[ASPEED_APB2OPB_NR_REGS];
    qemu_irq irq;

    OPBus opb[ASPEED_FSI_NUM];
    FSIMasterState fsi[ASPEED_FSI_NUM];
} AspeedAPB2OPBState;

#endif /* FSI_ASPEED_APB2OPB_H */
