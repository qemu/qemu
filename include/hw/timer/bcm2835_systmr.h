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
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_BCM2835_SYSTIMER "bcm2835-sys-timer"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835SystemTimerState, BCM2835_SYSTIMER)

#define BCM2835_SYSTIMER_COUNT 4

typedef struct {
    unsigned id;
    QEMUTimer timer;
    qemu_irq irq;
    BCM2835SystemTimerState *state;
} BCM2835SystemTimerCompare;

struct BCM2835SystemTimerState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    struct {
        uint32_t ctrl_status;
        uint32_t compare[BCM2835_SYSTIMER_COUNT];
    } reg;
    BCM2835SystemTimerCompare tmr[BCM2835_SYSTIMER_COUNT];
};

#endif
