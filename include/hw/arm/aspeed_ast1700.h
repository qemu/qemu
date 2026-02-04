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
#include "hw/adc/aspeed_adc.h"
#include "hw/misc/aspeed_ltpi.h"
#include "hw/ssi/aspeed_smc.h"
#include "hw/char/serial-mm.h"

#define TYPE_ASPEED_AST1700 "aspeed.ast1700"

OBJECT_DECLARE_SIMPLE_TYPE(AspeedAST1700SoCState, ASPEED_AST1700)

struct AspeedAST1700SoCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dram_mr;
    uint8_t board_idx;

    AspeedLTPIState ltpi;
    SerialMM uart;
    MemoryRegion sram;
    AspeedSMCState spi;
    AspeedADCState adc;
};

#endif /* ASPEED_AST1700_H */
