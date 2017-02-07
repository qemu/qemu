/*
 * ASPEED Watchdog Controller
 *
 * Copyright (C) 2016-2017 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#ifndef ASPEED_WDT_H
#define ASPEED_WDT_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_WDT "aspeed.wdt"
#define ASPEED_WDT(obj) \
    OBJECT_CHECK(AspeedWDTState, (obj), TYPE_ASPEED_WDT)

#define ASPEED_WDT_REGS_MAX        (0x20 / 4)

typedef struct AspeedWDTState {
    /*< private >*/
    SysBusDevice parent_obj;
    QEMUTimer *timer;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[ASPEED_WDT_REGS_MAX];

    uint32_t pclk_freq;
} AspeedWDTState;

#endif  /* ASPEED_WDT_H */
