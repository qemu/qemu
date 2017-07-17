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

#define TYPE_CMSDK_APB_TIMER "cmsdk-apb-timer"
#define CMSDK_APB_TIMER(obj) OBJECT_CHECK(CMSDKAPBTIMER, (obj), \
                                         TYPE_CMSDK_APB_TIMER)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq timerint;
    uint32_t pclk_frq;
    struct ptimer_state *timer;

    uint32_t ctrl;
    uint32_t value;
    uint32_t reload;
    uint32_t intstatus;
} CMSDKAPBTIMER;

/**
 * cmsdk_apb_timer_create - convenience function to create TYPE_CMSDK_APB_TIMER
 * @addr: location in system memory to map registers
 * @pclk_frq: frequency in Hz of the PCLK clock (used for calculating baud rate)
 */
static inline DeviceState *cmsdk_apb_timer_create(hwaddr addr,
                                                 qemu_irq timerint,
                                                 uint32_t pclk_frq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, TYPE_CMSDK_APB_TIMER);
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_uint32(dev, "pclk-frq", pclk_frq);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, timerint);
    return dev;
}

#endif
