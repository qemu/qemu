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
#include "qom/object.h"

#define TYPE_BCM2835_SYSTIMER "bcm2835-sys-timer"
typedef struct BCM2835SystemTimerState BCM2835SystemTimerState;
#define BCM2835_SYSTIMER(obj) \
    OBJECT_CHECK(BCM2835SystemTimerState, (obj), TYPE_BCM2835_SYSTIMER)

struct BCM2835SystemTimerState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    struct {
        uint32_t status;
        uint32_t compare[4];
    } reg;
};

#endif
