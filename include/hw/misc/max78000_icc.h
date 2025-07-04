/*
 * MAX78000 Instruction Cache
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MAX78000_ICC_H
#define HW_MAX78000_ICC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MAX78000_ICC "max78000-icc"
OBJECT_DECLARE_SIMPLE_TYPE(Max78000IccState, MAX78000_ICC)

#define ICC_INFO       0x0
#define ICC_SZ         0x4
#define ICC_CTRL       0x100
#define ICC_INVALIDATE 0x700

struct Max78000IccState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t info;
    uint32_t sz;
    uint32_t ctrl;
};

#endif
