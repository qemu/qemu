/*
 * ASPEED Hash and Crypto Engine
 *
 * Copyright (C) 2021 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_HACE_H
#define ASPEED_HACE_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_HACE "aspeed.hace"
#define TYPE_ASPEED_AST2400_HACE TYPE_ASPEED_HACE "-ast2400"
#define TYPE_ASPEED_AST2500_HACE TYPE_ASPEED_HACE "-ast2500"
#define TYPE_ASPEED_AST2600_HACE TYPE_ASPEED_HACE "-ast2600"
OBJECT_DECLARE_TYPE(AspeedHACEState, AspeedHACEClass, ASPEED_HACE)

#define ASPEED_HACE_NR_REGS (0x64 >> 2)

struct AspeedHACEState {
    SysBusDevice parent;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_HACE_NR_REGS];

    MemoryRegion *dram_mr;
    AddressSpace dram_as;
};


struct AspeedHACEClass {
    SysBusDeviceClass parent_class;

    uint32_t src_mask;
    uint32_t dest_mask;
    uint32_t hash_mask;
};

#endif /* _ASPEED_HACE_H_ */
