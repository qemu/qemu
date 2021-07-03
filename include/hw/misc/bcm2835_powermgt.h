/*
 * BCM2835 Power Management emulation
 *
 * Copyright (C) 2017 Marcin Chojnacki <marcinch7@gmail.com>
 * Copyright (C) 2021 Nolan Leake <nolan@sigbus.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_POWERMGT_H
#define BCM2835_POWERMGT_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2835_POWERMGT "bcm2835-powermgt"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835PowerMgtState, BCM2835_POWERMGT)

struct BCM2835PowerMgtState {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t rstc;
    uint32_t rsts;
    uint32_t wdog;
};

#endif
