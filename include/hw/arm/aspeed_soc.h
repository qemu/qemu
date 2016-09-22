/*
 * ASPEED SoC family
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_SOC_H
#define ASPEED_SOC_H

#include "hw/arm/arm.h"
#include "hw/intc/aspeed_vic.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/misc/aspeed_sdmc.h"
#include "hw/timer/aspeed_timer.h"
#include "hw/i2c/aspeed_i2c.h"
#include "hw/ssi/aspeed_smc.h"

typedef struct AspeedSoCState {
    /*< private >*/
    DeviceState parent;

    /*< public >*/
    ARMCPU *cpu;
    MemoryRegion iomem;
    AspeedVICState vic;
    AspeedTimerCtrlState timerctrl;
    AspeedI2CState i2c;
    AspeedSCUState scu;
    AspeedSMCState smc;
    AspeedSMCState spi;
    AspeedSDMCState sdmc;
} AspeedSoCState;

#define TYPE_ASPEED_SOC "aspeed-soc"
#define ASPEED_SOC(obj) OBJECT_CHECK(AspeedSoCState, (obj), TYPE_ASPEED_SOC)

#define AST2400_SDRAM_BASE       0x40000000

#endif /* ASPEED_SOC_H */
