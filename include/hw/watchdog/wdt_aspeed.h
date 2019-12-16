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

#include "hw/misc/aspeed_scu.h"
#include "hw/sysbus.h"

#define TYPE_ASPEED_WDT "aspeed.wdt"
#define ASPEED_WDT(obj) \
    OBJECT_CHECK(AspeedWDTState, (obj), TYPE_ASPEED_WDT)
#define TYPE_ASPEED_2400_WDT TYPE_ASPEED_WDT "-ast2400"
#define TYPE_ASPEED_2500_WDT TYPE_ASPEED_WDT "-ast2500"
#define TYPE_ASPEED_2600_WDT TYPE_ASPEED_WDT "-ast2600"

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
} AspeedWDTState;

#define ASPEED_WDT_CLASS(klass) \
     OBJECT_CLASS_CHECK(AspeedWDTClass, (klass), TYPE_ASPEED_WDT)
#define ASPEED_WDT_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AspeedWDTClass, (obj), TYPE_ASPEED_WDT)

typedef struct AspeedWDTClass {
    SysBusDeviceClass parent_class;

    uint32_t offset;
    uint32_t ext_pulse_width_mask;
    uint32_t reset_ctrl_reg;
    void (*reset_pulse)(AspeedWDTState *s, uint32_t property);
    void (*wdt_reload)(AspeedWDTState *s);
}  AspeedWDTClass;

#endif /* WDT_ASPEED_H */
