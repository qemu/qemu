/*
 * ARM IoTKit system control element
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "system control element" which is part of the
 * Arm IoTKit and documented in
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 * Specifically, it implements the "system information block" and
 * "system control register" blocks.
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the system information register bank
 *  + sysbus MMIO region 1: the system control register bank
 */

#ifndef HW_MISC_IOTKIT_SYSCTL_H
#define HW_MISC_IOTKIT_SYSCTL_H

#include "hw/sysbus.h"

#define TYPE_IOTKIT_SYSCTL "iotkit-sysctl"
#define IOTKIT_SYSCTL(obj) OBJECT_CHECK(IoTKitSysCtl, (obj), \
                                        TYPE_IOTKIT_SYSCTL)

typedef struct IoTKitSysCtl {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t secure_debug;
    uint32_t reset_syndrome;
    uint32_t reset_mask;
    uint32_t gretreg;
    uint32_t initsvrtor0;
    uint32_t cpuwait;
    uint32_t wicctrl;
} IoTKitSysCtl;

#endif
