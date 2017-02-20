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

#endif
