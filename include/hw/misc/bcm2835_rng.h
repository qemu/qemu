/*
 * BCM2835 Random Number Generator emulation
 *
 * Copyright (C) 2017 Marcin Chojnacki <marcinch7@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_RNG_H
#define BCM2835_RNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2835_RNG "bcm2835-rng"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835RngState, BCM2835_RNG)

struct BCM2835RngState {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t rng_ctrl;
    uint32_t rng_status;
};

#endif
