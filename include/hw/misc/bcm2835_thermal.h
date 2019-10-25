/*
 * BCM2835 dummy thermal sensor
 *
 * Copyright (C) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_BCM2835_THERMAL_H
#define HW_MISC_BCM2835_THERMAL_H

#include "hw/sysbus.h"

#define TYPE_BCM2835_THERMAL "bcm2835-thermal"

#define BCM2835_THERMAL(obj) \
    OBJECT_CHECK(Bcm2835ThermalState, (obj), TYPE_BCM2835_THERMAL)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion iomem;
    uint32_t ctl;
} Bcm2835ThermalState;

#endif
