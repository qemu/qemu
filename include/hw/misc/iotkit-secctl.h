/*
 * ARM IoT Kit security controller
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the security controller which is part of the
 * Arm IoT Kit and documented in
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 *
 * QEMU interface:
 *  + sysbus MMIO region 0 is the "secure privilege control block" registers
 *  + sysbus MMIO region 1 is the "non-secure privilege control block" registers
 */

#ifndef IOTKIT_SECCTL_H
#define IOTKIT_SECCTL_H

#include "hw/sysbus.h"

#define TYPE_IOTKIT_SECCTL "iotkit-secctl"
#define IOTKIT_SECCTL(obj) OBJECT_CHECK(IoTKitSecCtl, (obj), TYPE_IOTKIT_SECCTL)

typedef struct IoTKitSecCtl {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    MemoryRegion s_regs;
    MemoryRegion ns_regs;
} IoTKitSecCtl;

#endif
