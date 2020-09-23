/*
 * BCM2835 SOC MPHI state definitions
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
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
 */

#ifndef HW_MISC_BCM2835_MPHI_H
#define HW_MISC_BCM2835_MPHI_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define MPHI_MMIO_SIZE      0x1000

typedef struct BCM2835MphiState BCM2835MphiState;

struct BCM2835MphiState {
    SysBusDevice parent_obj;
    qemu_irq irq;
    MemoryRegion iomem;

    uint32_t outdda;
    uint32_t outddb;
    uint32_t ctrl;
    uint32_t intstat;
    uint32_t swirq;
};

#define TYPE_BCM2835_MPHI   "bcm2835-mphi"

OBJECT_DECLARE_SIMPLE_TYPE(BCM2835MphiState, BCM2835_MPHI)

#endif
