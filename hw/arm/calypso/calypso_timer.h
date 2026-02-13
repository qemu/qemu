/*
 * calypso_timer.h — Calypso GP/Watchdog Timer QOM device
 *
 * 16-bit down-counter with auto-reload and IRQ support.
 * Clocked from 13 MHz / (prescaler + 1).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_CALYPSO_TIMER_H
#define HW_TIMER_CALYPSO_TIMER_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_CALYPSO_TIMER "calypso-timer"
OBJECT_DECLARE_SIMPLE_TYPE(CalypsoTimerState, CALYPSO_TIMER)

struct CalypsoTimerState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    QEMUTimer    *timer;
    qemu_irq     irq;

    uint16_t load;        /* Reload value */
    uint16_t count;       /* Current counter */
    uint16_t ctrl;        /* Control: bit0=start, bit1=auto-reload, bit2=irq-en */
    uint16_t prescaler;
    int64_t  tick_ns;     /* Nanoseconds per tick */
    bool     running;
};

#endif /* HW_TIMER_CALYPSO_TIMER_H */
