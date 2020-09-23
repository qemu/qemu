/*
 * Canon DIGIC timer block declarations.
 *
 * Copyright (C) 2013 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef HW_TIMER_DIGIC_TIMER_H
#define HW_TIMER_DIGIC_TIMER_H

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qom/object.h"

#define TYPE_DIGIC_TIMER "digic-timer"
OBJECT_DECLARE_SIMPLE_TYPE(DigicTimerState, DIGIC_TIMER)

#define DIGIC_TIMER_CONTROL 0x00
#define DIGIC_TIMER_CONTROL_RST 0x80000000
#define DIGIC_TIMER_CONTROL_EN 0x00000001
#define DIGIC_TIMER_RELVALUE 0x08
#define DIGIC_TIMER_VALUE 0x0c

struct DigicTimerState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    ptimer_state *ptimer;

    uint32_t control;
    uint32_t relvalue;
};

#endif /* HW_TIMER_DIGIC_TIMER_H */
