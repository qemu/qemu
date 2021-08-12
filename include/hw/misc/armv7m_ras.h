/*
 * Arm M-profile RAS (Reliability, Availability and Serviceability) block
 *
 * Copyright (c) 2021 Linaro Limited
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the RAS register block of an M-profile CPU
 * (the registers starting at 0xE0005000 with ERRFRn).
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the register bank
 *
 * The QEMU implementation currently provides "minimal RAS" only.
 */

#ifndef HW_MISC_ARMV7M_RAS_H
#define HW_MISC_ARMV7M_RAS_H

#include "hw/sysbus.h"

#define TYPE_ARMV7M_RAS "armv7m-ras"
OBJECT_DECLARE_SIMPLE_TYPE(ARMv7MRAS, ARMV7M_RAS)

struct ARMv7MRAS {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
};

#endif
