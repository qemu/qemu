/*
 * ARMv7M SysTick timer
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell
 *
 * This code is licensed under the GPL (version 2 or later).
 */

#ifndef HW_TIMER_ARMV7M_SYSTICK_H
#define HW_TIMER_ARMV7M_SYSTICK_H

#include "hw/sysbus.h"

#define TYPE_SYSTICK "armv7m_systick"

#define SYSTICK(obj) OBJECT_CHECK(SysTickState, (obj), TYPE_SYSTICK)

typedef struct SysTickState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint32_t control;
    uint32_t reload;
    int64_t tick;
    QEMUTimer *timer;
    MemoryRegion iomem;
    qemu_irq irq;
} SysTickState;

/*
 * Multiplication factor to convert from system clock ticks to qemu timer
 * ticks. This should be set (by board code, usually) to a value
 * equal to NANOSECONDS_PER_SECOND / frq, where frq is the clock frequency
 * in Hz of the CPU.
 *
 * This value is used by the systick device when it is running in
 * its "use the CPU clock" mode (ie when SYST_CSR.CLKSOURCE == 1) to
 * set how fast the timer should tick.
 *
 * TODO: we should refactor this so that rather than using a global
 * we use a device property or something similar. This is complicated
 * because (a) the property would need to be plumbed through from the
 * board code down through various layers to the systick device
 * and (b) the property needs to be modifiable after realize, because
 * the stellaris board uses this to implement the behaviour where the
 * guest can reprogram the PLL registers to downclock the CPU, and the
 * systick device needs to react accordingly. Possibly this should
 * be deferred until we have a good API for modelling clock trees.
 */
extern int system_clock_scale;

#endif
