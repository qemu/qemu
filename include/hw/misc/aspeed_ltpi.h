/*
 * ASPEED LTPI Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_LTPI_H
#define ASPEED_LTPI_H

#include "hw/core/sysbus.h"

#define TYPE_ASPEED_LTPI "aspeed.ltpi-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedLTPIState, ASPEED_LTPI)

#define ASPEED_LTPI_TOTAL_SIZE  0x900
#define ASPEED_LTPI_CTRL_SIZE   0x200
#define ASPEED_LTPI_PHY_SIZE    0x100
#define ASPEED_LTPI_TOP_SIZE    0x100

struct AspeedLTPIState {
    SysBusDevice parent;
    MemoryRegion mmio;
    MemoryRegion mmio_ctrl;
    MemoryRegion mmio_phy;
    MemoryRegion mmio_top;

    uint32_t ctrl_regs[ASPEED_LTPI_CTRL_SIZE >> 2];
    uint32_t phy_regs[ASPEED_LTPI_PHY_SIZE >> 2];
    uint32_t top_regs[ASPEED_LTPI_TOP_SIZE >> 2];
};

#endif /* ASPEED_LTPI_H */
