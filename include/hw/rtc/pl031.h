/*
 * ARM AMBA PrimeCell PL031 RTC
 *
 * Copyright (c) 2007 CodeSourcery
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_RTC_PL031_H
#define HW_RTC_PL031_H

#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_PL031 "pl031"
#define PL031(obj) OBJECT_CHECK(PL031State, (obj), TYPE_PL031)

typedef struct PL031State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    /*
     * Needed to preserve the tick_count across migration, even if the
     * absolute value of the rtc_clock is different on the source and
     * destination.
     */
    uint32_t tick_offset_vmstate;
    uint32_t tick_offset;
    bool tick_offset_migrated;
    bool migrate_tick_offset;

    uint32_t mr;
    uint32_t lr;
    uint32_t cr;
    uint32_t im;
    uint32_t is;
} PL031State;

#endif
