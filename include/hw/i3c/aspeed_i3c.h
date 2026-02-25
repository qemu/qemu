/*
 * ASPEED I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2023 Google, LLC
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_I3C_H
#define ASPEED_I3C_H

#include "hw/i3c/dw-i3c.h"
#include "hw/core/sysbus.h"

#define TYPE_ASPEED_I3C "aspeed.i3c"
OBJECT_DECLARE_TYPE(AspeedI3CState, AspeedI3CClass, ASPEED_I3C)

#define ASPEED_I3C_NR_REGS (0x70 >> 2)
#define ASPEED_I3C_NR_DEVICES 6

struct AspeedI3CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion iomem_container;
    qemu_irq irq;

    uint32_t regs[ASPEED_I3C_NR_REGS];
    DWI3C devices[ASPEED_I3C_NR_DEVICES];
    uint8_t id;
};
#endif /* ASPEED_I3C_H */
