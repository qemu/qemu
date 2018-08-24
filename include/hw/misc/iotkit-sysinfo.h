/*
 * ARM IoTKit system information block
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "system information block" which is part of the
 * Arm IoTKit and documented in
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 * QEMU interface:
 *  + sysbus MMIO region 0: the system information register bank
 */

#ifndef HW_MISC_IOTKIT_SYSINFO_H
#define HW_MISC_IOTKIT_SYSINFO_H

#include "hw/sysbus.h"

#define TYPE_IOTKIT_SYSINFO "iotkit-sysinfo"
#define IOTKIT_SYSINFO(obj) OBJECT_CHECK(IoTKitSysInfo, (obj), \
                                        TYPE_IOTKIT_SYSINFO)

typedef struct IoTKitSysInfo {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
} IoTKitSysInfo;

#endif
