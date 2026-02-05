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
#include "hw/misc/aspeed_scu.h"
#include "hw/adc/aspeed_adc.h"
#include "hw/gpio/aspeed_gpio.h"
#include "hw/gpio/aspeed_sgpio.h"
#include "hw/i2c/aspeed_i2c.h"
#include "hw/misc/aspeed_ltpi.h"
#include "hw/misc/aspeed_pwm.h"
#include "hw/ssi/aspeed_smc.h"
#include "hw/watchdog/wdt_aspeed.h"
#include "hw/char/serial-mm.h"
#include "hw/misc/unimp.h"

#define AST1700_SGPIO_NUM            2
#define AST1700_WDT_NUM              9

#define TYPE_ASPEED_AST1700 "aspeed.ast1700"

OBJECT_DECLARE_SIMPLE_TYPE(AspeedAST1700SoCState, ASPEED_AST1700)

struct AspeedAST1700SoCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dram_mr;
    uint8_t board_idx;
    uint32_t silicon_rev;

    AspeedLTPIState ltpi;
    SerialMM uart;
    MemoryRegion sram;
    AspeedSMCState spi;
    AspeedADCState adc;
    AspeedSCUState scu;
    AspeedGPIOState gpio;
    AspeedSGPIOState sgpiom[AST1700_SGPIO_NUM];
    AspeedI2CState i2c;
    AspeedPWMState pwm;
    AspeedWDTState wdt[AST1700_WDT_NUM];

    UnimplementedDeviceState i3c;
};

#endif /* ASPEED_AST1700_H */
