/*
 * Luminary Micro Stellaris General Purpose Timer Module
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#ifndef HW_TIMER_STELLARIS_GPTM_H
#define HW_TIMER_STELLARIS_GPTM_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/clock.h"

#define TYPE_STELLARIS_GPTM "stellaris-gptm"
OBJECT_DECLARE_SIMPLE_TYPE(gptm_state, STELLARIS_GPTM)

/*
 * QEMU interface:
 *  + sysbus MMIO region 0: register bank
 *  + sysbus IRQ 0: timer interrupt
 *  + unnamed GPIO output 0: trigger output for the ADC
 *  + Clock input "clk": the 32-bit countdown timer runs at this speed
 */
struct gptm_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t config;
    uint32_t mode[2];
    uint32_t control;
    uint32_t state;
    uint32_t mask;
    uint32_t load[2];
    uint32_t match[2];
    uint32_t prescale[2];
    uint32_t match_prescale[2];
    uint32_t rtc;
    int64_t tick[2];
    struct gptm_state *opaque[2];
    QEMUTimer *timer[2];
    /* The timers have an alternate output used to trigger the ADC.  */
    qemu_irq trigger;
    qemu_irq irq;
    Clock *clk;
};

#endif
