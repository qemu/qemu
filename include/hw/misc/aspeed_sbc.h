/*
 * ASPEED Secure Boot Controller
 *
 * Copyright (C) 2021-2022 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_SBC_H
#define ASPEED_SBC_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_SBC "aspeed.sbc"
#define TYPE_ASPEED_AST2600_SBC TYPE_ASPEED_SBC "-ast2600"
OBJECT_DECLARE_TYPE(AspeedSBCState, AspeedSBCClass, ASPEED_SBC)

#define ASPEED_SBC_NR_REGS (0x93c >> 2)

struct AspeedSBCState {
    SysBusDevice parent;

    MemoryRegion iomem;

    uint32_t regs[ASPEED_SBC_NR_REGS];
};

struct AspeedSBCClass {
    SysBusDeviceClass parent_class;
};

#endif /* _ASPEED_SBC_H_ */
