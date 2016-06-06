/*
 * ASPEED AST2400 SoC
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef AST2400_H
#define AST2400_H

#include "hw/arm/arm.h"
#include "hw/intc/aspeed_vic.h"
#include "hw/timer/aspeed_timer.h"
#include "hw/i2c/aspeed_i2c.h"

typedef struct AST2400State {
    /*< private >*/
    DeviceState parent;

    /*< public >*/
    ARMCPU *cpu;
    MemoryRegion iomem;
    AspeedVICState vic;
    AspeedTimerCtrlState timerctrl;
    AspeedI2CState i2c;
} AST2400State;

#define TYPE_AST2400 "ast2400"
#define AST2400(obj) OBJECT_CHECK(AST2400State, (obj), TYPE_AST2400)

#define AST2400_SDRAM_BASE       0x40000000

#endif /* AST2400_H */
