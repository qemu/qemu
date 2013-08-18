/*
 * ARM11MPCore Snoop Control Unit (SCU) emulation
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 * Written by Paul Brook and Andreas Färber
 *
 * This code is licensed under the GPL.
 */

#ifndef HW_MISC_ARM11SCU_H
#define HW_MISC_ARM11SCU_H

#include "hw/sysbus.h"

#define TYPE_ARM11_SCU "arm11-scu"
#define ARM11_SCU(obj) OBJECT_CHECK(ARM11SCUState, (obj), TYPE_ARM11_SCU)

typedef struct ARM11SCUState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint32_t control;
    uint32_t num_cpu;
    MemoryRegion iomem;
} ARM11SCUState;

#endif
