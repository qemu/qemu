/*
 * ARM SSE CPU PWRCTRL register block
 *
 * Copyright (c) 2021 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "CPU<N>_PWRCTRL block" which is part of the
 * Arm Corstone SSE-300 Example Subsystem and documented in
 * https://developer.arm.com/documentation/101773/0000
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the register bank
 */

#ifndef HW_MISC_ARMSSE_CPU_PWRCTRL_H
#define HW_MISC_ARMSSE_CPU_PWRCTRL_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ARMSSE_CPU_PWRCTRL "armsse-cpu-pwrctrl"
OBJECT_DECLARE_SIMPLE_TYPE(ARMSSECPUPwrCtrl, ARMSSE_CPU_PWRCTRL)

struct ARMSSECPUPwrCtrl {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t cpupwrcfg;
};

#endif
