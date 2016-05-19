/*
 * Misc Canon DIGIC declarations.
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

#ifndef HW_ARM_DIGIC_H
#define HW_ARM_DIGIC_H

#include "cpu.h"
#include "hw/timer/digic-timer.h"
#include "hw/char/digic-uart.h"

#define TYPE_DIGIC "digic"

#define DIGIC(obj) OBJECT_CHECK(DigicState, (obj), TYPE_DIGIC)

#define DIGIC4_NB_TIMERS 3

typedef struct DigicState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    ARMCPU cpu;

    DigicTimerState timer[DIGIC4_NB_TIMERS];
    DigicUartState uart;
} DigicState;

#endif /* HW_ARM_DIGIC_H */
