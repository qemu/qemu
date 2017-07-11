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
#include "hw/watchdog/wdt_aspeed.h"
#include "hw/net/ftgmac100.h"

#define ASPEED_SPIS_NUM  2
#define ASPEED_WDTS_NUM  3

typedef struct AspeedSoCState {
    /*< private >*/
    DeviceState parent;

    /*< public >*/
    ARMCPU cpu;
    MemoryRegion iomem;
    MemoryRegion sram;
    AspeedVICState vic;
    AspeedTimerCtrlState timerctrl;
    AspeedI2CState i2c;
    AspeedSCUState scu;
    AspeedSMCState fmc;
    AspeedSMCState spi[ASPEED_SPIS_NUM];
    AspeedSDMCState sdmc;
    AspeedWDTState wdt[ASPEED_WDTS_NUM];
    FTGMAC100State ftgmac100;
} AspeedSoCState;

#define TYPE_ASPEED_SOC "aspeed-soc"
#define ASPEED_SOC(obj) OBJECT_CHECK(AspeedSoCState, (obj), TYPE_ASPEED_SOC)

typedef struct AspeedSoCInfo {
    const char *name;
    const char *cpu_model;
    uint32_t silicon_rev;
    hwaddr sdram_base;
    uint64_t sram_size;
    int spis_num;
    const hwaddr *spi_bases;
    const char *fmc_typename;
    const char **spi_typename;
    int wdts_num;
} AspeedSoCInfo;

typedef struct AspeedSoCClass {
    DeviceClass parent_class;
    AspeedSoCInfo *info;
} AspeedSoCClass;

#define ASPEED_SOC_CLASS(klass)                                         \
    OBJECT_CLASS_CHECK(AspeedSoCClass, (klass), TYPE_ASPEED_SOC)
#define ASPEED_SOC_GET_CLASS(obj)                               \
    OBJECT_GET_CLASS(AspeedSoCClass, (obj), TYPE_ASPEED_SOC)

#endif /* ASPEED_SOC_H */
