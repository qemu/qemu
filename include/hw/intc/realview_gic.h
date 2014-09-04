/*
 * ARM RealView Emulation Baseboard Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#ifndef HW_INTC_REALVIEW_GIC_H
#define HW_INTC_REALVIEW_GIC_H

#include "hw/sysbus.h"
#include "hw/intc/arm_gic.h"

#define TYPE_REALVIEW_GIC "realview_gic"
#define REALVIEW_GIC(obj) \
    OBJECT_CHECK(RealViewGICState, (obj), TYPE_REALVIEW_GIC)

typedef struct RealViewGICState {
    SysBusDevice parent_obj;

    MemoryRegion container;

    GICState gic;
} RealViewGICState;

#endif
