/*
 * ASPEED AST1700 IO Expander
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_AST1700_H
#define ASPEED_AST1700_H

#include "hw/core/sysbus.h"
#include "hw/misc/aspeed_ltpi.h"
#include "hw/char/serial-mm.h"

#define TYPE_ASPEED_AST1700 "aspeed.ast1700"

OBJECT_DECLARE_SIMPLE_TYPE(AspeedAST1700SoCState, ASPEED_AST1700)

struct AspeedAST1700SoCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t board_idx;

    AspeedLTPIState ltpi;
    SerialMM uart;
    MemoryRegion sram;
};

#endif /* ASPEED_AST1700_H */
