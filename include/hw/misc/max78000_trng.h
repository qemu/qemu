/*
 * MAX78000 True Random Number Generator
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MAX78000_TRNG_H
#define HW_MAX78000_TRNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MAX78000_TRNG "max78000-trng"
OBJECT_DECLARE_SIMPLE_TYPE(Max78000TrngState, MAX78000_TRNG)

#define CTRL 0
#define STATUS 4
#define DATA 8

#define RND_IE (1 << 1)

struct Max78000TrngState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t ctrl;
    uint32_t status;
    uint32_t data;

    qemu_irq irq;
};

#endif
