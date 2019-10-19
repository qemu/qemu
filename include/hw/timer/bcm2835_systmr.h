/*
 * BCM2835 SYS timer emulation
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2835_SYSTIMER_H
#define BCM2835_SYSTIMER_H

#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_BCM2835_SYSTIMER "bcm2835-sys-timer"
#define BCM2835_SYSTIMER(obj) \
    OBJECT_CHECK(BCM2835SystemTimerState, (obj), TYPE_BCM2835_SYSTIMER)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    struct {
        uint32_t status;
        uint32_t compare[4];
    } reg;
} BCM2835SystemTimerState;

#endif
