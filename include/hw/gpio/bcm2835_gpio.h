/*
 * Raspberry Pi (BCM2835) GPIO Controller
 *
 * Copyright (c) 2017 Antfield SAS
 *
 * Authors:
 *  Clement Deschamps <clement.deschamps@antfield.fr>
 *  Luc Michel <luc.michel@antfield.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_GPIO_H
#define BCM2835_GPIO_H

#include "hw/sd/sd.h"
#include "hw/sysbus.h"

typedef struct BCM2835GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* SDBus selector */
    SDBus sdbus;
    SDBus *sdbus_sdhci;
    SDBus *sdbus_sdhost;

    uint8_t fsel[54];
    uint32_t lev0, lev1;
    uint8_t sd_fsel;
    qemu_irq out[54];
} BCM2835GpioState;

#define TYPE_BCM2835_GPIO "bcm2835_gpio"
#define BCM2835_GPIO(obj) \
    OBJECT_CHECK(BCM2835GpioState, (obj), TYPE_BCM2835_GPIO)

#endif
