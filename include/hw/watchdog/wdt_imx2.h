/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX2_WDT_H
#define IMX2_WDT_H

#include "hw/sysbus.h"

#define TYPE_IMX2_WDT "imx2.wdt"
#define IMX2_WDT(obj) OBJECT_CHECK(IMX2WdtState, (obj), TYPE_IMX2_WDT)

enum IMX2WdtRegisters {
    IMX2_WDT_WCR     = 0x0000,
    IMX2_WDT_REG_NUM = 0x0008 / sizeof(uint16_t) + 1,
};


typedef struct IMX2WdtState {
    /* <private> */
    SysBusDevice parent_obj;

    MemoryRegion mmio;
} IMX2WdtState;

#endif /* IMX2_WDT_H */
