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

struct PL050DeviceClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;
};

#define TYPE_PL050 "pl050"
OBJECT_DECLARE_TYPE(PL050State, PL050DeviceClass, PL050)

struct PL050State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    PS2State *ps2dev;
    uint32_t cr;
    uint32_t clk;
    uint32_t last;
    int pending;
    qemu_irq irq;
    bool is_mouse;
};

#define TYPE_PL050_KBD_DEVICE "pl050_keyboard"
OBJECT_DECLARE_SIMPLE_TYPE(PL050KbdState, PL050_KBD_DEVICE)

struct PL050KbdState {
    PL050State parent_obj;

    PS2KbdState kbd;
};

#define TYPE_PL050_MOUSE_DEVICE "pl050_mouse"
OBJECT_DECLARE_SIMPLE_TYPE(PL050MouseState, PL050_MOUSE_DEVICE)

struct PL050MouseState {
    PL050State parent_obj;

    PS2MouseState mouse;
};

#endif
