/*
 * ASPEED Watchdog Controller
 *
 * Copyright (C) 2016-2017 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef WDT_ASPEED_H
#define WDT_ASPEED_H

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

    AspeedSCUState *scu;
    uint32_t pclk_freq;
    uint32_t silicon_rev;
    uint32_t ext_pulse_width_mask;
} AspeedWDTState;

#endif /* WDT_ASPEED_H */
