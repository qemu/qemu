/*
 * ARM CMSDK APB timer emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef CMSDK_APB_TIMER_H
#define CMSDK_APB_TIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/clock.h"
#include "qom/object.h"

#define TYPE_CMSDK_APB_TIMER "cmsdk-apb-timer"
OBJECT_DECLARE_SIMPLE_TYPE(CMSDKAPBTimer, CMSDK_APB_TIMER)

/*
 * QEMU interface:
 *  + Clock input "pclk": clock for the timer
 *  + sysbus MMIO region 0: the register bank
 *  + sysbus IRQ 0: timer interrupt TIMERINT
 */
struct CMSDKAPBTimer {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq timerint;
    struct ptimer_state *timer;
    Clock *pclk;

    uint32_t ctrl;
    uint32_t value;
    uint32_t reload;
    uint32_t intstatus;
};

#endif
