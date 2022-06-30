/*
 * Aspeed PECI Controller
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#ifndef ASPEED_PECI_H
#define ASPEED_PECI_H

#include "hw/sysbus.h"

#define ASPEED_PECI_NR_REGS ((0xFC + 4) >> 2)
#define TYPE_ASPEED_PECI "aspeed.peci"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedPECIState, ASPEED_PECI);

struct AspeedPECIState {
    /* <private> */
    SysBusDevice parent;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t regs[ASPEED_PECI_NR_REGS];
};

#endif
