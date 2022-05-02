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
#include "qom/object.h"

#define TYPE_ASPEED_WDT "aspeed.wdt"
OBJECT_DECLARE_TYPE(AspeedWDTState, AspeedWDTClass, ASPEED_WDT)
#define TYPE_ASPEED_2400_WDT TYPE_ASPEED_WDT "-ast2400"
#define TYPE_ASPEED_2500_WDT TYPE_ASPEED_WDT "-ast2500"
#define TYPE_ASPEED_2600_WDT TYPE_ASPEED_WDT "-ast2600"
#define TYPE_ASPEED_1030_WDT TYPE_ASPEED_WDT "-ast1030"

#define ASPEED_WDT_REGS_MAX        (0x20 / 4)

struct AspeedWDTState {
    /*< private >*/
    SysBusDevice parent_obj;
    QEMUTimer *timer;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[ASPEED_WDT_REGS_MAX];

    AspeedSCUState *scu;
    uint32_t pclk_freq;
};


struct AspeedWDTClass {
    SysBusDeviceClass parent_class;

    uint32_t offset;
    uint32_t ext_pulse_width_mask;
    uint32_t reset_ctrl_reg;
    void (*reset_pulse)(AspeedWDTState *s, uint32_t property);
    void (*wdt_reload)(AspeedWDTState *s);
    uint64_t (*sanitize_ctrl)(uint64_t data);
    uint32_t default_status;
    uint32_t default_reload_value;
};

#endif /* WDT_ASPEED_H */
