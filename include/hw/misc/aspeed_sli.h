/*
 * ASPEED SLI Controller
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_SLI_H
#define ASPEED_SLI_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_SLI "aspeed.sli"
#define TYPE_ASPEED_2700_SLI TYPE_ASPEED_SLI "-ast2700"
#define TYPE_ASPEED_2700_SLIIO TYPE_ASPEED_SLI "io" "-ast2700"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedSLIState, ASPEED_SLI)

#define ASPEED_SLI_NR_REGS  (0x500 >> 2)

struct AspeedSLIState {
    SysBusDevice parent;
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SLI_NR_REGS];
};

#endif /* ASPEED_SLI_H */
