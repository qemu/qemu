/*
 * Arm PrimeCell PL050 Keyboard / Mouse Interface
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#ifndef HW_PL050_H
#define HW_PL050_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/input/ps2.h"
#include "hw/irq.h"

#define TYPE_PL050 "pl050"
OBJECT_DECLARE_SIMPLE_TYPE(PL050State, PL050)

struct PL050State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    void *dev;
    uint32_t cr;
    uint32_t clk;
    uint32_t last;
    int pending;
    qemu_irq irq;
    bool is_mouse;
};

#endif
