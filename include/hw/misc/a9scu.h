/*
 * Cortex-A9MPCore Snoop Control Unit (SCU) emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * This code is licensed under the GPL.
 */
#ifndef HW_MISC_A9SCU_H
#define HW_MISC_A9SCU_H

#include "hw/sysbus.h"

/* A9MP private memory region.  */

typedef struct A9SCUState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t control;
    uint32_t status;
    uint32_t num_cpu;
} A9SCUState;

#define TYPE_A9_SCU "a9-scu"
#define A9_SCU(obj) OBJECT_CHECK(A9SCUState, (obj), TYPE_A9_SCU)

#endif
