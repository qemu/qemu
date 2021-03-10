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
 * https://developer.arm.com/documentation/ecm0601256/latest
 * QEMU interface:
 *  + QOM property "SYS_VERSION": value to use for SYS_VERSION register
 *  + QOM property "SYS_CONFIG": value to use for SYS_CONFIG register
 *  + sysbus MMIO region 0: the system information register bank
 */

#ifndef HW_MISC_IOTKIT_SYSINFO_H
#define HW_MISC_IOTKIT_SYSINFO_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IOTKIT_SYSINFO "iotkit-sysinfo"
OBJECT_DECLARE_SIMPLE_TYPE(IoTKitSysInfo, IOTKIT_SYSINFO)

struct IoTKitSysInfo {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* Properties */
    uint32_t sys_version;
    uint32_t sys_config;
    uint32_t sse_version;
    uint32_t iidr;
};

#endif
